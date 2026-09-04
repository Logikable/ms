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
#include <vector>

#include "absl/log/log.h"
#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/terminal.hpp"
#include "src/character/character.h"
#include "src/character/consumables.h"
#include "src/character/exp_table.h"
#include "src/character/honor.h"
#include "src/character/job_name.h"
#include "src/character/progression.h"
#include "src/combat/boss_timing.h"
#include "src/combat/combat.h"
#include "src/frontend/keybinds.h"
#include "src/frontend/main_layout.h"
#include "src/frontend/panels/character_panel.h"
#include "src/frontend/panels/combat_panel.h"
#include "src/frontend/panels/equipped_panel.h"
#include "src/frontend/panels/hotkeys_panel.h"
#include "src/frontend/panels/inventory_panel.h"
#include "src/frontend/panels/offline_popup_panel.h"
#include "src/frontend/screens/boss_clear_panel.h"
#include "src/frontend/screens/boss_fight_panel.h"
#include "src/frontend/screens/map_select_panel.h"
#include "src/frontend/screens/scroll_panel.h"
#include "src/frontend/tui_controller.h"
#include "src/frontend/widgets/amount_selector.h"
#include "src/frontend/widgets/chrome.h"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/format.h"
#include "src/frontend/widgets/game_names.h"
#include "src/frontend/widgets/item_menu.h"
#include "src/frontend/widgets/marquee.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
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

// Every kill the step recorded, whatever stood on the map.
int64_t TotalKills(const CombatSim& sim) {
  int64_t total = 0;
  for (int64_t kills : sim.view().kills_this_step) {
    total += kills;
  }
  return total;
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

// The host and port in `server`, which is "host:port". Nothing for an empty
// one, or for a port that is not a number.
std::unique_ptr<MultiplayerSession> MakeSession(const std::string& server) {
  size_t colon = server.rfind(':');
  if (colon == std::string::npos) {
    return nullptr;
  }
  int port = std::atoi(server.c_str() + colon + 1);
  if (port <= 0) {
    return nullptr;
  }
  return std::make_unique<MultiplayerSession>(server.substr(0, colon), port);
}

}  // namespace

Tui::Tui(GameState& state, std::string save_path, std::string server)
    : state_(state),
      save_policy_(std::move(save_path), std::chrono::steady_clock::now()),
      multiplayer_(MakeSession(server)),
      progress_watcher_(state.character.proto()),
      last_combat_update_(std::chrono::steady_clock::now()),
      keys_(state.account.mutable_keybinds()),
      char_panel_(state.character, state.account, panel_focus_, state.skills),
      combat_panel_(state, combat_sim_, panel_focus_),
      menu_panel_(state, analysis_, panel_focus_),
      analysis_panel_(state, analysis_),
      equip_panel_(state.character, state.account, panel_focus_),
      inventory_panel_(state.character, state.account, panel_focus_),
      scroll_panel_(state.character, state.scrolls),
      trace_recover_panel_(state.character),
      map_select_panel_(state),
      mob_inspect_panel_(state),
      boss_select_panel_(state),
      party_select_panel_(),
      job_inspect_panel_(state.skills),
      keybinds_panel_(keys_),
      options_panel_(state.account),
      all_stats_panel_(state.character, &state.account, state.skills),
      party_inspect_panel_(state),
      multi_sell_panel_(state.character, state.account),
      shop_panel_(state.character, state.equips, state.items),
      controller_(
          state,
          Screens{
              char_panel_,         equip_panel_,         inventory_panel_,
              scroll_panel_,       inspect_panel_,       preview_inspect_panel_,
              star_force_panel_,   cube_panel_,          trace_recover_panel_,
              sell_panel_,         sell_equip_panel_,    multi_sell_panel_,
              map_select_panel_,   mob_inspect_panel_,   boss_select_panel_,
              party_select_panel_, party_inspect_panel_, shop_panel_,
              buy_panel_,          job_inspect_panel_,   skill_inspect_panel_,
              pot_info_panel_,     menu_panel_,          keybinds_panel_,
              options_panel_},
          analysis_, keys_, panel_focus_, multiplayer_.get()) {
  // Both inspect panels read the character, not just the item: a piece of a
  // set is described beside the set it belongs to, and which of its tiers are
  // being paid depends on what is worn.
  inspect_panel_.UseCharacter(state.character);
  preview_inspect_panel_.UseCharacter(state.character);
  party_item_panel_.UseCharacter(party_inspect_panel_.character());
}

void Tui::BuildComponents() {
  equip_component_ = equip_panel_.MakeComponent(
      [this]() { controller_.OpenEquipMenu(); },
      [this]() { controller_.ToggleExpanded(kEquipPanel); });
  inventory_component_ = inventory_panel_.MakeComponent(
      [this]() { controller_.OpenInventoryMenu(); },
      [this]() { controller_.ToggleExpanded(kInventoryPanel); });
  CharacterPanelActions char_actions;
  char_actions.allocate = [this](StatField field) {
    controller_.OpenApAllocate(field);
  };
  char_actions.all_stats = [this]() {
    // The screen opens on whichever allocation the panel behind it is showing,
    // so one Enter never changes the numbers.
    all_stats_panel_.SetPreset(char_panel_.hyper_preset());
    controller_.OpenAllStats();
  };
  char_actions.learn = [this](const Skill& skill) {
    controller_.OpenSkillLearn(skill);
  };
  char_actions.menu = [this](const Skill& skill) {
    controller_.OpenSkillMenu(skill);
  };
  char_actions.advance = [this](Job job) { controller_.OpenJobMenu(job); };
  char_actions.hyper_allocate = [this](HyperStatField field) {
    controller_.OpenHyperAllocate(field, char_panel_.hyper_preset());
  };
  char_actions.hyper_reset = [this]() {
    controller_.OpenHyperReset(char_panel_.hyper_preset());
  };
  char_actions.hyper_inspect = [this](HyperStatField field) {
    controller_.OpenHyperStatInspect(field, char_panel_.hyper_preset());
  };
  char_actions.ability_lock = [this](int index) {
    controller_.ToggleAbilityLock(index, char_panel_.hyper_preset());
  };
  char_actions.ability_reroll = [this]() {
    controller_.OpenAbilityReroll(char_panel_.hyper_preset());
  };
  char_actions.pot_menu = [this](ConsumableType type) {
    controller_.OpenPotMenu(type);
  };
  char_component_ = char_panel_.MakeComponent(std::move(char_actions));
  combat_component_ =
      combat_panel_.MakeComponent([this]() { controller_.OpenMapSelect(); });
  menu_component_ = menu_panel_.MakeComponent(
      [this](MenuEntry entry) { controller_.OpenMenuEntry(entry); });
}

ftxui::Component Tui::MakeRoot(ftxui::ScreenInteractive& screen) {
  // Order must match the Panel enum: panel_focus_ indexes this list.
  ftxui::Component panels = ftxui::Container::Tab(
      {char_component_, equip_component_, inventory_component_, menu_component_,
       combat_component_},
      &panel_focus_);

  ftxui::Component base = ftxui::Renderer(
      panels, [this]() -> ftxui::Element { return RenderFrame(); });

  ftxui::Component handler =
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
  // Outside everything, so the whole tree -- ftxui's own menus included --
  // hears the game's keys rather than the terminal's.
  return TranslateKeys(handler, keys_,
                       [this]() { return controller_.capturing_key(); });
}

void Tui::Run() {
  BuildComponents();
  if (multiplayer_ != nullptr) {
    multiplayer_->Start(state_);
  }
  ftxui::ScreenInteractive screen = ftxui::ScreenInteractive::Fullscreen();
  HandleClosingSignals();
  ftxui::Component root = MakeRoot(screen);

  // Drive the idle game: wake periodically, advance combat on the loop thread
  // (so state mutation stays single-threaded), and redraw.
  std::atomic<bool> running = true;
  std::thread ticker([this, &screen, &running]() {
    while (running) {
      // The repaint period, set by the fastest thing on screen that moves: a
      // name sliding under its column out here, a swing charging in a boss
      // fight, which is quicker than any name. Everything is driven by elapsed
      // time rather than by tick count, so waking more often costs only the
      // wakeups.
      std::this_thread::sleep_for(in_boss_fight_ ? kBossFightStep
                                                 : kMarqueeStep);
      screen.Post([this, &screen]() {
        Tick();
        save_policy_.AutosaveIfDue(state_, std::chrono::steady_clock::now());
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
  // Before the save, so that an account the server issued this session is in
  // the file the player comes back to.
  if (multiplayer_ != nullptr) {
    multiplayer_->Advance(state_);
    multiplayer_->Stop();
  }
  // Every way out of the loop ends here -- the quit dialog, Ctrl+C, a closed
  // window -- so this is the one place the last save has to be written.
  save_policy_.Save(state_, std::chrono::steady_clock::now());
}

ftxui::Element Tui::RenderFrame() {
  // Set from the celebration every frame rather than when one starts, so the
  // panels go out on their own -- on the clock or on being visited, whichever
  // one is holding them -- and nothing has to remember to put them back.
  // The ability rank up rides the same gold: it lands on the panel the player
  // is already looking at, and goes out on their next key rather than a clock.
  char_panel_.SetHighlighted(celebration_.Lights(kCharPanel) ||
                             controller_.ability_rank_up());
  equip_panel_.SetHighlighted(celebration_.Lights(kEquipPanel));
  inventory_panel_.SetHighlighted(celebration_.Lights(kInventoryPanel));
  equip_panel_.SetExpanded(controller_.expanded_panel() == kEquipPanel);
  inventory_panel_.SetExpanded(controller_.expanded_panel() == kInventoryPanel);

  ftxui::Element frame = RenderScreen();
  if (controller_.party_notice_prompt().open()) {
    // Over whatever the player is looking at: the server does not wait for
    // them to be on the party screen before removing them from a party.
    frame = ftxui::dbox({
        std::move(frame),
        ftxui::center(ClearUnder(PartyNoticeDialog())),
    });
  }
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
      ftxui::center(ClearUnder(celebration_.Render())),
  });
}

ftxui::Element Tui::OverMain(ftxui::Element dialog) {
  return ftxui::dbox({
      RenderMain(),
      ftxui::center(ClearUnder(std::move(dialog))),
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
  return DialogWindow(
      " Job Advancement ",
      {
          CenteredRow("Advance to " +
                      AdvancementName(controller_.job_advance_job(),
                                      controller_.job_advance_stage()) +
                      "?"),
          CenteredRow("This action is irreversible."),
      },
      controller_.job_advance_prompt().Render());
}

ftxui::Element Tui::HyperResetDialog() {
  // Titleless, like the quit dialog: the question is the whole dialog.
  return DialogWindow("", {CenteredRow(controller_.hyper_reset_question())},
                      controller_.hyper_reset_prompt().Render());
}

ftxui::Element Tui::AbilityRerollDialog() {
  // AccentSeparator, not ftxui::separator: a dialog's content is drawn white,
  // so a plain rule inside the body comes out white against the theme-blue one
  // DialogWindow draws over the buttons.
  std::vector<ftxui::Element> body = {CenteredRow("Reroll these lines?"),
                                      AccentSeparator(kTheme)};
  for (const AbilityLine& line : controller_.ability_reroll_lines()) {
    body.push_back(CenteredRow(ftxui::text(AbilityLineName(line.type()) + " " +
                                           AbilityLineValueText(line)) |
                               ftxui::color(RarityColor(line.rank()))));
  }
  return DialogWindow("", std::move(body),
                      controller_.ability_reroll_prompt().Render());
}

ftxui::Element Tui::PotBuyDialog() {
  const ConsumableInfo* info = ConsumableInfoFor(controller_.pot_type());
  const bool affordable = controller_.pot_buy_affordable();
  // Three short rows rather than one long one: the pot's name is what the
  // player is deciding about, and the price is what they are weighing.
  return DialogWindow(
      "",
      {
          CenteredRow(info == nullptr ? "" : "Buy " + std::string(info->name)),
          CenteredRow("permanently for"),
          CenteredRow(
              RedUnless(ftxui::text(FormatMeso(controller_.pot_buy_price())),
                        affordable)),
      },
      ConfirmButtons(controller_.pot_buy_prompt().focus(), affordable));
}

ftxui::Element Tui::QuitDialog() {
  // Titleless: the question is the whole dialog, and a " Quit Game " chip over
  // a "Quit Game?" row would ask it twice.
  return DialogWindow("", {CenteredRow("Quit Game?")},
                      controller_.quit_prompt().Render());
}

ftxui::Element Tui::RenderMenuBox() {
  // The exp bar, which the corner menu sits one row above.
  constexpr int kExpBarRows = 1;
  return ftxui::dbox({
      RenderMain(),
      ftxui::vbox({
          ftxui::filler(),
          ftxui::hbox({ftxui::filler(), menu_panel_.RenderBox()}),
          ftxui::filler() | ftxui::size(ftxui::HEIGHT, ftxui::EQUAL,
                                        MenuPanel::kHeight + kExpBarRows),
      }),
  });
}

ftxui::Element Tui::PartyConfirmDialog() {
  // Titleless, like the quit dialog: the question is the whole dialog.
  return DialogWindow("", {CenteredRow(controller_.party_prompt_question())},
                      controller_.party_prompt().Render());
}

ftxui::Element Tui::PartyNoticeDialog() {
  // Red when the server would not do something or the connection has gone,
  // theme blue for a party that changed under the player.
  ftxui::Color accent = controller_.party_notice_is_refusal() ? kRed : kTheme;
  return DialogWindow("", {CenteredRow(controller_.party_notice())},
                      controller_.party_notice_prompt().Render("Close"),
                      accent);
}

ftxui::Element Tui::RenderParty() {
  // kPartyMenu draws the same thing: the menu is anchored to a row of the
  // list, so the panel puts it up itself.
  ftxui::Element screen = ftxui::center(party_select_panel_.Render());
  if (controller_.screen() != kPartyConfirm) {
    return screen;
  }
  return ftxui::dbox({
      std::move(screen),
      ftxui::center(ClearUnder(PartyConfirmDialog())),
  });
}

ftxui::Element Tui::RenderPartyInspect() {
  // The item gets a screen of its own, the way the player's own items do: the
  // sheet it came off is a screen already, and a card over it was two screens
  // to read at once.
  if (controller_.screen() == kPartyItemInspect) {
    party_item_panel_.SetItem(party_inspect_panel_.selected_item());
    return Standalone(party_item_panel_.Render());
  }
  party_inspect_panel_.SetMaxRows(ftxui::Terminal::Size().dimy);
  return Standalone(party_inspect_panel_.Render());
}

ftxui::Element Tui::BossConfirmDialog() {
  // Titleless, like the quit dialog: the question is the whole dialog.
  return DialogWindow(
      "", {CenteredRow("Fight " + controller_.boss_prompt_title() + "?")},
      controller_.boss_prompt().Render());
}

ftxui::Element Tui::NoticeDialog() {
  // Red when the player is the reason -- nothing to swing with, an item that
  // will take no more hammers -- and theme blue when it is only a clock. A
  // refusal is a dead end, so its button closes rather than continuing.
  bool refused = controller_.notice_is_refusal();
  ftxui::Elements rows;
  for (const std::string& line : controller_.notice_lines()) {
    rows.push_back(CenteredRow(line));
  }
  return DialogWindow(
      "", std::move(rows),
      controller_.notice_prompt().Render(refused ? "Close" : "Continue"),
      refused ? kRed : kTheme);
}

void Tui::ShowOfflineReport(OfflineReport report) {
  controller_.OpenOfflineReport(std::move(report));
}

ftxui::Element Tui::BossAbortDialog() {
  const BossRun* run = controller_.boss_run();
  return DialogWindow(
      "",
      {CenteredRow("Stop fighting " +
                   (run == nullptr ? std::string("") : run->title()) + "?")},
      controller_.boss_abort_prompt().Render());
}

// What stands over the arena, if anything: the leave prompt, or whatever the
// fight ended on. Null while the fight is still being fought.
ftxui::Element Tui::BossFightOverlay() {
  switch (controller_.screen()) {
    case kBossAbort:
      return BossAbortDialog();
    case kBossNotice:
      return NoticeDialog();
    case kBossClear:
      return BossClearPanel(controller_.boss_clear_title(),
                            controller_.boss_clear_seconds(),
                            controller_.boss_clear_reward(),
                            controller_.boss_clear_prompt().Render(),
                            HonorVisible(state_.character.proto().level(),
                                         state_.account.max_level()));
    default:
      return nullptr;
  }
}

ftxui::Element Tui::RenderBossFight() {
  const BossRun* run = controller_.boss_run();
  if (run == nullptr) {
    return ftxui::center(boss_select_panel_.Render());
  }
  // Whatever the fight ended on stands over the arena, so the player sees the
  // fight they just finished rather than the list they are going back to.
  ftxui::Element fight = BossFightPanel(*run);
  ftxui::Element overlay = BossFightOverlay();
  if (overlay == nullptr) {
    return fight;
  }
  return ftxui::dbox({
      std::move(fight),
      ftxui::center(ClearUnder(std::move(overlay))),
  });
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
  inspect_panel_.SetMaxRows(ftxui::Terminal::Size().dimy);
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

// The job's book on the left and whichever skill the cursor is on to the
// right, previewed at both ends of its levels: the player has spent no points
// on it and has none to spend, so "one more point" would say nothing.
ftxui::Element Tui::RenderJobInspect() {
  // One size for the whole book: the card holds it whichever skill the cursor
  // is on, so the screen does not shift under the reader.
  PreviewCardSize card =
      LargestPreviewCard(job_inspect_panel_.Skills(),
                         ftxui::Terminal::Size().dimx - kJobInspectBookWidth);
  skill_inspect_panel_.SetSkill(job_inspect_panel_.selected_skill(), 0, 0,
                                SkillInspectPanel::kPreview);
  skill_inspect_panel_.SetWidthBounds(card.columns, card.columns);
  return Standalone(JobInspectScreen(job_inspect_panel_.Render(),
                                     skill_inspect_panel_.Render(), card.rows));
}

ftxui::Element Tui::RenderTraceRecover() {
  EquipInstance preview = trace_recover_panel_.PreviewResult();
  preview_inspect_panel_.SetItem(&preview);
  int base_idx = trace_recover_panel_.selected_index();
  inspect_panel_.SetItem(base_idx >= 0 ? &state_.character.inventory()[base_idx]
                                       : nullptr);
  int rows = ftxui::Terminal::Size().dimy;
  preview_inspect_panel_.SetMaxRows(rows);
  // The right card shares its column with the chip row above it and the
  // confirm bar below, so it gets what those two leave.
  inspect_panel_.SetMaxRows(rows - kRecoverTabRows -
                            ConfirmPrompt::kWindowHeight);
  bool right = controller_.right_card_focused();
  ftxui::Element right_col = ftxui::vbox({
      trace_recover_panel_.RenderTabs(),
      inspect_panel_.RenderItemOnly(right),
      trace_recover_panel_.RenderBelow(),
  });
  return ftxui::hbox(
      {preview_inspect_panel_.RenderItemOnly(!right) | ftxui::flex,
       std::move(right_col) | ftxui::flex});
}

// The item as it stands, the panel, and the item one star on. At max stars
// there is no after: the panel says so, and a second card would say it twice.
ftxui::Element Tui::RenderStarForce() {
  const EquipInstance* item = controller_.star_force_item();
  star_force_before_.reset();
  star_force_after_.reset();
  if (item != nullptr) {
    star_force_before_.emplace(item->prototype(), item->equip_state());
    if (item->stars() < item->max_stars()) {
      Equip next = item->equip_state();
      next.set_stars(item->stars() + 1);
      star_force_after_.emplace(item->prototype(), next);
    }
  }
  // The panel reads the cached copy rather than the bag's item, so the result
  // screen below can draw the same panel after the item is destroyed.
  star_force_panel_.SetItem(
      star_force_before_.has_value() ? &*star_force_before_ : nullptr,
      state_.character.meso());
  return StarForceColumns();
}

// The result window over the cards the attempt was made on. The item may be
// gone by now, which is why the columns come from the cache.
ftxui::Element Tui::RenderStarForceResult() {
  ftxui::Element result =
      star_force_panel_.RenderResult(controller_.star_force_result());
  return ftxui::dbox({
      StarForceColumns(),
      ftxui::center(ClearUnder(std::move(result))),
  });
}

ftxui::Element Tui::StarForceColumns() {
  ftxui::Element panel = star_force_panel_.Render();
  if (!star_force_before_.has_value()) {
    return ftxui::center(std::move(panel));
  }
  int rows = ftxui::Terminal::Size().dimy;
  bool right = controller_.right_card_focused();
  bool two_cards = star_force_after_.has_value();
  inspect_panel_.SetItem(&*star_force_before_);
  inspect_panel_.SetMaxRows(rows);
  // Each card takes the width it asks for and no more, and the slack goes to
  // the two ends: what the player compares is the three of them shoulder to
  // shoulder, not a card pushed out to the edge of the terminal. They hold
  // one item a star apart, so they are titled for which side of the attempt
  // they are rather than both saying Inspect.
  std::vector<ftxui::Element> columns;
  columns.push_back(ftxui::filler());
  columns.push_back(
      inspect_panel_.RenderItemOnly(two_cards && !right, " Before ") |
      ftxui::yflex);
  columns.push_back(std::move(panel));
  if (two_cards) {
    preview_inspect_panel_.SetItem(&*star_force_after_);
    preview_inspect_panel_.SetMaxRows(rows);
    columns.push_back(preview_inspect_panel_.RenderItemOnly(right, " After ") |
                      ftxui::yflex);
  }
  columns.push_back(ftxui::filler());
  return ftxui::hbox(std::move(columns));
}

// The shelf and the item's card, shoulder to shoulder in the middle of the
// screen: two columns of about a width, neither run out to the edge. The
// question, when one is open, is centred over both -- it is asked about the
// cube on the left and the lines on the right at once.
ftxui::Element Tui::RenderCubing() {
  const EquipInstance* item = controller_.cube_item();
  cube_panel_.SetItem(item, state_.character.meso());
  inspect_panel_.SetItem(item);
  inspect_panel_.SetMaxRows(ftxui::Terminal::Size().dimy);
  bool right = controller_.right_card_focused();
  ftxui::Element columns = ftxui::hbox({
      ftxui::filler(),
      cube_panel_.Render(!right) | ftxui::vcenter,
      ftxui::text(" "),
      inspect_panel_.RenderItemOnly(right) | ftxui::vcenter,
      ftxui::filler(),
  });
  if (!cube_panel_.IsConfirming()) {
    return columns;
  }
  return ftxui::dbox({
      std::move(columns),
      ftxui::center(ClearUnder(cube_panel_.RenderConfirm())),
  });
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
  inspect_panel_.SetMaxRows(ftxui::Terminal::Size().dimy);
  return Standalone(inspect_panel_.Render());
}

ftxui::Element Tui::RenderScroll() {
  inspect_panel_.SetItem(controller_.scroll_item());
  inspect_panel_.SetMaxRows(ftxui::Terminal::Size().dimy);
  ftxui::Element scroll_view =
      scroll_panel_.Render(!controller_.right_card_focused());
  if (controller_.screen() == kScrollResult) {
    ftxui::Element dialog =
        scroll_panel_.RenderResult(controller_.scroll_result());
    scroll_view = ftxui::dbox({scroll_view, ftxui::center(ClearUnder(dialog))});
  }
  return ftxui::hbox(
      {scroll_view | ftxui::flex,
       inspect_panel_.RenderItemOnly(controller_.right_card_focused()) |
           ftxui::flex});
}

ftxui::Element Tui::RenderMultiSell() {
  ftxui::Element screen = ftxui::center(multi_sell_panel_.Render());
  if (!multi_sell_panel_.confirming()) {
    return screen;
  }
  return ftxui::dbox({
      std::move(screen),
      ftxui::center(ClearUnder(multi_sell_panel_.RenderConfirm())),
  });
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
    case kOffline:
      return OverMain(OfflinePopupPanel(
          controller_.offline_report(), controller_.offline_prompt().Render(),
          HonorVisible(state_.character.proto().level(),
                       state_.account.max_level())));
    case kSell:
      return OverMain(sell_panel_.Render());
    case kSellEquip:
      return OverMain(sell_equip_panel_.Render());
    case kSymbolLevel:
      return OverMain(controller_.symbol_level_panel().Render());
    case kSymbolCombine:
      return OverMain(controller_.symbol_combine_panel().Render());
    case kHammer:
      return OverMain(controller_.hammer_panel().Render());
    case kHammerNotice:
      return OverMain(NoticeDialog());
    // The dialog is the panel's own, so the screen it belongs to is one state.
    case kMultiSell:
      return RenderMultiSell();
    // kMapMenu draws the same thing: the menu is anchored to a row of the
    // list, so the panel puts it up itself.
    case kMapSelect:
    case kMapMenu:
      return ftxui::center(map_select_panel_.Render());
    case kMobInspect:
      return ftxui::center(mob_inspect_panel_.Render());
    case kMenuBox:
      return RenderMenuBox();
    case kAnalysis:
      return OverMain(analysis_panel_.Render());
    case kKeybinds:
      return ftxui::center(keybinds_panel_.Render());
    case kOptions:
      return ftxui::center(options_panel_.Render());
    case kPartySelect:
    case kPartyMenu:
    case kPartyConfirm:
      return RenderParty();
    case kPartyInspect:
    case kPartyItemInspect:
      return RenderPartyInspect();
    case kBossSelect:
      return ftxui::center(boss_select_panel_.Render());
    case kBossFight:
    case kBossAbort:
    case kBossClear:
      return RenderBossFight();
    case kBossConfirm:
      return ftxui::dbox({
          ftxui::center(boss_select_panel_.Render()),
          ftxui::center(ClearUnder(BossConfirmDialog())),
      });
    // Over the arena for a fight that ran out of clock, and over the list for
    // a notice raised instead of a fight -- no weapon, or a daily already
    // taken. Which one it is shows in whether a run is still held.
    case kBossNotice:
      if (controller_.boss_run() != nullptr) {
        return RenderBossFight();
      }
      return ftxui::dbox({
          ftxui::center(boss_select_panel_.Render()),
          ftxui::center(ClearUnder(NoticeDialog())),
      });
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
          ftxui::center(ClearUnder(buy_panel_.Render())),
      });
    case kStarForce:
      return RenderStarForce();
    case kCubing:
      return RenderCubing();
    case kStarForceResult:
      return RenderStarForceResult();
    case kTraceRecover:
      return RenderTraceRecover();
    case kTraceRecoverResult:
      return ftxui::center(trace_recover_panel_.RenderResult(
          controller_.trace_recovery_result()));
    case kAllStats:
      return Standalone(all_stats_panel_.Render());
    case kHyperAlloc:
      return OverMain(controller_.hyper_stat_level_panel().Render());
    case kHyperReset:
      return OverMain(HyperResetDialog());
    case kAbilityReroll:
      return OverMain(AbilityRerollDialog());
    case kPotBuy:
      return OverMain(PotBuyDialog());
    case kPotInfo:
      return Standalone(pot_info_panel_.Render());
    case kHyperStatInspect:
      hyper_stat_inspect_panel_.SetStat(controller_.hyper_inspect_field(),
                                        controller_.hyper_inspect_level(),
                                        controller_.hyper_inspect_max_level());
      return Standalone(hyper_stat_inspect_panel_.Render());
    case kJobInspect:
      return RenderJobInspect();
    case kSkillInspect:
      skill_inspect_panel_.SetSkill(&controller_.skill_inspect_skill(),
                                    controller_.skill_inspect_level(),
                                    controller_.skill_inspect_bonus());
      skill_inspect_panel_.SetMaxRows(ftxui::Terminal::Size().dimy);
      skill_inspect_panel_.SetWidthBounds(0, ftxui::Terminal::Size().dimx);
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

// The menu the open screen floats over the layout, or null when none is open.
// Every one is anchored a row ABOVE the row its cursor stands on, so the entry
// standing highlighted lands beside what it would act on rather than below it,
// and clear of the names to its left: what the menu is about is the one thing
// it must not cover.
ftxui::Element Tui::OpenMenu(const MainWidths& widths) {
  if (controller_.screen() == kSkillMenu) {
    // Past the widest a skill name is ever drawn, so what it covers is the
    // level column -- exactly as the bag's menu covers an item's stats. Held
    // inside the character panel, whose column narrows with the terminal: at
    // the bare anchor a small terminal drew the menu out over the bag beside
    // it. A narrow panel takes a name's tail instead.
    constexpr int kSkillMenuCol = 38;
    const ItemMenu& menu = controller_.skill_menu();
    int col = std::max(0, std::min(kSkillMenuCol, widths.left - menu.Width()));
    return Floating(
        menu.Render(std::max(0, char_panel_.skill_cursor_row() - 1), col));
  }
  if (controller_.screen() == kJobMenu) {
    constexpr int kJobMenuCol = 14;
    return Floating(controller_.job_menu().Render(
        std::max(0, char_panel_.job_cursor_row() - 1), kJobMenuCol));
  }
  if (controller_.screen() == kPotMenu) {
    // Past the widest a pot name is drawn, so the menu covers the two columns
    // after it rather than the name the entry is about. Held inside the
    // character panel, whose column narrows with the terminal: a narrow panel
    // takes a name's tail instead, as the skill menu does.
    constexpr int kPotMenuCol = 26;
    const ItemMenu& menu = controller_.pot_menu();
    int col = std::max(0, std::min(kPotMenuCol, widths.left - menu.Width()));
    return Floating(
        menu.Render(std::max(0, char_panel_.pot_cursor_row() - 1), col));
  }
  if (controller_.screen() != kItemMenu) {
    return nullptr;
  }
  // The row is asked of the panel rather than counted up from the header rows
  // above it: the old arithmetic added a fixed offset to the selected index,
  // which stops being the row on screen the moment the bag scrolls.
  bool on_equip = panel_focus_ == kEquipPanel;
  int cursor_row =
      on_equip ? equip_panel_.cursor_row() : inventory_panel_.cursor_row();
  ItemMenu& menu = on_equip                        ? equip_panel_.menu()
                   : inventory_panel_.on_tab_bar() ? inventory_panel_.tab_menu()
                                                   : inventory_panel_.menu();
  // Past the char panel border, menu cursor, name column, slot column and
  // separators, so the menu covers stats rather than item names. An expanded
  // panel hands out a wider name column than the fixed 18 allows for, and has
  // no left column in front of it, so it is asked where its own columns end.
  // Nothing in front of an expanded panel, so the menu hangs at its own left
  // border rather than past the character panel's column.
  int left = controller_.expanded_panel() != kNoPanel ? 0 : widths.left;
  int col;
  if (!on_equip && inventory_panel_.on_tab_bar()) {
    // A tab menu is about the whole tab rather than one row's stats, so it
    // hangs under the bar at the panel's left, clear of nothing.
    col = left + 3;
  } else if (controller_.expanded_panel() != kNoPanel) {
    col =
        on_equip ? equip_panel_.menu_column() : inventory_panel_.menu_column();
  } else {
    col = left + 1 + 2 + 18 + 2 + 10 + 2;
  }
  return Floating(menu.Render(std::max(0, cursor_row - 1), col));
}

// A panel opened up to the whole terminal. It stands in for the main view
// rather than being a screen of its own, so the item menu floats over it and
// every dialog the panel raises keeps working untouched.
ftxui::Element Tui::RenderExpandedPanel() {
  int columns = ftxui::Terminal::Size().dimx;
  ftxui::Element body;
  if (controller_.expanded_panel() == kEquipPanel) {
    equip_panel_.SetWidth(columns);
    body = equip_component_->Render();
  } else {
    inventory_panel_.SetWidth(columns);
    body = inventory_component_->Render();
  }
  // No left column in front of it, so the menu hangs at the panel's own
  // columns -- see OpenMenu.
  ftxui::Element menu = OpenMenu(MainWidths{/*left=*/0, /*right=*/0});
  if (menu == nullptr) {
    return body;
  }
  return ftxui::dbox({std::move(body), std::move(menu)});
}

ftxui::Element Tui::RenderMain() {
  if (controller_.expanded_panel() != kNoPanel) {
    return RenderExpandedPanel();
  }
  // The character panel and the combat panel share the left column, and combat
  // is pinned to its foot. Without a budget the character panel takes the room
  // it wants and the mob bars are drawn off the bottom of a short terminal.
  // One row goes to the exp bar under both of them.
  int rows = ftxui::Terminal::Size().dimy - 1 - combat_panel_.Height();
  char_panel_.SetMaxRows(rows);
  // Both columns follow the terminal's width, and the panels are told theirs
  // before they draw: a panel sizes itself to its column, never the column to
  // whatever the panel is displaying.
  MainWidths widths =
      ComputeMainWidths(ftxui::Terminal::Size().dimx,
                        controller_.PanelVisible(kEquipPanel) ||
                            controller_.PanelVisible(kInventoryPanel));
  char_panel_.SetWidth(widths.left);
  combat_panel_.SetWidth(widths.left);
  equip_panel_.SetWidth(widths.right);
  inventory_panel_.SetWidth(widths.right);
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
  // The corner holds the tip or the menu, never both: the menu arrives at the
  // level the tip retires at.
  ftxui::Element corner = nullptr;
  if (HotkeysTipVisible(state_.character, state_.account)) {
    corner = HotkeysPanel();
  } else if (controller_.PanelVisible(kMenuPanel)) {
    corner = menu_component_->Render();
  }
  ftxui::Element layout =
      MainLayout(widths, char_panel_.Render(), combat_component_->Render(),
                 std::move(equipped), std::move(inventory), std::move(corner),
                 RenderExpBar());
  // Floated so a menu opened near the foot of a panel hangs off it rather than
  // being cut off at the edge of the terminal.
  ftxui::Element menu = OpenMenu(widths);
  if (menu == nullptr) {
    return layout;
  }
  return ftxui::dbox({layout, std::move(menu)});
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
  // Ahead of the fight, so a run steps against what the server has just said
  // rather than a tick's worth of stale roster -- and so a fight the party
  // has begun opens its screen on the tick it arrives.
  if (multiplayer_ != nullptr) {
    multiplayer_->Advance(state_);
  }
  controller_.AdvanceParty();
  if (controller_.in_boss_fight()) {
    // The map is not farmed while the player is somewhere else: EXP quietly
    // arriving from a fight they cannot see is a strange thing to owe them.
    // The analysis is not fed either, so its clock stops with the farming.
    controller_.AdvanceBossRun(elapsed.count());
  } else {
    RewardTally tally = AdvanceCombat(state_, combat_sim_, elapsed.count());
    AnalysisSample sample;
    sample.seconds = elapsed.count();
    sample.respawned = combat_sim_.view().respawned_this_step;
    sample.damage = combat_sim_.view().damage_this_step;
    sample.kills = TotalKills(combat_sim_);
    sample.meso = tally.meso;
    sample.exp = tally.exp;
    analysis_.Advance(sample);
  }
  // Ticked down before the new level is noticed, so a level-up landing on this
  // tick gets its full four seconds rather than one tick's worth less.
  celebration_.Advance(elapsed.count());
  celebration_.Visit(FocusedPanel());
  NoticeProgress();
  // Last, so that dying wins the card over anything else this tick turned up.
  // A level earned on the way down is still a level, and its gold is still
  // lit -- but what the player needs told is that they are no longer where
  // they thought they were.
  if (combat_sim_.view().died_this_step) {
    celebration_.BeginDeath();
  }
  // Last of all, so the beat the ticker sleeps next is the one this tick left
  // the player on.
  in_boss_fight_ = controller_.in_boss_fight();
}

Panel Tui::FocusedPanel() const {
  if (controller_.screen() != kMain) {
    return kNoPanel;
  }
  return static_cast<Panel>(panel_focus_);
}

void Tui::NoticeProgress() {
  // A level earned from a boss waits for the player to walk out of the fight.
  // The card would otherwise land on top of the clear card and cover the
  // reward it was earned from. The watcher is not asked at all, so the level
  // is still there to be noticed on the way out.
  if (controller_.in_boss_fight()) {
    return;
  }
  Progress progress = progress_watcher_.Notice(state_.character.proto());
  switch (progress.kind) {
    case kJobAdvanced:
      celebration_.BeginAdvancement(progress.from_job, progress.to_job,
                                    progress.to_stage, FocusedPanel());
      return;
    case kLevelGained:
      celebration_.BeginLevelUp(progress.from_level, progress.to_level,
                                progress.ap, progress.sp, progress.hyper_sp,
                                state_.account.max_level(), FocusedPanel());
      return;
    case kNothingNoticed:
      return;
  }
}

bool Tui::OnEvent(ftxui::Event event) {
  // A player who has looked is done with it. The key still does whatever it
  // normally does -- getting rid of the card is a side effect, not a key the
  // celebration swallows, so nothing the player meant to do is lost. Custom is
  // the ticker's own redraw and is not somebody looking.
  if (celebration_.card_visible() && event != ftxui::Event::Custom) {
    celebration_.Dismiss();
  }
  // The All Stats screen's Farm/Boss row is the panel's own, and the panel
  // lives here rather than on the controller. Everything it does not take --
  // the keys that close the screen -- carries on as usual.
  if (controller_.screen() == kAllStats && all_stats_panel_.OnEvent(event)) {
    return true;
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
