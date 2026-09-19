/* TuiController owns the screen-state machine for the TUI. It handles
 * keyboard events and drives transitions between screens. Tui holds a
 * TuiController and delegates event handling; tests can construct
 * TuiController directly without the ftxui event loop.
 *
 * panel_focus is owned by the caller and shared with the panel components
 * so Container::Tab can read it; TuiController mutates it on Tab.
 */
#ifndef MS_SRC_FRONTEND_TUI_CONTROLLER_H_
#define MS_SRC_FRONTEND_TUI_CONTROLLER_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "ftxui/component/event.hpp"
#include "src/combat/battle_analysis.h"
#include "src/combat/boss_run.h"
#include "src/combat/offline.h"
#include "src/frontend/item_ref.h"
#include "src/frontend/keybinds.h"
#include "src/frontend/notification_box.h"
#include "src/frontend/panels/character_panel.h"
#include "src/frontend/panels/equipped_panel.h"
#include "src/frontend/panels/inventory_panel.h"
#include "src/frontend/panels/menu_panel.h"
#include "src/frontend/screens/boss_select_panel.h"
#include "src/frontend/screens/buff_info_panel.h"
#include "src/frontend/screens/buy_panel.h"
#include "src/frontend/screens/character_select_panel.h"
#include "src/frontend/screens/cube_panel.h"
#include "src/frontend/screens/dailies_panel.h"
#include "src/frontend/screens/hammer_panel.h"
#include "src/frontend/screens/inspect_panel.h"
#include "src/frontend/screens/job_inspect_panel.h"
#include "src/frontend/screens/keybinds_panel.h"
#include "src/frontend/screens/map_select_panel.h"
#include "src/frontend/screens/mob_inspect_panel.h"
#include "src/frontend/screens/multi_sell_panel.h"
#include "src/frontend/screens/options_panel.h"
#include "src/frontend/screens/party_select_panel.h"
#include "src/frontend/screens/player_inspect_panel.h"
#include "src/frontend/screens/player_list_panel.h"
#include "src/frontend/screens/scroll_panel.h"
#include "src/frontend/screens/sell_equip_panel.h"
#include "src/frontend/screens/sell_panel.h"
#include "src/frontend/screens/shop_panel.h"
#include "src/frontend/screens/skill_inspect_panel.h"
#include "src/frontend/screens/star_force_panel.h"
#include "src/frontend/screens/symbol_combine_panel.h"
#include "src/frontend/screens/symbol_level_panel.h"
#include "src/frontend/screens/trace_recover_panel.h"
#include "src/frontend/screens/trade_panel.h"
#include "src/frontend/types.h"
#include "src/frontend/widgets/amount_selector.h"
#include "src/frontend/widgets/confirm_prompt.h"
#include "src/frontend/widgets/continue_prompt.h"
#include "src/frontend/widgets/item_menu.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/multiplayer/party_fight.h"
#include "src/multiplayer/session.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/skill.pb.h"
#include "src/roster.h"

namespace ms {

// What a party question is asking about, so answering Yes knows what to do.
enum class PartyAsk { kNone, kKick, kPromote, kLeave };

// An absence shorter than this raises no card. A player who restarted the game
// a minute after closing it does not need to be told what that minute paid.
inline constexpr double kOfflineNoticeSeconds = 60.0;

// Every panel the controller drives, in one piece. A struct of references
// rather than 25 constructor parameters, which had to be written out three
// times in the same order. `Tui` owns the panels; this must not outlive it.
struct Screens {
  CharacterPanel& char_panel;
  EquippedPanel& equip_panel;
  InventoryPanel& inventory_panel;
  ScrollPanel& scroll_panel;
  // The item card, and the preview card beside it on kTraceRecover and
  // kStarForce.
  InspectPanel& inspect_panel;
  InspectPanel& preview_inspect_panel;
  StarForcePanel& star_force_panel;
  CubePanel& cube_panel;
  TraceRecoverPanel& trace_recover_panel;
  SellPanel& sell_panel;
  SellEquipPanel& sell_equip_panel;
  MultiSellPanel& multi_sell_panel;
  MapSelectPanel& map_select_panel;
  MobInspectPanel& mob_inspect_panel;
  BossSelectPanel& boss_select_panel;
  PartySelectPanel& party_select_panel;
  PlayerListPanel& player_list_panel;
  TradePanel& trade_panel;
  PlayerInspectPanel& player_inspect_panel;
  // The card for an item another player is wearing, which takes the same keys
  // as the player's own.
  InspectPanel& player_item_panel;
  ShopPanel& shop_panel;
  BuyPanel& buy_panel;
  JobInspectPanel& job_inspect_panel;
  SkillInspectPanel& skill_inspect_panel;
  BuffInfoPanel& buff_info_panel;
  MenuPanel& menu_panel;
  KeybindsPanel& keybinds_panel;
  OptionsPanel& options_panel;
};

class TuiController {
 public:
  // panel_focus is a reference shared with panel components and
  // Container::Tab; the controller mutates it as focus changes.
  TuiController(GameState& state, Screens screens, BattleAnalysis& analysis,
                KeyMap& keys, int& panel_focus,
                MultiplayerSession* multiplayer = nullptr);

  // The [Expand]/[Close] button on `panel`. An expanded panel is the MAIN
  // VIEW rather than a screen of its own, so menus and dialogs float over it
  // unchanged. Pressing it elsewhere moves the expansion there.
  void ToggleExpanded(int panel);
  // The panel opened up over the whole screen, or kNoPanel.
  int expanded_panel() const {
    return expanded_panel_;
  }

  // Open the equip or bag context menu. Called from MakeComponent callbacks.
  void OpenEquipMenu();
  // Enter in the bag: the context menu on an item, or the shop when the Shop
  // tab is the one showing.
  void OpenInventoryMenu();
  // Float the AP-allocation amount entry over the main view, seeded to spend up
  // to all available AP on `field` (defaulting to the max).
  void OpenApAllocate(StatField field);
  // Floats the skill-learning amount entry, seeded to the most points `skill`
  // can still take.
  void OpenSkillLearn(const Skill& skill);
  // The menu Enter on a skill's name raises. The middle entry is offered only
  // by a toggle skill, and dims until it is bought.
  void OpenSkillMenu(const Skill& skill);
  // Open the skill's inspect screen. Copies the skill, as the learn dialog
  // does, so nothing downstream depends on the catalog outliving the screen.
  void OpenSkillInspect(const Skill& skill);
  // Every stat the character has, on a screen of its own.
  void OpenAllStats();

  // The four screens the Inspect panel raises. They are the player's own
  // cards over somebody else's numbers, so they read from the member being
  // inspected and close back onto the screen that raised them.
  void OpenPlayerSkillInspect(const Skill& skill);
  void OpenPlayerHyperStatInspect(HyperStatField field);
  void OpenPlayerAllStats();
  void OpenPlayerItemInspect();
  // Spends a point on `field` and gives the last one back. No dialog on
  // either: the row's [-] is the way out of a [+].
  void RaiseHyperStat(HyperStatField field, StatPreset preset);
  void LowerHyperStat(HyperStatField field, StatPreset preset);
  // The one question left on the tab: emptying an allocation whole.
  void OpenHyperReset(StatPreset preset);

  // The same question for the V Matrix: every node back to nothing, every
  // point back in the pool.
  void OpenVMatrixReset();
  const ConfirmPrompt& v_matrix_reset_prompt() const {
    return v_matrix_reset_prompt_;
  }

  // Holds or frees the Inner Ability line at `index` of `preset`, whichever it
  // is not now. No screen: the row's own lock says what happened.
  void ToggleAbilityLock(int index, StatPreset preset);
  // Asks before rerolling `preset`, which is where the honor is spent.
  void OpenAbilityReroll(StatPreset preset);

  // Flips `type`. No screen and no question: the row's switch says what
  // happened, and nothing is spent until the buff procs.
  void ToggleConsumable(ConsumableType type);
  // Float the buff's context menu over the main view: read it, buy it outright,
  // or walk away.
  void OpenBuffMenu(ConsumableType type);
  // Asks before buying `type` outright, opening on Cancel: it costs hundreds
  // of millions. A short purse still opens the question, price in red and
  // [Confirm] greyed, rather than leaving the player guessing at it.
  void OpenBuffBuy(ConsumableType type);
  // The buff every one of the three is about.
  ConsumableType buff_type() const {
    return buff_type_;
  }
  // The buff menu, for the overlay Tui floats beside the buff's row.
  const ItemMenu& buff_menu() const {
    return buff_menu_;
  }
  const ConfirmPrompt& buff_buy_prompt() const {
    return buff_buy_prompt_;
  }
  // Whether the purse covers what the open question asks. The dialog greys
  // [Confirm] with it, and the buy is held to the same check.
  bool buff_buy_affordable() const;
  // What the permanent price is, for the dialog to state.
  int64_t buff_buy_price() const;

  // The card Enter on a stat's name opens. Never gated: a stat the character
  // is too low for is the one they most want to read about.
  void OpenHyperStatInspect(HyperStatField field, StatPreset preset);
  HyperStatField hyper_inspect_field() const {
    return hyper_field_;
  }
  // Read live rather than captured, for the reason skill_inspect_level() is:
  // a point spent and the stat inspected again shows the level it is at.
  int hyper_inspect_level() const;
  int hyper_inspect_max_level() const;
  const ConfirmPrompt& hyper_reset_prompt() const {
    return hyper_reset_prompt_;
  }
  // What the reset dialog asks, which names the allocation being emptied.
  std::string hyper_reset_question() const;

  const ConfirmPrompt& ability_reroll_prompt() const {
    return ability_reroll_prompt_;
  }
  // The lines the open question would throw away: everything the allocation is
  // not holding, which is all the dialog lists.
  std::vector<AbilityLine> ability_reroll_lines() const;
  // Whether the last reroll carried the ability up a rank, which lights the
  // panel gold. True until the player's next key.
  bool ability_rank_up() const {
    return ability_rank_up_;
  }
  // Floats the job's context menu. Enter in the Advance tab lands HERE rather
  // than on the confirmation: a job should be readable before it is chosen.
  void OpenJobMenu(Job job);

  // Enter on the preset row: the menu that puts a preset in use or moves one.
  // `slot` is the chip the cursor was on, which every entry acts on.
  void OpenPresetMenu(PresetKind kind, StatPreset slot);
  // Float the job-advancement confirmation over the main view. The prompt opens
  // on Cancel: the choice cannot be taken back.
  void OpenJobAdvance(Job job);
  // Open the map selection screen, on the map being farmed.
  void OpenMapSelect();
  // Enter on an entry of the corner menu. Boss opens the boss screen and
  // clears the entry's gold; Settings opens its box over the corner.
  void OpenMenuEntry(MenuEntry entry);

  // Keeps the party screen and its fight in step with the connection: the
  // lobby as it stands, whatever the server has said, the fight screen the
  // moment the party is let in, and the way out if the connection goes. Every
  // tick, before the fight is stepped.
  void AdvanceParty();

  // Runs the gold box's clock down, and records that the player has pressed a
  // key. Both belong to Tui: it owns the frame clock and sees every key.
  void AdvanceNotification(double elapsed_seconds);
  void TouchNotification();

  // The word from the server, floated over whatever is on screen: a refusal
  // (drawn red), news of the party, or the connection going away.
  const ContinuePrompt& party_notice_prompt() const {
    return party_notice_prompt_;
  }
  const std::string& party_notice() const {
    return party_notice_;
  }
  bool party_notice_is_refusal() const {
    return party_notice_is_refusal_;
  }
  // The item the trade screen's card is drawn from, one of which is null.
  const EquipTabItem* trade_inspect_equip() const {
    return trade_inspect_equip_.get();
  }
  const ItemPrototype* trade_inspect_stack() const {
    return trade_inspect_stack_;
  }
  // The place in the bag of the stack the trade's amount overlay is putting
  // up, or -1 while it is not open on one.
  int trade_stack() const {
    return trade_stack_;
  }
  // Whether this player has confirmed and is waiting on the other.
  bool trade_waiting() const;
  // Whether the player is anywhere on the trade screen. The map is not farmed
  // while they are: an offer whose purse drained under it, or a drop landing
  // between an acceptance and the exchange, is a trade nobody agreed to.
  bool OnTradeScreen() const;
  // Whether something happened that must not wait for the autosave clock: a
  // trade going through, which is the one point where value crosses from one
  // save file into another. Cleared by the taking.
  bool TakeSaveRequest();
  // The finalize dialog's cursor, for the row it draws.
  const ConfirmPrompt& trade_prompt() const {
    return trade_prompt_;
  }
  const ConfirmPrompt& trade_leave_prompt() const {
    return trade_leave_prompt_;
  }
  // Which currency the trade overlay is putting up, and how much.
  TradeCurrency trade_currency() const {
    return trade_currency_;
  }
  const AmountSelector& trade_selector() const {
    return trade_selector_;
  }
  // The gold box in the corner, which outlives whatever screen raised it.
  const NotificationBox& notification() const {
    return notification_;
  }
  // The question a party action asks before it is taken, and what it asks.
  const ConfirmPrompt& party_prompt() const {
    return party_prompt_;
  }
  const std::string& party_prompt_question() const {
    return party_prompt_question_;
  }

  // True while a key must reach the game AS PRESSED rather than as the action
  // it is bound to -- a keybind slot or a text field waiting. Every action's
  // first key is locked, so Enter, Escape and the arrows still work.
  bool capturing_key() const;

  // The stat the pending AP allocation targets, and its amount selector, for
  // the dialog Tui floats over the main view.
  StatField ap_alloc_field() const {
    return ap_field_;
  }
  const AmountSelector& ap_selector() const {
    return ap_selector_;
  }
  // The skill the pending learn targets, and its amount selector, for the
  // dialog Tui floats over the main view.
  const Skill& skill_learn_skill() const {
    return skill_learn_;
  }
  const AmountSelector& sp_selector() const {
    return sp_selector_;
  }

  // The skill menu, for the overlay Tui floats beside the skill's row, and the
  // skill it was opened on.
  const ItemMenu& skill_menu() const {
    return skill_menu_;
  }
  const Skill& skill_menu_skill() const {
    return skill_menu_skill_;
  }

  // What kSkillInspect draws: the skill, the level points were spent to, and
  // the levels the book lends.
  const Skill& skill_inspect_skill() const {
    return skill_inspect_;
  }
  int skill_inspect_level() const;
  int skill_inspect_bonus() const;

  // What the pending advancement's dialog draws. The stage is the one ABOVE
  // where the character stands, an advancement being offered from below.
  Job job_advance_job() const {
    return job_advance_;
  }
  int job_advance_stage() const {
    return state_.character.proto().job_stage() + 1;
  }
  const ConfirmPrompt& job_advance_prompt() const {
    return job_advance_prompt_;
  }

  // The job menu, for the overlay Tui floats beside the job's row.
  const ItemMenu& job_menu() const {
    return job_menu_;
  }

  // The preset menu, floated beside its row, and what the Move popup behind it
  // needs. kNumStatPresets is the Cancel button under them.
  const ItemMenu& preset_menu() const {
    return preset_menu_;
  }
  PresetKind preset_kind() const {
    return preset_kind_;
  }
  StatPreset preset_slot() const {
    return preset_slot_;
  }
  int preset_move_row() const {
    return preset_move_row_;
  }

  // The Level Up and Combine dialogs for an Arcane Symbol. Owned rather than
  // handed in: neither carries game state, only what Reset was told.
  // The character select and the question Delete asks on it, for the
  // renderer: both are the controller's own, as the dailies card is.
  const CharacterSelectPanel& character_select_panel() const {
    return character_select_panel_;
  }
  const ConfirmPrompt& character_delete_prompt() const {
    return character_delete_prompt_;
  }
  // The screen the quit dialog was raised over, which cancelling goes back
  // to and which stays drawn behind it.
  Screen quit_return() const {
    return quit_return_;
  }
  // Whether the character select is up, which is farming's other stop: the
  // player is choosing who to be, and a map cannot be fought by somebody who
  // may be about to be swapped out.
  bool OnCharacterSelect() const {
    return screen_ == kCharacterSelect || screen_ == kCharacterMenu ||
           screen_ == kCharacterDelete;
  }
  // Whether a character has just been put into play, taken by the asking.
  // Tui owns the fight and the watcher, and both belong to whoever is being
  // played -- see Tui::StartPlayingCharacter.
  bool TakeCharacterSwitch();

  const DailiesPanel& dailies_panel() const {
    return dailies_panel_;
  }
  const SymbolLevelPanel& symbol_level_panel() const {
    return symbol_level_panel_;
  }
  const SymbolCombinePanel& symbol_combine_panel() const {
    return symbol_combine_panel_;
  }
  // The golden hammer's question, owned for the same reason.
  const HammerPanel& hammer_panel() const {
    return hammer_panel_;
  }

  // The prompt on the quit dialog, for the same reason.
  const ConfirmPrompt& quit_prompt() const {
    return quit_prompt_;
  }

  // The prompt asking whether to take the highlighted boss fight, and what it
  // is asking about.
  const ConfirmPrompt& boss_prompt() const {
    return boss_prompt_;
  }
  const std::string& boss_prompt_title() const {
    return boss_prompt_title_;
  }
  // Whether the fight being asked about would be taken as practice. Snapshot
  // with the title, so the question cannot change under the player.
  bool boss_prompt_practice() const {
    return boss_prompt_practice_;
  }
  // The one button every one-button screen is dismissed by -- a scroll or star
  // force result, and the notice that a fight is still on its reset.
  const ContinuePrompt& notice_prompt() const {
    return notice_prompt_;
  }
  // What that button says: "Continue" for a result the player reads on
  // through, "Close" for a notice that is the end of it.
  const std::string& notice_button() const {
    return notice_button_;
  }
  // What the notice says, a line at a time, and whether it is a refusal --
  // which is drawn in red, the colour of a reason the player fell short of.
  const std::vector<std::string>& notice_lines() const {
    return notice_lines_;
  }
  bool notice_is_refusal() const {
    return notice_is_refusal_;
  }
  // The fight in progress, or null when the player is not in one.
  const BossRun* boss_run() const {
    return boss_run_.get();
  }
  // The prompt asking whether to walk out of a fight.
  const ConfirmPrompt& boss_abort_prompt() const {
    return boss_abort_prompt_;
  }
  // The clear card: what was beaten, what it paid, and the button. Held here
  // rather than read off the run, which is gone by the time it is up.
  const std::string& boss_clear_title() const {
    return boss_clear_title_;
  }
  double boss_clear_seconds() const {
    return boss_clear_seconds_;
  }
  const BossReward& boss_clear_reward() const {
    return boss_clear_reward_;
  }
  const ContinuePrompt& boss_clear_prompt() const {
    return boss_clear_prompt_;
  }
  // What the player earned while the game was closed, and the button that
  // dismisses the card showing it.
  const OfflineReport& offline_report() const {
    return offline_report_;
  }
  const ContinuePrompt& offline_prompt() const {
    return offline_prompt_;
  }
  // Raises that card at launch, before the player has touched anything: they
  // should see what they were paid before they see the game. A report not
  // worth a card raises nothing.
  void OpenOfflineReport(OfflineReport report);

  // Steps the fight, records a clear, and takes the screen back once the
  // closing beat is up. Nothing while the leave prompt is up: the clock must
  // not run out while the player is deciding.
  void AdvanceBossRun(double elapsed_seconds);
  // Takes what walking into a fight costs in potions. Called by both doors
  // into a boss run -- the solo one and the party's.
  // Throws the switch at `option` on the boss screen's options row.
  void ToggleBossOption(int option);
  void ChargeBossEntry();
  // True while a fight owns the screen, which is when the map should not be
  // farmed: the player is somewhere else.
  bool in_boss_fight() const {
    return boss_run_ != nullptr;
  }

  // True once the quit dialog is confirmed. The controller does not own the
  // ftxui screen, so it raises this and leaves the leaving to Tui.
  bool quit_requested() const {
    return quit_requested_;
  }

  // On a screen with a card beside something else, which the arrows reach. Tab
  // moves between them and the holder lights its title.
  bool right_card_focused() const {
    return right_card_focused_;
  }

  // Returns true if the event was consumed.
  bool OnEvent(ftxui::Event event);

  Screen screen() const {
    return screen_;
  }
  // What the Multi-Sell screen has marked. The screen owns the basket; this is
  // how a caller reads it without reaching through to the panel.
  const SaleBasket& multi_sell_basket() const {
    return multi_sell_panel_.basket();
  }
  const ScrollResult& scroll_result() const {
    return scroll_result_;
  }
  const StarForceResult& star_force_result() const {
    return star_force_result_;
  }
  const TraceRecoveryResult& trace_recovery_result() const {
    return trace_recovery_result_;
  }
  // Returns the item being scrolled while in kScrollSelect or kScrollResult,
  // or nullptr otherwise.
  const EquipInstance* scroll_item() const;
  // The gear preset every comparison on an item's card reads: the open Gear
  // tab, which is also where Equip would put the item. With the autoswap off
  // that tab opens on the preset in use, so by default this is what the
  // character has on.
  StatPreset ComparisonPreset() const;
  // What the player already wears in the slot the inspected item would fill,
  // for the card drawn beside it. nullptr when there is nothing to compare.
  const EquipTabItem* inspect_comparison() const;
  const EquipTabItem* player_item_comparison() const;
  const EquipInstance* WornForComparison(const EquipPrototype& proto) const;
  // What putting `item` on would do to the player's combat power, for the
  // figure on its card. Empty when there is nothing to say: a slot this
  // character cannot fill, or an item already worn in ComparisonPreset().
  std::optional<int> CombatPowerDelta(const EquipTabItem* item) const;
  // The same for whichever item each screen has on it.
  std::optional<int> inspect_delta() const;
  std::optional<int> player_item_delta() const;
  // Returns the item being inspected while in kInspect, or nullptr otherwise.
  // May be an EquipTrace if the selected bag item was destroyed.
  const EquipTabItem* inspect_item() const;
  // The stack being inspected in kItemInspect, as a PROTOTYPE: what is on
  // screen is what the item is, not how many are held.
  const ItemPrototype* item_inspect_item() const;
  // Returns the item being star forced while in kStarForce, or nullptr
  // otherwise. Do not call in kStarForceResult (item may be destroyed).
  const EquipInstance* star_force_item() const;
  // The item the cubing screen is working on, live rather than cached: a cube
  // destroys nothing, so the lines the screen draws are the item's own.
  const EquipInstance* cube_item() const;
  // Returns the trace being recovered while in kTraceRecover, or nullptr.
  const EquipTabItem* trace_recover_item() const;

  // Whether `panel` is on screen for this character: the equipped panel and
  // the bag are handed over as they level, the other two are there from the
  // first frame. Asked by the layout AND by Tab, so the two cannot disagree.
  bool PanelVisible(int panel) const;

 private:
  // Moves focus off a panel that is not on screen. The game opens focused on
  // the equipped panel, which a level 1 character does not have yet.
  void EnsureFocusIsVisible();

  // Where the item under the cursor of the focused panel lives. The one place
  // that reads panel_focus_ to answer that question.
  ItemRef SelectedItem() const;

  bool OnMainViewEvent(ftxui::Event event);
  bool OnItemMenuEvent(ftxui::Event event);

  // The three families of screen an item menu opens, each seeding the panel it
  // hands the screen to. A seed that finds the item cannot take the screen
  // returns the one it opened instead.
  Screen SeedUpgradeScreen(Screen next);
  Screen SeedSaleScreen(Screen next);
  Screen SeedSymbolScreen(Screen next);
  // The keys every screen made of inspect cards takes, `back` being where it
  // leaves for. One handler because they are one screen to the player, reached
  // from the bag, the shelf or another player's sheet.
  bool OnCardEvent(ftxui::Event event, InspectPanel& panel, Screen back);
  bool OnInspectEvent(ftxui::Event event);
  bool OnScrollSelectEvent(ftxui::Event event);
  bool OnScrollResultEvent(ftxui::Event event);
  bool OnApAllocEvent(ftxui::Event event);
  bool OnSkillLearnEvent(ftxui::Event event);
  bool OnSkillMenuEvent(ftxui::Event event);
  bool OnSkillInspectEvent(ftxui::Event event);
  bool OnJobMenuEvent(ftxui::Event event);
  bool OnPresetMenuEvent(ftxui::Event event);
  bool OnPresetMoveEvent(ftxui::Event event);
  bool OnBuffMenuEvent(ftxui::Event event);
  bool OnBuffInfoEvent(ftxui::Event event);
  bool OnBuffBuyEvent(ftxui::Event event);
  bool OnJobInspectEvent(ftxui::Event event);
  bool OnJobAdvanceEvent(ftxui::Event event);
  bool OnQuitEvent(ftxui::Event event);
  bool OnCharacterSelectEvent(ftxui::Event event);
  bool OnCharacterMenuEvent(ftxui::Event event);
  bool OnCharacterDeleteEvent(ftxui::Event event);
  // What the character menu's entry under the cursor does.
  void TakeCharacterMenuEntry();
  // Back into the game with whoever is now in play: the save goes out, the
  // panels start where a session starts, and Tui is told to build the fight
  // again.
  void LeaveCharacterSelect();
  // Raises the quit dialog over whatever screen is up, which is how both
  // Escape and the character select's Quit button ask.
  void OpenQuit();
  bool OnStarForceEvent(ftxui::Event event);
  bool OnCubeEvent(ftxui::Event event);
  bool OnStarForceResultEvent(ftxui::Event event);
  bool OnHammerEvent(ftxui::Event event);
  bool OnTraceRecoverEvent(ftxui::Event event);
  bool OnTraceRecoverResultEvent(ftxui::Event event);
  bool OnSellEvent(ftxui::Event event);
  bool OnSellEquipEvent(ftxui::Event event);
  bool OnSymbolLevelEvent(ftxui::Event event);
  bool OnHyperResetEvent(ftxui::Event event);
  bool OnVMatrixResetEvent(ftxui::Event event);
  bool OnAbilityRerollEvent(ftxui::Event event);
  bool OnSymbolCombineEvent(ftxui::Event event);
  bool OnMultiSellEvent(ftxui::Event event);
  bool OnMapSelectEvent(ftxui::Event event);
  bool OnMapMenuEvent(ftxui::Event event);
  bool OnMobInspectEvent(ftxui::Event event);
  bool OnPlayerListEvent(ftxui::Event event);
  bool OnPlayerMenuEvent(ftxui::Event event);
  bool OnTradeEvent(ftxui::Event event);
  bool OnTradeMenuEvent(ftxui::Event event);
  bool OnTradeConfirmEvent(ftxui::Event event);
  bool OnTradeLeaveEvent(ftxui::Event event);
  bool OnTradeItemAmountEvent(ftxui::Event event);
  bool OnTradeAmountEvent(ftxui::Event event);
  bool OnPartySelectEvent(ftxui::Event event);
  bool OnPartyMenuEvent(ftxui::Event event);
  bool OnPlayerInspectEvent(ftxui::Event event);
  bool OnPlayerAllStatsEvent(ftxui::Event event);
  // Whoever the open skill or Hyper Stat card is about -- see
  // card_from_inspect_.
  const CharacterInstance& card_character() const;
  bool OnPlayerItemInspectEvent(ftxui::Event event);
  bool OnPartyConfirmEvent(ftxui::Event event);
  // Opens the inspect screen on the member playing under `account_id`. Does
  // nothing for a member who has gone since the menu was raised.
  // Opens the party screen and the Players screen, each after checking that
  // there is a connection to draw.
  // Closes whichever of the two the player is on, back to the box that
  // opened it.
  void LeaveMultiplayerScreen();
  void OpenPartySelect();
  void OpenPlayerList();
  // Whether the connection is up. Raises the notice, and asks for a fresh
  // attempt, when it is not.
  bool Connected();
  // Opens the Inspect screen on a party member, whose sheet the party state
  // already carries.
  void OpenPlayerInspect(const std::string& account_id);
  // Asks `account_id` to trade, from either menu. The trade screen opens when
  // the server answers, so nothing here says where the player goes next.
  void AskToTrade(const std::string& account_id);
  // Puts a trade that went through into the bag: what was put up leaves, what
  // the other side put up arrives, and the screen closes back to the list.
  void ApplyCompletedTrade(const TradeOffer& received);
  // Raises the finalize dialog on the second acceptance and takes it down
  // when either is withdrawn.
  void AdvanceTradeConfirm(const TradeState& trade);
  // Opens and closes the trade screen as the server's trade comes and goes. A
  // trade that ends under the player takes them back where they opened it
  // from.
  void AdvanceTrade(const MultiplayerSnapshot& lobby);
  // Raises the amount overlay on the currency the cursor is on.
  void OpenTradeAmount();
  // Puts up what that overlay was left on.
  void PutUpTradeAmount();
  // Tells the server what is on this player's side of the table, whole.
  void SendTradeOffer();
  // Enter on a row of any of the three windows.
  void OpenTradeMenu();
  // Enter on the Accept button, which is a toggle. Accepting is refused with
  // a notice when the bag could not hold what is on their side of the table:
  // the one moment the question can be asked, since a table that changes
  // under an acceptance takes it back with it.
  void ToggleTradeAccept();
  // Offer on a bag row: an equip goes up as it stands, a stack through the
  // amount overlay. Refused with a notice once the table holds its eight.
  void OfferFromBag();
  void OpenTradeItemAmount(int stack);
  void PutUpTradeItemAmount();
  // Inspect on a row of any window. Theirs is built from the wire: nothing in
  // this client's bag is the item they are holding up.
  void OpenTradeInspect();
  // Walks out, which ends the trade for both.
  void LeaveTrade();
  // Asks for `account_id`'s sheet and opens the Inspect screen once it lands.
  // A player off the roster is not in any party, so their sheet has to be
  // fetched before there is anything to draw.
  void WatchForInspect(const std::string& account_id);
  // Whether the player the Players list is waiting on has arrived, and the
  // screen it opens. Nothing while no watch is pending.
  void AdvanceWatch(const MultiplayerSnapshot& lobby);
  // Stops the watch the Players list started, if there is one.
  void StopWatching();
  // Keeps the inspect screen on what the lobby last said, and turns the
  // player out of it when the member they are reading leaves.
  void RefreshPlayerInspect(const MultiplayerSnapshot& lobby);
  // Does what the cursor is on, which is either an ask sent straight to the
  // server or a question raised first.
  void TakePartyAction(PartyAction action);
  // Raises `question` over the party screen; PartyConfirmed() is what a Yes
  // runs.
  void AskAboutParty(PartyAsk ask, const std::string& question);
  void PartyConfirmed();
  // Floats `message` over whatever is on screen. A refusal is drawn in red.
  void RaisePartyNotice(const std::string& message, bool refusal);
  // Takes what the server has said about the party's fight and stands the
  // player in one that has just begun.
  void AdvancePartyFight(const MultiplayerSnapshot& lobby);
  // Opens the fight screen on the fight the party has been let into. Whatever
  // the player was doing, they are in it now.
  void OpenPartyFight(const MultiplayerSnapshot& lobby);
  // Rebuilds the party into the GameState, so what their skills hold over this
  // character is folded into its stats. See GameState::party.
  void SeatParty(const MultiplayerSnapshot& lobby);
  // Whether the fight on screen is the party's rather than one taken alone.
  bool in_party_fight() const;
  // Lets go of the finished run, and of the fight behind it.
  void DropBossRun();
  // The lobby as it stands, or nothing at all for a game played alone.
  MultiplayerSnapshot Lobby() const;
  bool OnBossSelectEvent(ftxui::Event event);
  bool OnBossConfirmEvent(ftxui::Event event);
  bool OnBossNoticeEvent(ftxui::Event event);
  // Raises a one-button screen with its prompt open, so every screen dismissed
  // by [Continue] is opened the one way.
  void OpenNotice(Screen screen);
  // The same for a screen that is nothing but a message: `lines` is what it
  // says, a refusal is drawn in red, and `button` is what the one button says.
  void OpenNotice(Screen screen, std::vector<std::string> lines, bool refusal,
                  const std::string& button);
  // A one-sentence notice split to read evenly. USE THIS for any notice naming
  // something: a short name and a long remainder read lopsided otherwise.
  void OpenSentenceNotice(Screen screen, const std::string& sentence,
                          bool refusal, const std::string& button);
  // Enter on the menu's Dailies entry: the claim, or the notice that today's
  // has been taken already.
  void OpenDailies();
  bool OnDailiesEvent(ftxui::Event event);
  bool OnDailiesNoticeEvent(ftxui::Event event);
  bool OnBossFightEvent(ftxui::Event event);
  bool OnBossAbortEvent(ftxui::Event event);
  bool OnBossClearEvent(ftxui::Event event);
  bool OnOfflineEvent(ftxui::Event event);
  // Drops the finished run and goes back to the fight list. What every panel a
  // fight ends on is dismissed by.
  void LeaveBossRun();
  bool OnMenuBoxEvent(ftxui::Event event);
  // Enter on a row of the open box: whatever that entry of that box leads to.
  void OpenBoxEntry();
  // Start and Stop toggle the tool; View raises its overlay.
  void OpenAnalysisEntry(AnalysisEntry entry);
  bool OnAnalysisEvent(ftxui::Event event);
  bool OnKeybindsEvent(ftxui::Event event);
  bool OnOptionsEvent(ftxui::Event event);
  // Puts the captured key in the waiting slot, or says why it could not.
  // Ignores what is not a key, so the slot goes on waiting.
  void TakeCapturedKey(const ftxui::Event& key);
  // Leaves the Keybinds screen for the box it was opened from.
  void LeaveKeybinds();
  void LeaveOptions();
  bool OnShopEvent(ftxui::Event event);
  bool OnShopMenuEvent(ftxui::Event event);
  bool OnShopInspectEvent(ftxui::Event event);
  // Puts every inspect card back at its top, with the left half of the screen
  // holding the arrows. Called as each such screen opens.
  void OpenInspectCards();
  bool OnShopBuyEvent(ftxui::Event event);
  // Seeds the buy dialog for a row of the buy-back shelf, which is priced and
  // bounded by the sale rather than by what the shop stocks.
  void OpenBuyBackDialog(const BuyBackEntry& entry);
  // Spends what a confirmed buy dialog agreed to, on whichever shelf it was
  // opened over.
  void BuyWhatTheDialogAgreedTo();

  GameState& state_;
  // Held only so focus arriving here can clear the Advance tab's gold; the
  // panel drives itself otherwise.
  CharacterPanel& char_panel_;
  EquippedPanel& equip_panel_;
  InventoryPanel& inventory_panel_;
  ScrollPanel& scroll_panel_;
  // The item card, and the preview beside it on kTraceRecover and kStarForce.
  // Held so the arrows can scroll whichever is being read.
  InspectPanel& inspect_panel_;
  InspectPanel& preview_inspect_panel_;
  StarForcePanel& star_force_panel_;
  CubePanel& cube_panel_;
  TraceRecoverPanel& trace_recover_panel_;
  SellPanel& sell_panel_;
  SellEquipPanel& sell_equip_panel_;
  MultiSellPanel& multi_sell_panel_;
  MapSelectPanel& map_select_panel_;
  MobInspectPanel& mob_inspect_panel_;
  BossSelectPanel& boss_select_panel_;
  PartySelectPanel& party_select_panel_;
  PlayerListPanel& player_list_panel_;
  TradePanel& trade_panel_;
  PlayerInspectPanel& player_inspect_panel_;
  InspectPanel& player_item_panel_;
  // The member the inspect screen is reading, so the lobby's next word about
  // them lands on it.
  std::string inspect_account_;
  // Which list the open Inspect screen was reached from, which is where
  // Escape puts the player back.
  bool inspect_from_players_ = false;
  // The player the Players list has asked the server for and is waiting on.
  // Empty once their sheet has landed, or once they have gone.
  std::string inspect_pending_;
  // Whether the open skill or Hyper Stat card is reading a party member
  // rather than the player. The card is the same either way; whose levels it
  // states is not, and neither is the screen it closes onto.
  bool card_from_inspect_ = false;
  JobInspectPanel& job_inspect_panel_;
  SkillInspectPanel& skill_inspect_panel_;
  BuffInfoPanel& buff_info_panel_;
  MenuPanel& menu_panel_;
  KeybindsPanel& keybinds_panel_;
  OptionsPanel& options_panel_;
  // The measurement the Analysis entry starts and stops. Owned by the session,
  // not by the controller: it outlives every screen it is read from.
  BattleAnalysis& analysis_;
  KeyMap& keys_;
  ShopPanel& shop_panel_;
  BuyPanel& buy_panel_;
  // Catalog key of the item the buy dialog is open on, so the purchase reads
  // the prototype rather than trusting a pointer to outlive the screen.
  std::string buy_item_;
  // The shelf row the buy dialog was opened on, so a cursor that moved under
  // it cannot buy back a different sale.
  int buy_back_row_ = 0;
  int& panel_focus_;
  Screen screen_ = kMain;
  // The item the open modal was opened on, settled once when the player picks
  // it. ONE ref for every modal, one being open at a time; each accessor below
  // gates on screen_, which says whose it is.
  ItemRef subject_;
  // The bag row the open modal is about: recovery and the equip sale are
  // bag-only. One row, for the reason subject_ is one ref.
  int bag_row_ = 0;
  // See right_card_focused(). False on every screen that opens, so the arrows
  // start on the list or the card the player came in reading.
  bool right_card_focused_ = false;
  // See expanded_panel().
  int expanded_panel_ = kNoPanel;
  // The stack the Etc sale is open on, which is a row in that tab rather than
  // in the equip bag.
  int sell_index_ = 0;
  StatField ap_field_ = STAT_FIELD_UNSPECIFIED;
  AmountSelector ap_selector_;
  Skill skill_learn_;
  AmountSelector sp_selector_;
  Skill skill_inspect_;
  // Rebuilt on every open: the middle entry is a verb that reads Activate or
  // Deactivate by which way the switch is currently thrown.
  Skill skill_menu_skill_;
  ItemMenu skill_menu_{{"Inspect", "Activate", "Close"}};
  Job job_advance_ = JOB_UNSPECIFIED;
  ItemMenu job_menu_{{"Inspect", "Advance", "Close"}};
  // What the preset menu and its Move popup are about. Held rather than read
  // back off the panel, so a swap lands on the preset the menu named.
  PresetKind preset_kind_ = PresetKind::kHyperStats;
  StatPreset preset_slot_ = StatPreset::kFirst;
  ItemMenu preset_menu_{{"Use", "Move", "Close"}};
  // The Move popup's cursor: a preset, or kNumStatPresets for Cancel.
  int preset_move_row_ = 0;
  // The buff the menu, the card and the question are all about. Held so the
  // answer lands on the buff the question named, whatever the cursor did.
  ConsumableType buff_type_ = CONSUMABLE_TYPE_UNSPECIFIED;
  ItemMenu buff_menu_{{"Disable", "Inspect", "Buy Perm", "Close"}};
  ConfirmPrompt buff_buy_prompt_;
  DailiesPanel dailies_panel_;
  CharacterSelectPanel character_select_panel_;
  ConfirmPrompt character_delete_prompt_;
  // The slot the open Delete question is about, taken when it opens: the
  // cursor is free to be somewhere else by the time it is answered.
  int character_delete_slot_ = -1;
  // True from a switch until Tui has taken it.
  bool character_switched_ = false;
  // The screen the quit dialog was raised over, which cancelling goes back
  // to. The character select is the one screen it can be asked from that is
  // not the main view.
  Screen quit_return_ = kMain;
  SymbolLevelPanel symbol_level_panel_;
  ConfirmPrompt hyper_reset_prompt_;
  ConfirmPrompt v_matrix_reset_prompt_;
  // What the open Hyper Stat question is about. Held rather than read back off
  // the panel, so the answer lands on the stat the question named.
  HyperStatField hyper_field_ = HYPER_STAT_FIELD_UNSPECIFIED;
  StatPreset hyper_preset_ = StatPreset::kFirst;
  ConfirmPrompt ability_reroll_prompt_;
  // And which allocation the open Inner Ability question is about, kept apart
  // from the Hyper one above so neither answer can land on the other's.
  StatPreset ability_preset_ = StatPreset::kFirst;
  // See ability_rank_up(). Put out by OnEvent before it dispatches, so the
  // reroll that sets it keeps it and the key after it does not.
  bool ability_rank_up_ = false;
  SymbolCombinePanel symbol_combine_panel_;
  HammerPanel hammer_panel_;
  // The worn symbol the two symbol dialogs are asking about. Held so the
  // answer acts on what was asked, whatever the cursor did in the meantime.
  EquipSlot symbol_slot_ = EQUIP_SLOT_UNSPECIFIED;
  ConfirmPrompt job_advance_prompt_;
  ConfirmPrompt quit_prompt_;
  // The boss confirmation and the fight it asks about. The title is held, so
  // the dialog cannot change its question under the player.
  ConfirmPrompt boss_prompt_;
  std::string boss_prompt_title_;
  bool boss_prompt_practice_ = false;
  ContinuePrompt notice_prompt_;
  std::vector<std::string> notice_lines_;
  bool notice_is_refusal_ = false;
  std::string notice_button_;
  ConfirmPrompt boss_abort_prompt_;
  // The connection, or null for a game played alone.
  MultiplayerSession* multiplayer_ = nullptr;
  // The pending party action and who it is about, held so a cursor that moved
  // under the question cannot answer a different one.
  PartyAsk party_ask_ = PartyAsk::kNone;
  std::string party_target_;
  ConfirmPrompt party_prompt_;
  std::string party_prompt_question_;
  NotificationBox notification_;
  // The last gold box raised, so one arriving is raised once rather than on
  // every frame after it.
  int64_t notification_seen_ = 0;
  AmountSelector trade_selector_;
  // The finalize dialog. Which side is waiting is the trade's to say, so this
  // holds only the cursor.
  ConfirmPrompt trade_prompt_;
  // And the one asked before walking out, which ends the trade for both. Its
  // own rather than the finalize dialog's: two questions, two cursors.
  ConfirmPrompt trade_leave_prompt_;
  // Which currency the amount overlay is putting up, taken when it opens: the
  // cursor is free to be somewhere else by the time it is answered. The stack
  // overlay takes the place in the bag for the same reason.
  TradeCurrency trade_currency_ = TradeCurrency::kMeso;
  int trade_stack_ = -1;
  // What the trade screen is inspecting. Built rather than pointed at: their
  // half of the table is not in any bag this client holds, and one path for
  // both sides is one path to keep right.
  std::unique_ptr<EquipTabItem> trade_inspect_equip_;
  const ItemPrototype* trade_inspect_stack_ = nullptr;
  // The screen the trade was opened from, which walking out closes back to.
  Screen trade_return_ = kPlayerList;
  // The trade this player walked out of, so a state still in flight does not
  // stand the screen back up.
  std::string left_trade_id_;
  ContinuePrompt party_notice_prompt_;
  std::string party_notice_;
  bool party_notice_is_refusal_ = false;
  // The serial of the last notice raised, so one is shown once.
  int64_t party_notice_seen_ = 0;
  // The last trade payment this client has put in the bag, so one is never
  // applied twice.
  int64_t trade_paid_seen_ = 0;
  bool save_wanted_ = false;
  // What the clear card reads from, kept for as long as it is up.
  OfflineReport offline_report_;
  ContinuePrompt offline_prompt_;
  std::string boss_clear_title_;
  BossReward boss_clear_reward_;
  double boss_clear_seconds_ = 0.0;
  ContinuePrompt boss_clear_prompt_;
  // The fight in progress and the catalog entry it is against, so a clear is
  // recorded under the names the reset clock reads.
  std::unique_ptr<BossRun> boss_run_;
  std::string boss_run_key_;
  std::string boss_run_difficulty_;
  // The party's fight, which the run follows. Null for a game played alone.
  std::unique_ptr<PartyFightAuthority> party_fight_;
  bool quit_requested_ = false;
  ScrollResult scroll_result_;
  StarForceResult star_force_result_;
  TraceRecoveryResult trace_recovery_result_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_TUI_CONTROLLER_H_
