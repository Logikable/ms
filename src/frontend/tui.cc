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
      last_save_(std::chrono::steady_clock::now()),
      last_combat_update_(std::chrono::steady_clock::now()),
      char_panel_(state.character, panel_focus_, state.skills),
      combat_panel_(state, combat_sim_, panel_focus_),
      equip_panel_(state.character, panel_focus_),
      inventory_panel_(state.character, panel_focus_),
      scroll_panel_(state.scrolls),
      trace_recover_panel_(state.character),
      map_select_panel_(state),
      shop_panel_(state.character, state.equips),
      controller_(state, equip_panel_, inventory_panel_, scroll_panel_,
                  star_force_panel_, trace_recover_panel_, sell_panel_,
                  map_select_panel_, shop_panel_, buy_panel_, panel_focus_) {
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
      [this](const Skill& skill) { controller_.OpenSkillInspect(skill); });
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
      std::this_thread::sleep_for(std::chrono::milliseconds(300));
      screen.Post([this, &screen]() {
        AdvanceCombatTick();
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
  if (controller_.screen() == kApAlloc) {
    // Float the AP amount entry over the main view so the stat being raised
    // stays visible behind it.
    ftxui::Element dialog = ThemedWindow(
        " Allocate AP ",
        ftxui::vbox({
            CenteredRow(StatFieldName(controller_.ap_alloc_field())),
            ThemedSeparator(),
            controller_.ap_selector().Render(),
        }));
    return ftxui::dbox({
        RenderMain(),
        ftxui::center(dialog | ftxui::clear_under),
    });
  }
  if (controller_.screen() == kSkillLearn) {
    // Float the skill-learning amount entry over the main view so the skill
    // being raised stays visible behind it.
    ftxui::Element dialog =
        ThemedWindow(" Learn Skill ",
                     ftxui::vbox({
                         CenteredRow(controller_.skill_learn_skill().name()),
                         ThemedSeparator(),
                         controller_.sp_selector().Render(),
                     }));
    return ftxui::dbox({
        RenderMain(),
        ftxui::center(dialog | ftxui::clear_under),
    });
  }
  if (controller_.screen() == kJobAdvance) {
    // Float the confirmation over the main view, so the job list the choice
    // came from stays behind it. A blank row separates the warning from the
    // buttons, as on every other confirmation.
    ftxui::Element dialog = ThemedWindow(
        " Job Advancement ",
        ftxui::vbox({
            CenteredRow("Advance to " + JobName(controller_.job_advance_job()) +
                        "?"),
            CenteredRow("This action is irreversible."),
            ThemedSeparator(),
            CenteredRow(controller_.job_advance_prompt().Render()),
        }));
    return ftxui::dbox({
        RenderMain(),
        ftxui::center(dialog | ftxui::clear_under),
    });
  }
  if (controller_.screen() == kQuit) {
    // Titleless, like the bare confirm prompt: the question is the whole
    // dialog, and a " Quit Game " chip above a "Quit Game?" row would ask it
    // twice. Floated over the main view so the game the player is leaving is
    // still behind the question.
    ftxui::Element dialog =
        ThemedWindow("", ftxui::vbox({
                             CenteredRow("Quit Game?"),
                             ThemedSeparator(),
                             CenteredRow(controller_.quit_prompt().Render()),
                         }));
    return ftxui::dbox({
        RenderMain(),
        ftxui::center(dialog | ftxui::clear_under),
    });
  }
  if (controller_.screen() == kSell) {
    // Float the sell dialog over the main view for context.
    return ftxui::dbox({
        RenderMain(),
        ftxui::center(sell_panel_.Render() | ftxui::clear_under),
    });
  }
  if (controller_.screen() == kMapSelect) {
    return ftxui::center(map_select_panel_.Render());
  }
  // kShopMenu draws the same thing: the menu is anchored to a row of the list,
  // so the panel puts it up itself. It floats, so it may hang below the shop's
  // bottom border without the centring here moving.
  if (controller_.screen() == kShop || controller_.screen() == kShopMenu) {
    return ftxui::center(shop_panel_.Render());
  }
  if (controller_.screen() == kShopInspect) {
    const EquipPrototype* proto = shop_panel_.selected_item();
    if (proto == nullptr) {
      return ftxui::center(shop_panel_.Render());
    }
    // A pristine copy of what the shop would hand over -- no scrolls spent, no
    // stars. Built here rather than held anywhere, because nothing owns a shop
    // item until someone buys it.
    EquipInstance preview(*proto);
    inspect_panel_.SetItem(&preview);
    // A screen of its own, like inspecting something already in the bag. The
    // details are what the player came to read, so nothing sits behind them.
    // Over a filler, not bare: an hbox hands its child the full height of the
    // row, so the window would stretch to the terminal rather than fit what
    // it holds.
    return ftxui::hbox({
        ftxui::filler(),
        ftxui::vbox({inspect_panel_.Render(), ftxui::filler()}),
        ftxui::filler(),
    });
  }
  if (controller_.screen() == kShopBuy) {
    // Float the buy dialog over the shop, so the list it came from stays
    // behind it -- the same way selling floats over the bag.
    return ftxui::dbox({
        ftxui::center(shop_panel_.Render()),
        ftxui::center(buy_panel_.Render() | ftxui::clear_under),
    });
  }
  if (controller_.screen() == kStarForce) {
    star_force_panel_.SetItem(controller_.star_force_item());
    return ftxui::center(star_force_panel_.Render());
  }
  if (controller_.screen() == kStarForceResult) {
    return ftxui::center(
        star_force_panel_.RenderResult(controller_.star_force_result()));
  }
  if (controller_.screen() == kTraceRecover) {
    EquipInstance preview = trace_recover_panel_.PreviewResult();
    trace_inspect_panel_.SetItem(&preview);
    int base_idx = trace_recover_panel_.selected_index();
    inspect_panel_.SetItem(
        base_idx >= 0 ? &state_.character.inventory()[base_idx] : nullptr);
    ftxui::Element right_col = ftxui::vbox({
        trace_recover_panel_.RenderTabs(),
        inspect_panel_.Render(),
        trace_recover_panel_.RenderBelow(),
    });
    return ftxui::hbox({trace_inspect_panel_.Render() | ftxui::flex,
                        std::move(right_col) | ftxui::flex});
  }
  if (controller_.screen() == kTraceRecoverResult) {
    return ftxui::center(
        trace_recover_panel_.RenderResult(controller_.trace_recovery_result()));
  }
  if (controller_.screen() == kSkillInspect) {
    skill_inspect_panel_.SetSkill(&controller_.skill_inspect_skill(),
                                  controller_.skill_inspect_level());
    // Over a filler so the window keeps its own height; an hbox stretches a
    // bare child to the row height, and this screen is shorter than the
    // terminal by a long way.
    return ftxui::hbox({
        ftxui::filler(),
        ftxui::vbox({skill_inspect_panel_.Render(), ftxui::filler()}),
        ftxui::filler(),
    });
  }
  if (controller_.screen() == kInspect ||
      controller_.screen() == kItemInspect) {
    // One screen, two kinds of item: the panel takes whichever the cursor was
    // on and frames both the same way.
    if (controller_.screen() == kItemInspect) {
      inspect_panel_.SetItem(controller_.item_inspect_item());
    } else {
      inspect_panel_.SetItem(controller_.inspect_item());
    }
    // Over a filler, not bare: an hbox hands its child the full height of the
    // row, which for a stackable's three rows is a window of mostly nothing.
    return ftxui::hbox({
        ftxui::filler(),
        ftxui::vbox({inspect_panel_.Render(), ftxui::filler()}),
        ftxui::filler(),
    });
  }
  if (controller_.screen() == kScrollSelect ||
      controller_.screen() == kScrollResult) {
    inspect_panel_.SetItem(controller_.scroll_item());
    ftxui::Element scroll_view = scroll_panel_.Render();
    if (controller_.screen() == kScrollResult) {
      ftxui::Element dialog =
          scroll_panel_.RenderResult(controller_.scroll_result());
      scroll_view = ftxui::dbox(
          {scroll_view, ftxui::center(dialog | ftxui::clear_under)});
    }
    return ftxui::hbox(
        {scroll_view | ftxui::flex, inspect_panel_.Render() | ftxui::flex});
  }
  return RenderMain();
}

ftxui::Element Tui::RenderMain() {
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

void Tui::AdvanceCombatTick() {
  std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
  std::chrono::duration<double> elapsed = now - last_combat_update_;
  last_combat_update_ = now;
  AdvanceCombat(state_, combat_sim_, elapsed.count());
}

bool Tui::OnEvent(ftxui::Event event) {
  return controller_.OnEvent(event);
}

}  // namespace ms
