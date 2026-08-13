#include "src/frontend/tui.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <utility>

#include "absl/log/log.h"
#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/terminal.hpp"
#include "src/character/character.h"
#include "src/character/exp_table.h"
#include "src/character/progression.h"
#include "src/combat/combat.h"
#include "src/frontend/main_layout.h"
#include "src/frontend/panels/character_panel.h"
#include "src/frontend/panels/combat_panel.h"
#include "src/frontend/panels/equipped_panel.h"
#include "src/frontend/panels/hotkeys_panel.h"
#include "src/frontend/panels/inventory_panel.h"
#include "src/frontend/screens/map_select_panel.h"
#include "src/frontend/screens/scroll_panel.h"
#include "src/frontend/tui_controller.h"
#include "src/frontend/widgets/amount_selector.h"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/item_menu.h"
#include "src/frontend/widgets/marquee.h"
#include "src/frontend/widgets/panel_util.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/protos/character.pb.h"
#include "src/protos/skill.pb.h"
#include "src/save.h"

namespace ms {
namespace {

// Returns decimal places for EXP percentage display, scaled by job tier.
int ExpPctDecimals(int level) {
  if (level < 60) {
    return 0;
  }
  if (level < 100) {
    return 1;
  }
  if (level < 200) {
    return 2;
  }
  if (level < 260) {
    return 3;
  }
  return 4;
}

// Raised by a signal asking the game to close: Ctrl+C, a killed process, or a
// terminal that went away. A handler may do almost nothing safely, so it does
// exactly one thing -- sets this -- and the loop notices on its next tick and
// leaves through the ordinary path, saving on the way like any other exit.
volatile std::sig_atomic_t g_leaving = 0;

extern "C" void NoteLeaving(int) {
  g_leaving = 1;
}

void HandleClosingSignals() {
  std::signal(SIGINT, NoteLeaving);
  std::signal(SIGTERM, NoteLeaving);
#ifdef SIGHUP
  // The terminal window's X, on the platforms that have it.
  std::signal(SIGHUP, NoteLeaving);
#endif
}

}  // namespace

Tui::Tui(GameState& state, std::string save_path)
    : state_(state),
      save_path_(std::move(save_path)),
      last_level_seen_(state.character.proto().level()),
      last_job_seen_(state.character.proto().job()),
      last_save_(std::chrono::steady_clock::now()),
      last_combat_update_(std::chrono::steady_clock::now()),
      char_panel_(state.character, panel_focus_, state.skills),
      combat_panel_(state, combat_sim_, panel_focus_),
      equip_panel_(state.character, panel_focus_),
      inventory_panel_(state.character, panel_focus_),
      scroll_panel_(state.character, state.scrolls),
      trace_recover_panel_(state.character),
      map_select_panel_(state),
      all_stats_panel_(state.character, state.skills),
      shop_panel_(state.character, state.equips, state.items),
      controller_(state, char_panel_, equip_panel_, inventory_panel_,
                  scroll_panel_, star_force_panel_, trace_recover_panel_,
                  sell_panel_, sell_equip_panel_, map_select_panel_,
                  shop_panel_, buy_panel_, panel_focus_) {
}

void Tui::Run() {
  equip_component_ =
      equip_panel_.MakeComponent([this]() { controller_.OpenEquipMenu(); });
  inventory_component_ = inventory_panel_.MakeComponent(
      [this]() { controller_.OpenInventoryMenu(); });
  char_component_ = char_panel_.MakeComponent(
      [this](StatField field) { controller_.OpenApAllocate(field); },
      [this](const Skill& skill) { controller_.OpenSkillLearn(skill); },
      [this](Job job) { controller_.OpenJobAdvance(job); },
      [this](const Skill& skill) { controller_.OpenSkillInspect(skill); },
      [this]() { controller_.OpenAllStats(); });
  combat_component_ =
      combat_panel_.MakeComponent([this]() { controller_.OpenMapSelect(); });

  // Order must match the Panel enum: panel_focus_ indexes this list.
  ftxui::Component panels =
      ftxui::Container::Tab({char_component_, equip_component_,
                             inventory_component_, combat_component_},
                            &panel_focus_);

  ftxui::Component base = ftxui::Renderer(
      panels, [this]() -> ftxui::Element { return RenderFrame(); });

  ftxui::ScreenInteractive screen = ftxui::ScreenInteractive::Fullscreen();
  HandleClosingSignals();

  ftxui::Component root =
      ftxui::CatchEvent(base, [this, &screen](ftxui::Event event) -> bool {
        if (event.is_mouse()) {
          return true;
        }
        // Ctrl+C leaves by the same door as the quit dialog, so it saves the
        // same way. Taken as an event rather than left to the signal handler
        // because ftxui installs its own for SIGINT once the loop is running.
        if (event == ftxui::Event::CtrlC) {
          screen.Exit();
          return true;
        }
        bool handled = OnEvent(event);
        // The controller can decide the game is over but not end it: the loop
        // is here. Checked after every event rather than only the ones it
        // consumed, so there is no key that can set the flag and not be seen.
        if (controller_.quit_requested()) {
          screen.Exit();
        }
        return handled;
      });

  // Drive the idle game: wake periodically, advance combat on the loop thread
  // (so state mutation stays single-threaded), and redraw.
  std::atomic<bool> running = true;
  std::thread ticker([this, &screen, &running]() {
    while (running) {
      // The repaint period, set by the fastest thing on screen that moves --
      // a name sliding under its column. Everything else here is driven by
      // elapsed time rather than by tick count, so waking more often costs
      // only the wakeups.
      std::this_thread::sleep_for(kMarqueeStep);
      screen.Post([this, &screen]() {
        Tick();
        AutosaveIfDue();
        // Posted here rather than acted on in the handler: this runs on the
        // loop thread, where ending the loop and writing a file are both
        // things it is safe to do.
        if (g_leaving != 0) {
          screen.Exit();
        }
      });
      screen.PostEvent(ftxui::Event::Custom);
    }
  });

  screen.Loop(root);
  running = false;
  ticker.join();
  // Every way out of the loop ends here -- the quit dialog, Ctrl+C, a closed
  // window -- so this is the one place the last save has to be written.
  Save();
}

void Tui::Save() {
  if (save_path_.empty()) {
    return;
  }
  last_save_ = std::chrono::steady_clock::now();
  if (!SaveGameToFile(state_, save_path_)) {
    LOG(ERROR) << "Could not save the game to " << save_path_;
  }
}

void Tui::AutosaveIfDue() {
  if (save_path_.empty()) {
    return;
  }
  if (std::chrono::steady_clock::now() - last_save_ < kAutosaveInterval) {
    return;
  }
  Save();
}

ftxui::Element Tui::RenderFrame() {
  // Set from the celebration every frame rather than when one starts, so the
  // panels go out on their own -- on the clock or on being visited, whichever
  // one is holding them -- and nothing has to remember to put them back.
  char_panel_.SetHighlighted(celebration_.Lights(kCharPanel));
  equip_panel_.SetHighlighted(celebration_.Lights(kEquipPanel));
  inventory_panel_.SetHighlighted(celebration_.Lights(kInventoryPanel));

  ftxui::Element frame = RenderScreen();
  if (!celebration_.card_visible()) {
    return frame;
  }
  // Over whatever the player is looking at, shop and map select included: a
  // card that only showed on the main screen would miss the player who
  // wandered off to spend their meso.
  //
  // ftxui::center shrinks it to its content on the way, which is why the card
  // sets a floor under its own width rather than leaving its size to be read
  // off the longest line in it.
  return ftxui::dbox({
      std::move(frame),
      ftxui::center(celebration_.Render() | ftxui::clear_under),
  });
}

ftxui::Element Tui::OverMain(ftxui::Element dialog) {
  return ftxui::dbox({
      RenderMain(),
      ftxui::center(std::move(dialog) | ftxui::clear_under),
  });
}

namespace {

// A window that keeps its own height. An hbox hands a bare child the full
// height of the row, and these screens are far shorter than the terminal.
ftxui::Element Standalone(ftxui::Element window) {
  return ftxui::hbox({
      ftxui::filler(),
      ftxui::vbox({std::move(window), ftxui::filler()}),
      ftxui::filler(),
  });
}

}  // namespace

ftxui::Element Tui::ApAllocDialog() {
  return ThemedWindow(
      " Allocate AP ",
      ftxui::vbox({
          CenteredRow(StatFieldName(controller_.ap_alloc_field())),
          ThemedSeparator(),
          controller_.ap_selector().Render(),
      }));
}

ftxui::Element Tui::SkillLearnDialog() {
  return ThemedWindow(" Learn Skill ",
                      ftxui::vbox({
                          CenteredRow(controller_.skill_learn_skill().name()),
                          ThemedSeparator(),
                          controller_.sp_selector().Render(),
                      }));
}

ftxui::Element Tui::JobAdvanceDialog() {
  return ThemedWindow(
      " Job Advancement ",
      ftxui::vbox({
          CenteredRow("Advance to " + JobName(controller_.job_advance_job()) +
                      "?"),
          CenteredRow("This action is irreversible."),
          ThemedSeparator(),
          CenteredRow(controller_.job_advance_prompt().Render()),
      }));
}

ftxui::Element Tui::QuitDialog() {
  // Titleless: the question is the whole dialog, and a " Quit Game " chip over
  // a "Quit Game?" row would ask it twice.
  return ThemedWindow("", ftxui::vbox({
                              CenteredRow("Quit Game?"),
                              ThemedSeparator(),
                              CenteredRow(controller_.quit_prompt().Render()),
                          }));
}

ftxui::Element Tui::RenderBuyBackInspect(const BuyBackEntry& entry) {
  if (entry.has_stack()) {
    const ItemPrototype* proto =
        FindItemByName(state_.items, entry.stack().name());
    if (proto == nullptr) {
      return ftxui::center(shop_panel_.Render());
    }
    inspect_panel_.SetItem(proto);
    return Standalone(inspect_panel_.Render());
  }
  const EquipPrototype* proto =
      FindEquipByName(state_.equips, entry.equip().equip_name());
  if (proto == nullptr) {
    return ftxui::center(shop_panel_.Render());
  }
  // Rebuilt from the state the sale kept, which is what buying it back would
  // hand over. A trace is inspected as a trace, for the same reason.
  if (entry.equip().trace()) {
    EquipTrace trace(*proto, entry.equip());
    inspect_panel_.SetItem(&trace);
    return Standalone(inspect_panel_.Render());
  }
  EquipInstance item(*proto, entry.equip());
  inspect_panel_.SetItem(&item);
  return Standalone(inspect_panel_.Render());
}

ftxui::Element Tui::RenderShopInspect() {
  // A buy-back row is an item the player owned, so what it inspects is that
  // item -- stars, scrolls and all -- and not a fresh one off the shelf.
  const BuyBackEntry* entry = shop_panel_.selected_buy_back();
  if (entry != nullptr) {
    return RenderBuyBackInspect(*entry);
  }
  // A stackable has no instance to build and nothing to preview: the panel
  // reads the prototype straight, as the bag's Etc tab does.
  const ItemPrototype* stackable = shop_panel_.selected_stackable();
  if (stackable != nullptr) {
    inspect_panel_.SetItem(stackable);
    return Standalone(inspect_panel_.Render());
  }
  const EquipPrototype* proto = shop_panel_.selected_item();
  if (proto == nullptr) {
    return ftxui::center(shop_panel_.Render());
  }
  // A pristine copy of what the shop would hand over -- no scrolls spent, no
  // stars. Built here because nothing owns a shop item until it is bought.
  EquipInstance preview(*proto);
  inspect_panel_.SetItem(&preview);
  return Standalone(inspect_panel_.Render());
}

ftxui::Element Tui::RenderTraceRecover() {
  EquipInstance preview = trace_recover_panel_.PreviewResult();
  trace_inspect_panel_.SetItem(&preview);
  int base_idx = trace_recover_panel_.selected_index();
  inspect_panel_.SetItem(base_idx >= 0 ? &state_.character.inventory()[base_idx]
                                       : nullptr);
  ftxui::Element right_col = ftxui::vbox({
      trace_recover_panel_.RenderTabs(),
      inspect_panel_.Render(),
      trace_recover_panel_.RenderBelow(),
  });
  return ftxui::hbox({trace_inspect_panel_.Render() | ftxui::flex,
                      std::move(right_col) | ftxui::flex});
}

ftxui::Element Tui::RenderInspect() {
  // One screen, two kinds of item: the panel takes whichever the cursor was on
  // and frames both the same way.
  // Two overloads of SetItem, so this cannot fold into one ternary.
  if (controller_.screen() == kItemInspect) {
    inspect_panel_.SetItem(controller_.item_inspect_item());
  } else {
    inspect_panel_.SetItem(controller_.inspect_item());
  }
  return Standalone(inspect_panel_.Render());
}

ftxui::Element Tui::RenderScroll() {
  inspect_panel_.SetItem(controller_.scroll_item());
  ftxui::Element scroll_view = scroll_panel_.Render();
  if (controller_.screen() == kScrollResult) {
    ftxui::Element dialog =
        scroll_panel_.RenderResult(controller_.scroll_result());
    scroll_view =
        ftxui::dbox({scroll_view, ftxui::center(dialog | ftxui::clear_under)});
  }
  return ftxui::hbox(
      {scroll_view | ftxui::flex, inspect_panel_.Render() | ftxui::flex});
}

ftxui::Element Tui::RenderScreen() {
  switch (controller_.screen()) {
    // Dialogs float over the main view, so what they are about stays visible
    // behind them.
    case kApAlloc:
      return OverMain(ApAllocDialog());
    case kSkillLearn:
      return OverMain(SkillLearnDialog());
    case kJobAdvance:
      return OverMain(JobAdvanceDialog());
    case kQuit:
      return OverMain(QuitDialog());
    case kSell:
      return OverMain(sell_panel_.Render());
    case kSellEquip:
      return OverMain(sell_equip_panel_.Render());
    case kMapSelect:
      return ftxui::center(map_select_panel_.Render());
    // kShopMenu draws the same thing: the menu is anchored to a row of the
    // list, so the panel puts it up itself.
    case kShop:
    case kShopMenu:
      return ftxui::center(shop_panel_.Render());
    case kShopInspect:
      return RenderShopInspect();
    case kShopBuy:
      return ftxui::dbox({
          ftxui::center(shop_panel_.Render()),
          ftxui::center(buy_panel_.Render() | ftxui::clear_under),
      });
    case kStarForce:
      star_force_panel_.SetItem(controller_.star_force_item());
      return ftxui::center(star_force_panel_.Render());
    case kStarForceResult:
      return ftxui::center(
          star_force_panel_.RenderResult(controller_.star_force_result()));
    case kTraceRecover:
      return RenderTraceRecover();
    case kTraceRecoverResult:
      return ftxui::center(trace_recover_panel_.RenderResult(
          controller_.trace_recovery_result()));
    case kAllStats:
      return Standalone(all_stats_panel_.Render());
    case kSkillInspect:
      skill_inspect_panel_.SetSkill(&controller_.skill_inspect_skill(),
                                    controller_.skill_inspect_level());
      return Standalone(skill_inspect_panel_.Render());
    case kInspect:
    case kItemInspect:
      return RenderInspect();
    case kScrollSelect:
    case kScrollResult:
      return RenderScroll();
    default:
      return RenderMain();
  }
}

ftxui::Element Tui::RenderMain() {
  // The character panel and the combat panel share the left column, and combat
  // is pinned to its foot. Without a budget the character panel takes the room
  // it wants and the mob bars are drawn off the bottom of a short terminal.
  // One row goes to the exp bar under both of them.
  int left = ftxui::Terminal::Size().dimy - 1 - combat_panel_.Height();
  char_panel_.SetMaxRows(left);
  // A panel the character has not unlocked is not drawn at all, and the layout
  // closes up around it. Rendering is skipped rather than hidden afterwards:
  // an undrawn panel has nothing to say about a game it is not part of yet.
  ftxui::Element equipped = nullptr;
  if (controller_.PanelVisible(kEquipPanel)) {
    equipped = equip_component_->Render();
  }
  ftxui::Element inventory = nullptr;
  if (controller_.PanelVisible(kInventoryPanel)) {
    inventory = inventory_component_->Render();
  }
  ftxui::Element hotkeys = nullptr;
  if (HotkeysTipVisible(state_.character)) {
    hotkeys = HotkeysPanel();
  }
  ftxui::Element layout = MainLayout(
      char_panel_.Render(), combat_component_->Render(), std::move(equipped),
      std::move(inventory), std::move(hotkeys), RenderExpBar());
  if (controller_.screen() != kItemMenu) {
    return layout;
  }
  // Asked of the panel rather than counted up from the header rows above it.
  // The old arithmetic added a fixed offset to the selected index, which stops
  // being the row on screen the moment the bag is long enough to scroll -- and
  // it had to be told the shape of both panels to do it.
  int cursor_row = 0;
  if (panel_focus_ == kEquipPanel) {
    cursor_row = equip_panel_.cursor_row();
  } else {
    cursor_row = inventory_panel_.cursor_row();
  }
  // Opened a row above the item, so the entry standing highlighted lands
  // beside the item it would act on rather than below it.
  int menu_row = std::max(0, cursor_row - 1);
  ItemMenu& menu = panel_focus_ == kEquipPanel ? equip_panel_.menu()
                                               : inventory_panel_.menu();
  // Offset past char panel border, menu cursor, name column, slot column, and
  // separators so the menu covers stats rather than item names.
  constexpr int kMenuCol =
      CharacterPanel::kTotalWidth + 1 + 2 + 18 + 2 + 10 + 2;
  // Floated so a menu opened near the foot of the bag hangs off the panel
  // rather than being cut off at the edge of the terminal.
  return ftxui::dbox({layout, Floating(menu.Render(menu_row, kMenuCol))});
}

ftxui::Element Tui::RenderExpBar() {
  const Character& p = state_.character.proto();
  std::string label;
  float frac;
  if (p.level() >= kTrialLevelCap) {
    // Full rather than empty. There is no next level to fill towards, and a
    // bar sitting at 0% reads like the EXP was taken away.
    label = "MAX";
    frac = 1.0f;
  } else {
    int64_t exp = p.exp();
    int64_t tnl = ExpToNextLevel(p.level());
    frac = tnl > 0 ? static_cast<float>(exp) / static_cast<float>(tnl) : 0.0f;
    double pct =
        tnl > 0 ? static_cast<double>(exp) * 100.0 / static_cast<double>(tnl)
                : 0.0;
    char buf[16];
    snprintf(buf, sizeof(buf), "%.*f%%", ExpPctDecimals(p.level()), pct);
    label = FormatWithCommas(exp) + " (" + buf + ")";
  }
  return ProgressBar(frac, kTheme, label);
}

void Tui::Tick() {
  std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
  std::chrono::duration<double> elapsed = now - last_combat_update_;
  last_combat_update_ = now;
  // Every tick, rather than only at save time: the total then stays true
  // between saves, which is what anything wanting to show it will read.
  state_.playtime_seconds += elapsed.count();
  AdvanceCombat(state_, combat_sim_, elapsed.count());
  // Ticked down before the new level is noticed, so a level-up landing on this
  // tick gets its full four seconds rather than one tick's worth less.
  celebration_.Advance(elapsed.count());
  celebration_.Visit(FocusedPanel());
  NoticeProgress();
  // Last, so that dying wins the card over anything else this tick turned up.
  // A level earned on the way down is still a level, and its gold is still
  // lit -- but what the player needs told is that they are no longer where
  // they thought they were.
  if (combat_sim_.died_this_step()) {
    celebration_.BeginDeath();
  }
}

Panel Tui::FocusedPanel() const {
  if (controller_.screen() != kMain) {
    return kNoPanel;
  }
  return static_cast<Panel>(panel_focus_);
}

void Tui::NoticeProgress() {
  const Character& character = state_.character.proto();
  // The advancement is checked first and wins: it is the larger news, and
  // reaching the level that offers one does not itself take it, so the two
  // cannot be describing the same moment.
  if (character.job() != last_job_seen_) {
    celebration_.BeginAdvancement(last_job_seen_, character.job(),
                                  FocusedPanel());
    last_job_seen_ = character.job();
    last_level_seen_ = character.level();
    return;
  }
  if (character.level() > last_level_seen_) {
    LevelGains gains = GainsForLevels(last_level_seen_, character.level());
    // A Beginner's SP is real but unreachable -- the skills tab belongs to a
    // job -- so the card does not mention what they cannot go and spend.
    int sp = character.job() == JOB_BEGINNER ? 0 : gains.sp;
    celebration_.BeginLevelUp(last_level_seen_, character.level(), gains.ap, sp,
                              FocusedPanel());
  }
  // Assigned rather than only raised, so a level that somehow went down does
  // not leave the next real level-up reporting a climb it did not make.
  last_level_seen_ = character.level();
}

bool Tui::OnEvent(ftxui::Event event) {
  // A player who has looked is done with it. The key still does whatever it
  // normally does -- getting rid of the card is a side effect, not a key the
  // celebration swallows, so nothing the player meant to do is lost. Custom is
  // the ticker's own redraw and is not somebody looking.
  if (celebration_.card_visible() && event != ftxui::Event::Custom) {
    celebration_.Dismiss();
  }
  bool handled = controller_.OnEvent(event);
  // After the event rather than before it: the key that just landed may be the
  // Tab that walked the player onto a panel waiting to be visited, and its gold
  // should be gone in the frame this event draws rather than the one after.
  celebration_.Visit(FocusedPanel());
  // Advancement happens here rather than in the tick, and so does the debug
  // Level-Up item.
  NoticeProgress();
  return handled;
}

}  // namespace ms
