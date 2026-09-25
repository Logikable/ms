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
#include "src/frontend/cards/offline_card.h"
#include "src/frontend/keybinds.h"
#include "src/frontend/main_layout.h"
#include "src/frontend/panels/character_panel.h"
#include "src/frontend/panels/combat_panel.h"
#include "src/frontend/panels/equipped_panel.h"
#include "src/frontend/panels/hotkeys_panel.h"
#include "src/frontend/panels/inventory_panel.h"
#include "src/frontend/placement.h"
#include "src/frontend/screens/boss_clear_panel.h"
#include "src/frontend/screens/boss_fight_panel.h"
#include "src/frontend/screens/map_select_panel.h"
#include "src/frontend/screens/scroll_panel.h"
#include "src/frontend/tui_controller.h"
#include "src/frontend/widgets/amount_selector.h"
#include "src/frontend/widgets/chrome.h"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/exp_bar.h"
#include "src/frontend/widgets/format.h"
#include "src/frontend/widgets/game_names.h"
#include "src/frontend/widgets/item_menu.h"
#include "src/frontend/widgets/marquee.h"
#include "src/frontend/widgets/text_columns.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/skill.pb.h"
#include "src/save.h"

namespace ms {
namespace {

// Every kill recorded in the step, across all mobs on the map.
int64_t TotalKills(const CombatSim& sim) {
  int64_t total = 0;
  for (int64_t kills : sim.view().kills_this_step) {
    total += kills;
  }
  return total;
}

// Set by a signal asking the game to close. A signal handler can do very little
// safely, so it only sets this, and the loop exits normally on its next tick,
// saving on the way.
volatile std::sig_atomic_t g_leaving = 0;

extern "C" void NoteLeaving(int) {
  g_leaving = 1;
}

void HandleClosingSignals() {
  std::signal(SIGINT, NoteLeaving);
  std::signal(SIGTERM, NoteLeaving);
#ifdef SIGHUP
  // Closing the terminal window, on platforms that have it.
  std::signal(SIGHUP, NoteLeaving);
#endif
}

// The session for `server`, which is "host:port". Null for an empty string or a
// port that isn't a number.
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

// `text` as centred rows, one per line. A notice is nearly always one line; a
// version mismatch adds the version numbers under the message.
std::vector<ftxui::Element> CenteredRows(const std::string& text) {
  std::vector<ftxui::Element> rows;
  size_t start = 0;
  while (start <= text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string::npos) {
      end = text.size();
    }
    rows.push_back(CenteredRow(text.substr(start, end - start)));
    start = end + 1;
  }
  return rows;
}

// What the player holds above what they have offered, one line each, aligned in
// one column. The same layout as PriceBlock, but neither line is a price, since
// an offer can't be unaffordable.
ftxui::Element OfferBlock(const std::string& held, const std::string& offered) {
  constexpr int kLabelWidth = 8;  // "Offering"
  const int width = std::max(TextColumns(held), TextColumns(offered));
  return ftxui::vbox({
      CenteredRow(PadRight("Held", kLabelWidth) + "  " + PadLeft(held, width)),
      CenteredRow(PadRight("Offering", kLabelWidth) + "  " +
                  PadLeft(offered, width)),
  });
}

}  // namespace

Tui::Tui(GameState& state, std::string save_path, std::string server, bool bgm)
    : state_(state),
      save_policy_(std::move(save_path), std::chrono::steady_clock::now()),
      multiplayer_(MakeSession(server)),
      progress_watcher_(state.character.proto()),
      music_player_(bgm ? MusicPlayer::Backend::kDevice
                        : MusicPlayer::Backend::kNull),
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
      jukebox_panel_(state, music_director_, state.account),
      all_stats_panel_(state.character, &state.account, state.skills),
      trade_panel_(state.character, state.account),
      player_inspect_panel_(state),
      multi_sell_panel_(state.character, state.account),
      shop_panel_(state.character, state.equips, state.items),
      bank_panel_(state.character, state.account, state.items),
      link_skill_panel_(state.character, state.skills),
      controller_(state, Screens{char_panel_,           equip_panel_,
                                 inventory_panel_,      scroll_panel_,
                                 inspect_panel_,        preview_inspect_panel_,
                                 star_force_panel_,     cube_panel_,
                                 trace_recover_panel_,  sell_panel_,
                                 sell_equip_panel_,     multi_sell_panel_,
                                 map_select_panel_,     mob_inspect_panel_,
                                 boss_select_panel_,    party_select_panel_,
                                 player_list_panel_,    trade_panel_,
                                 player_inspect_panel_, player_item_panel_,
                                 shop_panel_,           buy_panel_,
                                 bank_panel_,           link_skill_panel_,
                                 job_inspect_panel_,    skill_inspect_panel_,
                                 buff_info_panel_,      menu_panel_,
                                 keybinds_panel_,       options_panel_,
                                 jukebox_panel_},
                  analysis_, keys_, panel_focus_, multiplayer_.get()) {
  // Both inspect panels read the character, not just the item: a set piece is
  // shown with its set, and which tiers are active depends on what is worn.
  inspect_panel_.UseCharacter(state.character);
  preview_inspect_panel_.UseCharacter(state.character);
  player_item_panel_.UseCharacter(player_inspect_panel_.character());
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
    // The screen opens on the allocation the panel behind it is showing, so
    // pressing Enter never changes the numbers.
    all_stats_panel_.SetPreset(char_panel_.SelectedActivity());
    controller_.OpenAllStats();
  };
  char_actions.learn = [this](const Skill& skill) {
    controller_.OpenSkillLearn(skill);
  };
  char_actions.menu = [this](const Skill& skill) {
    controller_.OpenSkillMenu(skill);
  };
  char_actions.link_skills = [this]() { controller_.OpenLinkSkills(); };
  char_actions.v_reset = [this]() { controller_.OpenVMatrixReset(); };
  char_actions.advance = [this](Job job) { controller_.OpenJobMenu(job); };
  char_actions.hyper_allocate = [this](HyperStatField field) {
    controller_.RaiseHyperStat(field, char_panel_.hyper_preset());
  };
  char_actions.hyper_lower = [this](HyperStatField field) {
    controller_.LowerHyperStat(field, char_panel_.hyper_preset());
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
  char_actions.buff_menu = [this](ConsumableType type) {
    controller_.OpenBuffMenu(type);
  };
  char_actions.preset_menu = [this](PresetKind kind, StatPreset slot) {
    controller_.OpenPresetMenu(kind, slot);
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
        // Ctrl+C exits the same way as the quit dialog, so it saves the same
        // way. It is handled as an event, not by the signal handler, because
        // ftxui installs its own SIGINT handler once the loop runs.
        if (event == ftxui::Event::CtrlC) {
          screen.Exit();
          return true;
        }
        bool handled = OnEvent(event);
        // The controller can decide the game is over, but only the loop here
        // can end it. This is checked after every event, not only the ones it
        // handled, so no key can set the flag without it being seen.
        if (controller_.quit_requested()) {
          screen.Exit();
        }
        return handled;
      });
  // Outermost, so the whole tree (ftxui's own menus included) receives the
  // game's key events rather than the terminal's.
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

  // Runs the idle game: wake periodically, advance combat on the loop thread
  // (so game state is only changed on one thread), and redraw.
  std::atomic<bool> running = true;
  std::thread ticker([this, &screen, &running]() {
    while (running) {
      // The repaint interval, set by the fastest-moving thing on screen: a
      // scrolling name normally, a charging swing in a boss fight. Everything
      // runs on elapsed time rather than tick count, so waking more often only
      // costs the wakeups.
      std::this_thread::sleep_for(in_boss_fight_ ? kBossFightStep
                                                 : kMarqueeStep);
      screen.Post([this, &screen]() {
        Tick();
        std::chrono::steady_clock::time_point now =
            std::chrono::steady_clock::now();
        if (controller_.TakeSaveRequest()) {
          save_policy_.Save(state_, now);
        } else {
          save_policy_.AutosaveIfDue(state_, now);
        }
        // Handled here rather than in the signal handler: this runs on the loop
        // thread, where ending the loop and writing a file are both safe.
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
  // Before saving, so an account the server issued this session is in the saved
  // file.
  if (multiplayer_ != nullptr) {
    multiplayer_->Advance(state_);
    multiplayer_->Stop();
  }
  // Every way out of the loop ends here (the quit dialog, Ctrl+C, a closed
  // window), so this is the one place the final save needs to happen.
  save_policy_.Save(state_, std::chrono::steady_clock::now());
}

ftxui::Element Tui::RenderFrame() {
  // Set every frame rather than when a celebration starts, so the panels turn
  // off by themselves and nothing has to remember to reset them. The ability
  // rank-up uses the same gold and clears on the next key.
  char_panel_.SetHighlighted(celebration_.Lights(kCharPanel) ||
                             controller_.ability_rank_up());
  equip_panel_.SetHighlighted(celebration_.Lights(kEquipPanel));
  inventory_panel_.SetHighlighted(celebration_.Lights(kInventoryPanel));
  equip_panel_.SetExpanded(controller_.expanded_panel() == kEquipPanel);
  inventory_panel_.SetExpanded(controller_.expanded_panel() == kInventoryPanel);

  ftxui::Element frame = RenderScreen();
  // Under the dialogs and the card: the notification is news, and a question
  // the player has to answer matters more.
  if (controller_.notification().visible()) {
    frame = BottomRight(std::move(frame), controller_.notification().Render());
  }
  if (controller_.party_notice_prompt().open()) {
    // Over whatever the player is looking at: the server doesn't wait for them
    // to be on the party screen before removing them from a party.
    frame = Overlay(std::move(frame), PartyNoticeDialog());
  }
  if (!celebration_.card_visible()) {
    return frame;
  }
  // Over whatever the player is looking at, including the shop and map select:
  // a card shown only on the main screen would miss a player who is elsewhere.
  // An overlay shrinks to its content, so the card sets its own minimum width
  // instead of sizing to its longest line.
  return Overlay(std::move(frame), celebration_.Render());
}

ftxui::Element Tui::OverMain(ftxui::Element dialog) {
  return Overlay(RenderMain(), std::move(dialog));
}

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
  const Skill& skill = controller_.skill_learn_skill();
  std::vector<ftxui::Element> rows = {CenteredRow(skill.name()),
                                      ThemedSeparator()};
  // A node costs a price rather than one point per level, so the dialog shows
  // the total cost and what the pool has. An SP skill needs neither: its page
  // shows the pool, and the cost equals the amount.
  if (skill.v_node() != V_NODE_KIND_UNSPECIFIED) {
    const int64_t held = state_.character.v_points();
    const int64_t cost =
        state_.character.VNodeCostFor(skill, controller_.sp_selector().value());
    rows.push_back(PriceBlock(FormatWithCommas(held) + " VP",
                              FormatWithCommas(cost) + " VP", cost <= held));
    rows.push_back(ThemedSeparator());
  }
  rows.push_back(controller_.sp_selector().Render());
  return ThemedWindow(" Learn Skill ", ftxui::vbox(std::move(rows)));
}

ftxui::Element Tui::TradeAmountDialog() {
  TradeCurrency currency = controller_.trade_currency();
  const bool meso = currency == TradeCurrency::kMeso;
  std::string held = meso ? FormatMeso(trade_panel_.held(currency))
                          : FormatSpellTraces(trade_panel_.held(currency));
  std::string offered = meso
                            ? FormatMeso(trade_panel_.offered(currency))
                            : FormatSpellTraces(trade_panel_.offered(currency));
  return ThemedWindow(meso ? " Meso " : " Spell Traces ",
                      ftxui::vbox({
                          OfferBlock(held, offered),
                          ThemedSeparator(),
                          controller_.trade_selector().Render(),
                      }));
}

ftxui::Element Tui::BankAmountDialog() {
  const bool meso = controller_.bank_currency() == BankCurrency::kMeso;
  // The direction is set by which half the cursor is in, and the question says
  // so, since the same dialog asks the opposite from the other half.
  const bool to_bank = bank_panel_.zone() == BankZone::kBag;
  return ThemedWindow(
      meso ? " Meso " : " Spell Traces ",
      ftxui::vbox({
          CenteredRow("How much to move"),
          CenteredRow(to_bank ? "to the bank?" : "to the inventory?"),
          ThemedSeparator(),
          controller_.bank_selector().Render(),
      }));
}

ftxui::Element Tui::RenderBankInspect() {
  const EquipTabItem* item = bank_panel_.selected_equip();
  // SetItem has two overloads, so this can't be one ternary.
  if (item == nullptr) {
    const StackableItem* stack = bank_panel_.selected_stack();
    inspect_panel_.SetItem(stack == nullptr ? nullptr : &stack->prototype());
  } else {
    inspect_panel_.SetItem(item);
    inspect_panel_.SetComparison(controller_.comparison_slots());
    inspect_panel_.SetCombatPowerDelta(controller_.CombatPowerDelta(item));
  }
  inspect_panel_.SetMaxRows(ftxui::Terminal::Size().dimy);
  inspect_panel_.SetMaxColumns(ftxui::Terminal::Size().dimx);
  return Centred(inspect_panel_.Render());
}

ftxui::Element Tui::TradeItemAmountDialog() {
  const std::vector<StackableItem>& stacks = state_.character.stackables();
  int index = controller_.trade_stack();
  if (index < 0 || index >= static_cast<int>(stacks.size())) {
    return ftxui::text("");
  }
  // Held is the whole stack, however much is already offered: offering
  // something doesn't change what the player owns, and [MAX] must reach all of
  // it.
  return ThemedWindow(
      " " + stacks[index].name() + " ",
      ftxui::vbox({
          OfferBlock(FormatWithCommas(stacks[index].count()),
                     FormatWithCommas(trade_panel_.stack_offered(index))),
          ThemedSeparator(),
          controller_.trade_selector().Render(),
      }));
}

ftxui::Element Tui::TradeConfirmDialog() {
  // The player who accepted first waits for the other and can still take it
  // back, so their button says so instead of disappearing.
  const bool waiting = controller_.trade_waiting();
  ConfirmFocus focus = controller_.trade_prompt().focus();
  ftxui::Element buttons =
      ButtonRow(waiting ? "Waiting..." : "Confirm", "Cancel",
                focus == ConfirmFocus::kConfirm, focus == ConfirmFocus::kCancel,
                /*go_enabled=*/!waiting);
  return DialogWindow("", {CenteredRow("Finalize Trade?")}, std::move(buttons));
}

ftxui::Element Tui::RenderTradeInspect() {
  const EquipTabItem* item = controller_.trade_inspect_equip();
  // SetItem has two overloads, so this can't be one ternary.
  if (item == nullptr) {
    inspect_panel_.SetItem(controller_.trade_inspect_stack());
  } else {
    inspect_panel_.SetItem(item);
    // Compared against what the viewer is wearing, since that is the first
    // thing either side wants to know about an offered item.
    inspect_panel_.SetComparison(controller_.comparison_slots());
    inspect_panel_.SetCombatPowerDelta(controller_.CombatPowerDelta(item));
  }
  inspect_panel_.SetMaxRows(ftxui::Terminal::Size().dimy);
  inspect_panel_.SetMaxColumns(ftxui::Terminal::Size().dimx);
  return Centred(inspect_panel_.Render());
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
  // No title, like the quit dialog: the question is the whole dialog.
  return DialogWindow("", {CenteredRow(controller_.hyper_reset_question())},
                      controller_.hyper_reset_prompt().Render());
}

ftxui::Element Tui::VMatrixResetDialog() {
  // No title, like the Hyper question. The matrix is shared rather than one per
  // preset, so the question names no preset.
  return DialogWindow("", {CenteredRow("Reset V Matrix?")},
                      controller_.v_matrix_reset_prompt().Render());
}

ftxui::Element Tui::PresetMoveDialog() {
  const bool autoswap = state_.character.autoswap_presets();
  std::vector<ftxui::Element> body = {
      CenteredRow("Swap " +
                  PresetSlotName(controller_.preset_slot(), autoswap) +
                  " with"),
      AccentSeparator(kTheme),
  };
  for (int i = 0; i < kNumStatPresets; ++i) {
    body.push_back(
        HighlightRow(CenteredRow(PresetSlotName(StatPresetAt(i), autoswap)),
                     i == controller_.preset_move_row()));
  }
  return DialogWindow(
      "", std::move(body),
      ActionButton("Cancel", controller_.preset_move_row() == kNumStatPresets));
}

ftxui::Element Tui::AbilityRerollDialog() {
  // AccentSeparator, not ftxui::separator: a dialog's content is drawn white,
  // so a plain line in the body would be white next to the theme-blue line
  // DialogWindow draws above the buttons.
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

ftxui::Element Tui::BuffBuyDialog() {
  const ConsumableInfo* info = ConsumableInfoFor(controller_.buff_type());
  const bool affordable = controller_.buff_buy_affordable();
  // Three short rows rather than one long one: the buff's name is what the
  // player is deciding on, and the price is what they are weighing.
  return DialogWindow(
      "",
      {
          CenteredRow(info == nullptr ? "" : "Buy " + std::string(info->name)),
          CenteredRow("permanently for"),
          CenteredRow(
              RedUnless(ftxui::text(FormatMeso(controller_.buff_buy_price())),
                        affordable)),
      },
      ConfirmButtons(controller_.buff_buy_prompt().focus(), affordable));
}

ftxui::Element Tui::QuitDialog() {
  // No title: the question is the whole dialog, and a " Quit Game " title over
  // a "Quit Game?" row would ask it twice.
  return DialogWindow("", {CenteredRow("Quit Game?")},
                      controller_.quit_prompt().Render());
}

ftxui::Element Tui::RenderMenuBox() {
  // The EXP bar, which the corner menu sits one row above.
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
  // No title, like the quit dialog: the question is the whole dialog.
  return DialogWindow("", {CenteredRow(controller_.party_prompt_question())},
                      controller_.party_prompt().Render());
}

ftxui::Element Tui::PartyNoticeDialog() {
  // Red when the server refused something or the connection dropped; theme blue
  // when the party changed around the player.
  ftxui::Color accent = controller_.party_notice_is_refusal() ? kRed : kTheme;
  return DialogWindow("", CenteredRows(controller_.party_notice()),
                      controller_.party_notice_prompt().Render("Close"),
                      accent);
}

ftxui::Element Tui::RenderParty() {
  // kPartyMenu draws the same thing: the menu is anchored to a row of the list,
  // so the panel draws it.
  ftxui::Element screen = Centred(party_select_panel_.Render());
  if (controller_.screen() != kPartyConfirm) {
    return screen;
  }
  return Overlay(std::move(screen), PartyConfirmDialog());
}

ftxui::Element Tui::RenderPlayerInspect() {
  // The item gets its own screen, as the player's own items do. The sheet it
  // came from is already a screen, and a card on top of it meant reading two
  // screens at once.
  if (controller_.screen() == kPlayerItemInspect) {
    player_item_panel_.SetItem(player_inspect_panel_.selected_item());
    player_item_panel_.SetComparison(controller_.comparison_slots());
    player_item_panel_.SetCombatPowerDelta(controller_.player_item_delta());
    player_item_panel_.SetMaxRows(ftxui::Terminal::Size().dimy);
    player_item_panel_.SetMaxColumns(ftxui::Terminal::Size().dimx);
    return Centred(player_item_panel_.Render());
  }
  if (controller_.screen() == kPlayerAllStats) {
    return Centred(player_inspect_panel_.RenderAllStats());
  }
  // The whole terminal, like the main view: this screen shows the member's own
  // panels, laid out at the same widths as the player's.
  return player_inspect_panel_.Render(ftxui::Terminal::Size().dimy,
                                      ftxui::Terminal::Size().dimx);
}

ftxui::Element Tui::BossConfirmDialog() {
  // No title, like the quit dialog: the question is the whole dialog.
  // "Practice" instead of "Fight" is the last chance to notice the practice
  // switch is on, since this run would pay nothing.
  std::string verb =
      controller_.boss_prompt_practice() ? "Practice " : "Fight ";
  return DialogWindow(
      "", {CenteredRow(verb + controller_.boss_prompt_title() + "?")},
      controller_.boss_prompt().Render());
}

ftxui::Element Tui::NoticeDialog() {
  // Red when the player is the reason (no weapon, an item that can't take more
  // hammers), and theme blue when it is only a timer. The caller chooses the
  // button's label: a result says to continue, a notice says to close.
  bool refused = controller_.notice_is_refusal();
  ftxui::Elements rows;
  for (const std::string& line : controller_.notice_lines()) {
    rows.push_back(CenteredRow(line));
  }
  return DialogWindow(
      "", std::move(rows),
      controller_.notice_prompt().Render(controller_.notice_button()),
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

// What is shown over the arena, if anything: the leave prompt, or the fight's
// result. Null while the fight is in progress.
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
                            ButtonRow("Continue", "Analysis",
                                      !controller_.boss_clear_on_analysis(),
                                      controller_.boss_clear_on_analysis(),
                                      /*go_enabled=*/true),
                            HonorVisible(state_.character.proto().level(),
                                         state_.account.max_level()));
    default:
      return nullptr;
  }
}

ftxui::Element Tui::RenderBossFight() {
  const BossRun* run = controller_.boss_run();
  if (run == nullptr) {
    return Centred(boss_select_panel_.Render());
  }
  // The fight's result is shown over the arena, so the player sees the fight
  // they just finished rather than the list they are returning to.
  ftxui::Element fight = BossFightPanel(*run, state_.account.buff_indicators());
  ftxui::Element overlay = BossFightOverlay();
  if (overlay == nullptr) {
    return fight;
  }
  return Overlay(std::move(fight), std::move(overlay));
}

ftxui::Element Tui::RenderBuyBackInspect(const BuyBackEntry& entry) {
  if (entry.has_stack()) {
    const ItemPrototype* proto =
        FindItemByName(state_.items, entry.stack().name());
    if (proto == nullptr) {
      return Centred(shop_panel_.Render());
    }
    inspect_panel_.SetItem(proto);
    return Centred(inspect_panel_.Render());
  }
  const EquipPrototype* proto =
      FindEquipByName(state_.equips, entry.equip().equip_name());
  if (proto == nullptr) {
    return Centred(shop_panel_.Render());
  }
  // Rebuilt from the state saved with the sale, which is what buying it back
  // would give. A trace is shown as a trace for the same reason.
  if (entry.equip().trace()) {
    EquipTrace trace(*proto, entry.equip());
    inspect_panel_.SetItem(&trace);
    inspect_panel_.SetComparison(controller_.comparison_slots());
    inspect_panel_.SetCombatPowerDelta(controller_.CombatPowerDelta(&trace));
    return Centred(inspect_panel_.Render());
  }
  EquipInstance item(*proto, entry.equip());
  inspect_panel_.SetItem(&item);
  inspect_panel_.SetComparison(controller_.comparison_slots());
  inspect_panel_.SetCombatPowerDelta(controller_.CombatPowerDelta(&item));
  return Centred(inspect_panel_.Render());
}

ftxui::Element Tui::RenderShopInspect() {
  inspect_panel_.SetMaxRows(ftxui::Terminal::Size().dimy);
  inspect_panel_.SetMaxColumns(ftxui::Terminal::Size().dimx);
  // A buy-back row is an item the player owned, so the inspect shows that item,
  // stars and scrolls included, not a fresh one from the shop.
  const BuyBackEntry* entry = shop_panel_.selected_buy_back();
  if (entry != nullptr) {
    return RenderBuyBackInspect(*entry);
  }
  // A stackable has no instance to build and no preview: the panel reads the
  // prototype directly, as the bag's Etc tab does.
  const ItemPrototype* stackable = shop_panel_.selected_stackable();
  if (stackable != nullptr) {
    inspect_panel_.SetItem(stackable);
    return Centred(inspect_panel_.Render());
  }
  const EquipPrototype* proto = shop_panel_.selected_item();
  if (proto == nullptr) {
    return Centred(shop_panel_.Render());
  }
  // A clean copy of what the shop would give, with no scrolls or stars. Built
  // here because nothing owns a shop item until it is bought.
  EquipInstance preview(*proto);
  inspect_panel_.SetItem(&preview);
  inspect_panel_.SetComparison(controller_.comparison_slots());
  inspect_panel_.SetCombatPowerDelta(controller_.CombatPowerDelta(&preview));
  return Centred(inspect_panel_.Render());
}

// The job's book on the left and the selected skill on the right, shown at its
// first and last levels: the player hasn't spent points on it and has none to
// spend, so "one more point" would say nothing.
ftxui::Element Tui::RenderJobInspect() {
  // One size for the whole book: the card keeps that size whichever skill is
  // selected, so the screen doesn't shift as the cursor moves.
  int rows = ftxui::Terminal::Size().dimy;
  PreviewCardSize card =
      LargestPreviewCard(job_inspect_panel_.Skills(),
                         ftxui::Terminal::Size().dimx - kJobInspectBookWidth);
  skill_inspect_panel_.SetSkill(job_inspect_panel_.selected_skill(), 0, 0,
                                SkillInspectPanel::kPreview);
  skill_inspect_panel_.SetWidthBounds(card.columns, card.columns);
  // The card scrolls instead of running off the terminal, and the book's
  // minimum height shrinks with it.
  skill_inspect_panel_.SetMaxRows(rows);
  return Centred(JobInspectScreen(job_inspect_panel_.Render(),
                                  skill_inspect_panel_.Render(),
                                  std::min(card.rows, rows)));
}

ftxui::Element Tui::RenderTraceRecover() {
  EquipInstance preview = trace_recover_panel_.PreviewResult();
  preview_inspect_panel_.SetItem(&preview);
  int base_idx = trace_recover_panel_.selected_index();
  inspect_panel_.SetItem(base_idx >= 0 ? &state_.character.inventory()[base_idx]
                                       : nullptr);
  int rows = ftxui::Terminal::Size().dimy;
  preview_inspect_panel_.SetMaxRows(rows);
  // The right card shares its column with the tab row above and the confirm bar
  // below, so it gets the rows those leave.
  inspect_panel_.SetMaxRows(rows - kRecoverTabRows -
                            ConfirmPrompt::kWindowHeight);
  bool right = controller_.right_card_focused();
  ftxui::Element right_col = ftxui::vbox({
      trace_recover_panel_.RenderTabs(),
      inspect_panel_.RenderItemOnly(right),
      trace_recover_panel_.RenderBelow(),
  });
  return SideBySide(
      {preview_inspect_panel_.RenderItemOnly(!right), std::move(right_col)});
}

// The item as it is, the panel, and the item with one more star. At max stars
// there is no "after": the panel says so, and a second card would repeat it.
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
  // screen can draw the same panel after the item is destroyed.
  star_force_panel_.SetItem(
      star_force_before_.has_value() ? &*star_force_before_ : nullptr,
      state_.character.meso());
  return StarForceColumns();
}

// The result window over the cards from the attempt. The item may be gone by
// now, which is why the columns come from the cache.
ftxui::Element Tui::RenderStarForceResult() {
  ftxui::Element result =
      star_force_panel_.RenderResult(controller_.star_force_result());
  return Overlay(StarForceColumns(), std::move(result));
}

ftxui::Element Tui::StarForceColumns() {
  ftxui::Element panel = star_force_panel_.Render();
  if (!star_force_before_.has_value()) {
    return Centred(std::move(panel));
  }
  int rows = ftxui::Terminal::Size().dimy;
  bool right = controller_.right_card_focused();
  bool two_cards = star_force_after_.has_value();
  inspect_panel_.SetItem(&*star_force_before_);
  inspect_panel_.SetMaxRows(rows);
  // The three side by side are what is being compared. They show one item a
  // star apart, so each is titled for its side of the attempt.
  ftxui::Elements cards;
  cards.push_back(
      inspect_panel_.RenderItemOnly(two_cards && !right, " Before "));
  cards.push_back(std::move(panel));
  if (two_cards) {
    preview_inspect_panel_.SetItem(&*star_force_after_);
    preview_inspect_panel_.SetMaxRows(rows);
    cards.push_back(preview_inspect_panel_.RenderItemOnly(right, " After "));
  }
  return SideBySide(std::move(cards));
}

// The cube shelf and the item's card side by side, in two columns of similar
// width. An open confirmation is centred over both, since it is about the cube
// on the left and the lines on the right.
ftxui::Element Tui::RenderCubing() {
  const EquipInstance* item = controller_.cube_item();
  cube_panel_.SetItem(item, state_.character.meso());
  inspect_panel_.SetItem(item);
  inspect_panel_.SetMaxRows(ftxui::Terminal::Size().dimy);
  bool right = controller_.right_card_focused();
  ftxui::Element columns = SideBySide({
      cube_panel_.Render(!right),
      ftxui::text(" "),
      inspect_panel_.RenderItemOnly(right),
  });
  if (!cube_panel_.IsConfirming()) {
    return columns;
  }
  return Overlay(std::move(columns), cube_panel_.RenderConfirm());
}

ftxui::Element Tui::RenderInspect() {
  // One screen for two kinds of item: the panel shows whichever the cursor was
  // on and frames both the same way. SetItem has two overloads, so this can't
  // be one ternary.
  if (controller_.screen() == kItemInspect) {
    inspect_panel_.SetItem(controller_.item_inspect_item());
  } else {
    inspect_panel_.SetItem(controller_.inspect_item());
    // Stackables are never worn, so only an equip is compared against what the
    // player has on.
    inspect_panel_.SetComparison(controller_.comparison_slots());
    inspect_panel_.SetCombatPowerDelta(controller_.inspect_delta());
  }
  inspect_panel_.SetMaxRows(ftxui::Terminal::Size().dimy);
  inspect_panel_.SetMaxColumns(ftxui::Terminal::Size().dimx);
  return Centred(inspect_panel_.Render());
}

ftxui::Element Tui::RenderScroll() {
  inspect_panel_.SetItem(controller_.scroll_item());
  inspect_panel_.SetMaxRows(ftxui::Terminal::Size().dimy);
  ftxui::Element scroll_view =
      scroll_panel_.Render(!controller_.right_card_focused());
  if (controller_.screen() == kScrollResult) {
    ftxui::Element dialog =
        scroll_panel_.RenderResult(controller_.scroll_result());
    scroll_view = Overlay(std::move(scroll_view), std::move(dialog));
  }
  return SideBySide(
      {std::move(scroll_view),
       inspect_panel_.RenderItemOnly(controller_.right_card_focused())});
}

ftxui::Element Tui::RenderMultiSell() {
  ftxui::Element screen = Centred(multi_sell_panel_.Render());
  if (!multi_sell_panel_.confirming()) {
    return screen;
  }
  return Overlay(std::move(screen), multi_sell_panel_.RenderConfirm());
}

ftxui::Element Tui::RenderScreen() {
  switch (controller_.screen()) {
    // Dialogs are drawn over the main view, so what they are about stays
    // visible behind them.
    case kApAlloc:
      return OverMain(ApAllocDialog());
    case kSkillLearn:
      return OverMain(SkillLearnDialog());
    case kJobAdvance:
      return OverMain(JobAdvanceDialog());
    case kQuit:
      // Over the screen it was opened from. The character select is the only
      // one that isn't the main view, and showing a game behind the question
      // would answer it for the player.
      if (controller_.quit_return() == kCharacterSelect) {
        return Overlay(Centred(controller_.character_select_panel().Render()),
                       QuitDialog());
      }
      return OverMain(QuitDialog());
    case kOffline:
      return OverMain(OfflineCard(controller_.offline_report(),
                                  controller_.offline_prompt().Render(),
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
    // The dialog belongs to the panel, so the screen and its dialog are one
    // state.
    case kMultiSell:
      return RenderMultiSell();
    // kMapMenu draws the same thing: the menu is anchored to a row of the list,
    // so the panel draws it.
    case kMapSelect:
    case kMapMenu:
      return Centred(map_select_panel_.Render());
    case kMobInspect:
      return Centred(mob_inspect_panel_.Render());
    case kMenuBox:
      return RenderMenuBox();
    case kDailies:
      return OverMain(controller_.dailies_panel().Render());
    case kDailiesNotice:
      return OverMain(NoticeDialog());
    case kAnalysis:
      return OverMain(analysis_panel_.Render());
    case kKeybinds:
      return Centred(keybinds_panel_.Render());
    // kCharacterMenu draws the same thing: the menu is anchored to a row of the
    // list, so the panel draws it.
    case kCharacterSelect:
    case kCharacterMenu:
      return Centred(controller_.character_select_panel().Render());
    case kCharacterDelete:
      return Overlay(
          Centred(controller_.character_select_panel().Render()),
          DialogWindow("",
                       {CenteredRow("Delete this character?"),
                        CenteredRow("This is irreversible.")},
                       controller_.character_delete_prompt().Render()));
    case kOptions:
      return Centred(options_panel_.Render());
    case kJukebox:
      return Centred(jukebox_panel_.Render());
    case kTrade:
    case kTradeMenu:
      // The menu is anchored to a row of one of the windows, so the panel draws
      // it.
      return Centred(trade_panel_.Render());
    case kTradeAmount:
      return Overlay(Centred(trade_panel_.Render()), TradeAmountDialog());
    case kTradeItemAmount:
      return Overlay(Centred(trade_panel_.Render()), TradeItemAmountDialog());
    case kTradeConfirm:
      return Overlay(Centred(trade_panel_.Render()), TradeConfirmDialog());
    case kTradeLeave:
      return Overlay(Centred(trade_panel_.Render()),
                     DialogWindow("", {CenteredRow("Leave this trade?")},
                                  controller_.trade_leave_prompt().Render()));
    case kTradeInspect:
      return RenderTradeInspect();
    case kBank:
    case kBankMenu:
      // The menu is anchored to a row of one of the halves, so the panel draws
      // it.
      return Centred(bank_panel_.Render());
    case kBankAmount:
      return Overlay(Centred(bank_panel_.Render()), BankAmountDialog());
    case kBankInspect:
      return RenderBankInspect();
    // kLinkSkillMenu draws the same thing: the menu is anchored to a row of the
    // screen, so the panel draws it.
    case kLinkSkills:
    case kLinkSkillMenu:
      return Centred(link_skill_panel_.Render());
    case kPlayerList:
    case kPlayerMenu:
      // The menu is anchored to a row of the list, so the panel draws it.
      return Centred(player_list_panel_.Render());
    case kPartySelect:
    case kPartyMenu:
    case kPartyConfirm:
      return RenderParty();
    case kPlayerAllStats:
    case kPlayerInspect:
    case kPlayerItemInspect:
      return RenderPlayerInspect();
    case kBossSelect:
      return Centred(boss_select_panel_.Render());
    case kBossFight:
    case kBossAbort:
    case kBossClear:
      return RenderBossFight();
    case kBossAnalysis:
      return Centred(controller_.boss_analysis_panel().Render());
    case kBossConfirm:
      return Overlay(Centred(boss_select_panel_.Render()), BossConfirmDialog());
    // Over the arena for a fight that ran out of time, and over the list for a
    // notice shown instead of a fight (no weapon, or a daily already done).
    // Whether a run is still held tells which.
    case kBossNotice:
      if (controller_.boss_run() != nullptr) {
        return RenderBossFight();
      }
      return Overlay(Centred(boss_select_panel_.Render()), NoticeDialog());
    // kShopMenu draws the same thing: the menu is anchored to a row of the
    // list, so the panel draws it.
    case kShop:
    case kShopMenu:
      return Centred(shop_panel_.Render());
    case kShopInspect:
      return RenderShopInspect();
    case kShopBuy:
      return Overlay(Centred(shop_panel_.Render()), buy_panel_.Render());
    case kStarForce:
      return RenderStarForce();
    case kCubing:
      return RenderCubing();
    case kStarForceResult:
      return RenderStarForceResult();
    case kTraceRecover:
      return RenderTraceRecover();
    case kTraceRecoverResult:
      return Centred(trace_recover_panel_.RenderResult(
          controller_.trace_recovery_result()));
    case kAllStats:
      return Centred(all_stats_panel_.Render());
    case kHyperReset:
      return OverMain(HyperResetDialog());
    case kVMatrixReset:
      return OverMain(VMatrixResetDialog());
    case kPresetMove:
      return OverMain(PresetMoveDialog());
    case kAbilityReroll:
      return OverMain(AbilityRerollDialog());
    case kBuffBuy:
      return OverMain(BuffBuyDialog());
    case kBuffInfo:
      return Centred(buff_info_panel_.Render());
    case kHyperStatInspect:
      hyper_stat_inspect_panel_.SetStat(controller_.hyper_inspect_field(),
                                        controller_.hyper_inspect_level(),
                                        controller_.hyper_inspect_max_level());
      return Centred(hyper_stat_inspect_panel_.Render());
    case kJobInspect:
      return RenderJobInspect();
    case kSkillInspect:
      skill_inspect_panel_.SetSkill(&controller_.skill_inspect_skill(),
                                    controller_.skill_inspect_level(),
                                    controller_.skill_inspect_bonus());
      skill_inspect_panel_.SetMaxRows(ftxui::Terminal::Size().dimy);
      skill_inspect_panel_.SetWidthBounds(0, ftxui::Terminal::Size().dimx);
      return Centred(skill_inspect_panel_.Render());
    case kInspect:
    case kItemInspect:
      return RenderInspect();
    case kScrollSelect:
    case kScrollResult:
      return RenderScroll();
    // The main view and everything drawn on it: the corner menus and the
    // panels' own popups. A screen with no case of its own ends up here without
    // any error, which looks like a screen that never opened, so add its case
    // along with the screen.
    default:
      return RenderMain();
  }
}

// The menu the open screen shows over the layout. Every menu is anchored one
// row above its cursor's row, so the highlighted entry sits beside what it acts
// on; a menu must never cover what it is about.
ftxui::Element Tui::OpenMenu(const MainWidths& widths) {
  if (controller_.screen() == kSkillMenu) {
    // Past the widest skill name, so it covers the level column the way the
    // bag's menu covers an item's stats. It stays inside the character panel,
    // whose column narrows with the terminal; a narrow panel covers the end of
    // a name instead of spilling over the bag.
    constexpr int kSkillMenuCol = 38;
    const ItemMenu& menu = controller_.skill_menu();
    int col = std::max(0, std::min(kSkillMenuCol, widths.left - menu.Width()));
    return Floating(
        menu.Render(std::max(0, char_panel_.skill_cursor_row() - 1), col));
  }
  if (controller_.screen() == kPresetMenu) {
    // Past the last tab rather than over them, so the row the menu is about
    // stays readable behind it. It stays inside the panel, like the others.
    constexpr int kPresetMenuCol = 14;
    const ItemMenu& menu = controller_.preset_menu();
    int col = std::max(0, std::min(kPresetMenuCol, widths.left - menu.Width()));
    return Floating(menu.Render(std::max(0, char_panel_.preset_row()), col));
  }
  if (controller_.screen() == kJobMenu) {
    constexpr int kJobMenuCol = 14;
    return Floating(controller_.job_menu().Render(
        std::max(0, char_panel_.job_cursor_row() - 1), kJobMenuCol));
  }
  if (controller_.screen() == kBuffMenu) {
    // Past the widest buff name, so the menu covers the columns after it rather
    // than the name. It stays inside the panel, like the skill menu.
    constexpr int kBuffMenuCol = 26;
    const ItemMenu& menu = controller_.buff_menu();
    int col = std::max(0, std::min(kBuffMenuCol, widths.left - menu.Width()));
    return Floating(
        menu.Render(std::max(0, char_panel_.buff_cursor_row() - 1), col));
  }
  if (controller_.screen() != kItemMenu) {
    return nullptr;
  }
  // The row comes from the panel rather than being counted from the header
  // rows, because an offset added to the selected index stops matching the
  // screen row as soon as the bag scrolls.
  bool on_equip = panel_focus_ == kEquipPanel;
  int cursor_row =
      on_equip ? equip_panel_.cursor_row() : inventory_panel_.cursor_row();
  ItemMenu& menu = on_equip                        ? equip_panel_.menu()
                   : inventory_panel_.on_tab_bar() ? inventory_panel_.tab_menu()
                                                   : inventory_panel_.menu();
  // Past the panel border, cursor, name and slot columns and separators, so the
  // menu covers stats rather than item names. An expanded panel reports where
  // its own columns end and the menu hangs from its own left border.
  int left = controller_.expanded_panel() != kNoPanel ? 0 : widths.left;
  int col;
  if (!on_equip && inventory_panel_.on_tab_bar()) {
    // A tab menu is about the whole tab, not one row's stats, so it hangs under
    // the tab bar at the panel's left edge.
    col = left + 3;
  } else if (controller_.expanded_panel() != kNoPanel) {
    col =
        on_equip ? equip_panel_.menu_column() : inventory_panel_.menu_column();
  } else {
    col = left + 1 + 2 + 18 + 2 + 10 + 2;
  }
  return Floating(menu.Render(std::max(0, cursor_row - 1), col));
}

// A panel expanded to the whole terminal. It replaces the main view rather than
// being its own screen, so the item menu still appears over it and every dialog
// the panel opens works unchanged.
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
  // No left column in front of it, so the menu hangs from the panel's own
  // columns; see OpenMenu.
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
  // The character and combat panels share the left column, with combat pinned
  // to the bottom. Without a height limit, the character panel takes what it
  // wants and the mob bars fall off a short terminal. One row is for the EXP
  // bar.
  int rows = ftxui::Terminal::Size().dimy - 1 - combat_panel_.Height();
  char_panel_.SetMaxRows(rows);
  // Both columns follow the terminal's width, and each panel is told its width
  // before drawing: a panel sizes itself to its column, never the other way
  // round.
  MainWidths widths =
      ComputeMainWidths(ftxui::Terminal::Size().dimx,
                        controller_.PanelVisible(kEquipPanel) ||
                            controller_.PanelVisible(kInventoryPanel));
  char_panel_.SetWidth(widths.left);
  combat_panel_.SetWidth(widths.left);
  equip_panel_.SetWidth(widths.right);
  inventory_panel_.SetWidth(widths.right);
  // A panel the character hasn't unlocked isn't drawn at all, and the layout
  // closes up around it. It is skipped rather than drawn and hidden, since it
  // has nothing to show yet.
  ftxui::Element equipped = nullptr;
  if (controller_.PanelVisible(kEquipPanel)) {
    equipped = equip_component_->Render();
  }
  ftxui::Element inventory = nullptr;
  if (controller_.PanelVisible(kInventoryPanel)) {
    inventory = inventory_component_->Render();
  }
  // The corner shows the tip or the menu, never both: the menu unlocks at the
  // level the tip goes away.
  ftxui::Element corner = nullptr;
  if (HotkeysTipVisible(state_.character, state_.account)) {
    corner = HotkeysPanel();
  } else if (controller_.PanelVisible(kMenuPanel)) {
    corner = menu_component_->Render();
  }
  ftxui::Element layout =
      MainLayout(widths, char_panel_.Render(), combat_component_->Render(),
                 std::move(equipped), std::move(inventory), std::move(corner),
                 ExpBar(state_.character.proto()));
  // Drawn as an overlay so a menu opened near the bottom of a panel hangs over
  // the edge instead of being cut off at the terminal's edge.
  ftxui::Element menu = OpenMenu(widths);
  if (menu == nullptr) {
    return layout;
  }
  return ftxui::dbox({layout, std::move(menu)});
}

void Tui::Tick() {
  std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
  std::chrono::duration<double> elapsed = now - last_combat_update_;
  last_combat_update_ = now;
  // Updated every tick, not only when saving, so the total is always current
  // for anything that shows it.
  state_.playtime_seconds += elapsed.count();
  // Before the fight, so the run uses the server's latest party roster rather
  // than a tick-old one, and a fight the party has started opens on the tick it
  // arrives.
  if (multiplayer_ != nullptr) {
    multiplayer_->Advance(state_);
  }
  controller_.AdvanceParty();
  // The map only farms while the player is on it. EXP from a fight they can't
  // see would be odd, meso draining during a trade would change a deal nobody
  // agreed to, and kills on the character select would belong to nobody. The
  // analysis stops along with farming.
  if (controller_.in_boss_fight()) {
    controller_.AdvanceBossRun(elapsed.count());
  } else if (!controller_.OnTradeScreen() && !controller_.OnCharacterSelect()) {
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
  // Counted down before checking for a new level, so a level-up on this tick
  // gets its full four seconds rather than one tick less.
  celebration_.Advance(elapsed.count());
  controller_.AdvanceNotification(elapsed.count());
  celebration_.Visit(FocusedPanel());
  NoticeProgress();
  // Last, so dying takes priority over anything else found this tick. A level
  // earned before dying still counts and its gold stays lit, but the player
  // needs to be told where they are now.
  if (combat_sim_.view().died_this_step) {
    celebration_.BeginDeath();
  }
  UpdateMusic();
  // Last of all, so the ticker's next sleep uses the step for where this tick
  // left the player.
  in_boss_fight_ = controller_.in_boss_fight();
}

void Tui::StartPlayingCharacter() {
  // The fight holds the previous character's HP, buffs and party roster, none
  // of which belongs to the new character.
  combat_sim_ = CombatSim();
  // Reset from the new character, so switching to a level 210 character isn't
  // treated as a climb of 209 levels.
  progress_watcher_ = ProgressWatcher(state_.character.proto());
  celebration_ = Celebration();
  // A measurement is of one character farming one map, so it can't carry over
  // to another character.
  analysis_ = BattleAnalysis();
  last_combat_update_ = std::chrono::steady_clock::now();
}

std::string Tui::NormalTrack() const {
  if (const BossRun* run = controller_.boss_run(); run != nullptr) {
    return std::string(run->bgm());
  }
  auto map = state_.maps.find(state_.current_map);
  return map == state_.maps.end() ? "" : std::string(map->second.bgm());
}

void Tui::UpdateMusic() {
  if (!music_player_.ready()) {
    return;
  }
  // A boss fight takes over the screen and the volume; the map underneath isn't
  // where the player is. The volume follows the fight whatever is playing, so a
  // jukebox track during a boss is still at boss volume.
  music_player_.SetVolume(controller_.boss_run() != nullptr
                              ? state_.account.boss_bgm_volume()
                              : state_.account.map_bgm_volume());
  music_director_.Update(state_.account.jukebox_mode(), NormalTrack());
}

Panel Tui::FocusedPanel() const {
  if (controller_.screen() != kMain) {
    return kNoPanel;
  }
  return static_cast<Panel>(panel_focus_);
}

void Tui::NoticeProgress() {
  // A level earned from a boss waits until the player leaves: otherwise its
  // card would cover the clear card. The watcher isn't checked, so the level is
  // still there to notice on the way out.
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
  // A player who has looked at the card is done with it. The key still does its
  // normal job, since dismissing the card is a side effect rather than
  // consuming the key. Custom is the ticker's redraw event, not the player.
  if (celebration_.card_visible() && event != ftxui::Event::Custom) {
    celebration_.Dismiss();
  }
  // The notification needs a key press as well as its four seconds, and the
  // ticker's redraw isn't a key press.
  if (event != ftxui::Event::Custom) {
    controller_.TouchNotification();
  }
  // The All Stats screen's Farm/Boss row belongs to the panel, which lives here
  // rather than on the controller. Keys it doesn't handle, such as the ones
  // that close the screen, work as usual.
  if (controller_.screen() == kAllStats && all_stats_panel_.OnEvent(event)) {
    return true;
  }
  bool handled = controller_.OnEvent(event);
  // Before NoticeProgress, which would otherwise compare the new character's
  // level against the previous character's and raise a card for the difference.
  if (controller_.TakeCharacterSwitch()) {
    StartPlayingCharacter();
  }
  // After the event, not before: the key may be the Tab that moved the player
  // onto a panel waiting to be visited, and its gold should clear in the frame
  // this event draws, not the next one.
  celebration_.Visit(FocusedPanel());
  // Advancement happens during events rather than ticks.
  NoticeProgress();
  return handled;
}

}  // namespace ms
