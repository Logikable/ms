/* TuiController owns the TUI's screen-state machine. It handles keyboard events
 * and moves between screens. Tui holds a TuiController and passes events to it;
 * tests can construct a TuiController directly without the ftxui event loop.
 *
 * The caller owns panel_focus and shares it with the panel components so
 * Container::Tab can read it; TuiController changes it on Tab.
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
#include "src/frontend/screens/bank_panel.h"
#include "src/frontend/screens/boss_analysis_panel.h"
#include "src/frontend/screens/boss_select_panel.h"
#include "src/frontend/screens/buff_info_panel.h"
#include "src/frontend/screens/buy_panel.h"
#include "src/frontend/screens/character_select_panel.h"
#include "src/frontend/screens/cube_panel.h"
#include "src/frontend/screens/dailies_panel.h"
#include "src/frontend/screens/hammer_panel.h"
#include "src/frontend/screens/inspect_panel.h"
#include "src/frontend/screens/job_inspect_panel.h"
#include "src/frontend/screens/jukebox_panel.h"
#include "src/frontend/screens/keybinds_panel.h"
#include "src/frontend/screens/link_skill_panel.h"
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

// What a party confirmation is about, so answering Yes knows what to do.
enum class PartyAsk { kNone, kKick, kPromote, kLeave };

// An absence shorter than this shows no card. A player who restarts a minute
// after closing the game doesn't need to hear what that minute paid.
inline constexpr double kOfflineNoticeSeconds = 60.0;

// Every panel the controller drives, bundled together. A struct of references
// instead of 25 constructor parameters written out three times in the same
// order. `Tui` owns the panels; this must not outlive it.
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
  BankPanel& bank_panel;
  LinkSkillPanel& link_skill_panel;
  JobInspectPanel& job_inspect_panel;
  SkillInspectPanel& skill_inspect_panel;
  BuffInfoPanel& buff_info_panel;
  MenuPanel& menu_panel;
  KeybindsPanel& keybinds_panel;
  OptionsPanel& options_panel;
  JukeboxPanel& jukebox_panel;
};

class TuiController {
 public:
  // panel_focus is a reference shared with the panel components and
  // Container::Tab; the controller changes it as focus moves.
  TuiController(GameState& state, Screens screens, BattleAnalysis& analysis,
                KeyMap& keys, int& panel_focus,
                MultiplayerSession* multiplayer = nullptr);

  // The [Expand]/[Close] button on `panel`. An expanded panel replaces the main
  // view rather than being its own screen, so menus and dialogs still appear
  // over it. Pressing it on another panel moves the expansion there.
  void ToggleExpanded(int panel);
  // The panel expanded to the whole screen, or kNoPanel.
  int expanded_panel() const {
    return expanded_panel_;
  }

  // Opens the equip or bag context menu. Called from MakeComponent callbacks.
  void OpenEquipMenu();
  // Enter in the bag: the context menu for an item, or the shop when the Shop
  // tab is showing.
  void OpenInventoryMenu();
  // Shows the AP amount entry over the main view, set to spend up to all
  // available AP on `field` (the maximum by default).
  void OpenApAllocate(StatField field);
  // Shows the skill-learning amount entry, set to the most points `skill` can
  // still take.
  void OpenSkillLearn(const Skill& skill);
  // The menu opened by Enter on a skill's name. The middle entry is only shown
  // for a toggle skill, and is dimmed until the skill is learned.
  void OpenSkillMenu(const Skill& skill);
  // Opens the skill's inspect screen. It copies the skill, as the learn dialog
  // does, so nothing depends on the catalog outliving the screen.
  void OpenSkillInspect(const Skill& skill);
  // Every stat the character has, on its own screen.
  void OpenAllStats();
  // Enter on the Link Skills row of the beginner page. The screen opens on the
  // preset the character is using.
  void OpenLinkSkills();

  // The four screens the Inspect panel opens. They are the player's own cards
  // showing someone else's numbers, so they read from the inspected member and
  // close back to the screen that opened them.
  void OpenPlayerSkillInspect(const Skill& skill);
  void OpenPlayerHyperStatInspect(HyperStatField field);
  void OpenPlayerAllStats();
  void OpenPlayerItemInspect();
  // Spends a point on `field`, or refunds the last one. Neither has a dialog:
  // the row's [-] undoes a [+].
  void RaiseHyperStat(HyperStatField field, StatPreset preset);
  void LowerHyperStat(HyperStatField field, StatPreset preset);
  // The tab's only confirmation: resetting a whole allocation.
  void OpenHyperReset(StatPreset preset);

  // The same confirmation for the V Matrix: every node reset and every point
  // refunded.
  void OpenVMatrixReset();
  const ConfirmPrompt& v_matrix_reset_prompt() const {
    return v_matrix_reset_prompt_;
  }

  // Locks or unlocks the Inner Ability line at `index` of `preset`. No screen:
  // the row's lock icon shows the result.
  void ToggleAbilityLock(int index, StatPreset preset);
  // Asks before rerolling `preset`, since that spends honor.
  void OpenAbilityReroll(StatPreset preset);

  // Turns `type` on or off. No screen and no confirmation: the row's switch
  // shows the result, and nothing is spent until the buff is used.
  void ToggleConsumable(ConsumableType type);
  // Shows the buff's context menu over the main view: read it, buy it
  // permanently, or close.
  void OpenBuffMenu(ConsumableType type);
  // Asks before buying `type` permanently, starting on Cancel because it costs
  // hundreds of millions. If the character can't afford it, the dialog still
  // opens with the price in red and [Confirm] greyed out, so the player can see
  // the price.
  void OpenBuffBuy(ConsumableType type);
  // The buff all three are about.
  ConsumableType buff_type() const {
    return buff_type_;
  }
  // The buff menu, for the overlay Tui shows beside the buff's row.
  const ItemMenu& buff_menu() const {
    return buff_menu_;
  }
  const ConfirmPrompt& buff_buy_prompt() const {
    return buff_buy_prompt_;
  }
  // Whether the character can afford what the open dialog asks. The dialog
  // greys out [Confirm] based on this, and the purchase uses the same check.
  bool buff_buy_affordable() const;
  // The permanent price, for the dialog to show.
  int64_t buff_buy_price() const;

  // The card opened by Enter on a stat's name. It is never gated: a stat the
  // character is too low for is the one they most want to read about.
  void OpenHyperStatInspect(HyperStatField field, StatPreset preset);
  HyperStatField hyper_inspect_field() const {
    return hyper_field_;
  }
  // Read live rather than stored, like skill_inspect_level(): spending a point
  // and inspecting again shows the new level.
  int hyper_inspect_level() const;
  int hyper_inspect_max_level() const;
  const ConfirmPrompt& hyper_reset_prompt() const {
    return hyper_reset_prompt_;
  }
  // The reset dialog's question, which names the allocation being reset.
  std::string hyper_reset_question() const;

  const ConfirmPrompt& ability_reroll_prompt() const {
    return ability_reroll_prompt_;
  }
  // The lines the open confirmation would replace: every line that isn't
  // locked, which is all the dialog lists.
  std::vector<AbilityLine> ability_reroll_lines() const;
  // Whether the last reroll raised the ability's rank, which lights the panel
  // gold. True until the player's next key.
  bool ability_rank_up() const {
    return ability_rank_up_;
  }
  // Shows the job's context menu. Enter in the Advance tab opens this rather
  // than the confirmation, so a job can be read before it is chosen.
  void OpenJobMenu(Job job);

  // Enter on the preset row: the menu to use or move a preset. `slot` is the
  // preset the cursor was on, which every entry acts on.
  void OpenPresetMenu(PresetKind kind, StatPreset slot);
  // Shows the job advancement confirmation over the main view. It starts on
  // Cancel, since the choice can't be undone.
  void OpenJobAdvance(Job job);
  // Opens the map selection screen on the current map.
  void OpenMapSelect();
  // Enter on a corner menu entry. Boss opens the boss screen and clears the
  // entry's gold; Settings opens its box over the corner.
  void OpenMenuEntry(MenuEntry entry);

  // Keeps the party screen and its fight in sync with the connection: the
  // current lobby, any server messages, the fight screen as soon as the party
  // enters, and the exit if the connection drops. Runs every tick, before the
  // fight is advanced.
  void AdvanceParty();

  // Runs the notification's clock down, and records that the player pressed a
  // key. Both come from Tui, which owns the frame clock and sees every key.
  void AdvanceNotification(double elapsed_seconds);
  void TouchNotification();

  // The server's message, shown over whatever is on screen: a refusal (in red),
  // party news, or a lost connection.
  const ContinuePrompt& party_notice_prompt() const {
    return party_notice_prompt_;
  }
  const std::string& party_notice() const {
    return party_notice_;
  }
  bool party_notice_is_refusal() const {
    return party_notice_is_refusal_;
  }
  // The item the trade screen's card is drawn from. One of the two is null.
  const EquipTabItem* trade_inspect_equip() const {
    return trade_inspect_equip_.get();
  }
  const ItemPrototype* trade_inspect_stack() const {
    return trade_inspect_stack_;
  }
  // The bag index of the stack the trade's amount overlay is offering, or -1
  // while it isn't open on one.
  int trade_stack() const {
    return trade_stack_;
  }
  // Whether this player has confirmed and is waiting for the other.
  bool trade_waiting() const;
  // Whether the player is anywhere on the trade screen. The map isn't farmed
  // meanwhile: meso draining from an offer, or a drop arriving between
  // accepting and the exchange, would change a trade nobody agreed to.
  bool OnTradeScreen() const;
  // Whether something happened that should be saved now rather than on the
  // autosave timer: a completed trade, the one moment value moves from one save
  // file to another. Reading it clears it.
  bool TakeSaveRequest();
  // The final confirmation's cursor, for the row it draws.
  const ConfirmPrompt& trade_prompt() const {
    return trade_prompt_;
  }
  const ConfirmPrompt& trade_leave_prompt() const {
    return trade_leave_prompt_;
  }
  // Which currency the trade overlay is offering, and how much.
  TradeCurrency trade_currency() const {
    return trade_currency_;
  }
  const AmountSelector& trade_selector() const {
    return trade_selector_;
  }
  // Which balance the bank overlay is moving, and how much.
  BankCurrency bank_currency() const {
    return bank_currency_;
  }
  const AmountSelector& bank_selector() const {
    return bank_selector_;
  }
  // The notification in the corner, which outlasts the screen that raised it.
  const NotificationBox& notification() const {
    return notification_;
  }
  // The confirmation a party action shows before it happens, and its question.
  const ConfirmPrompt& party_prompt() const {
    return party_prompt_;
  }
  const std::string& party_prompt_question() const {
    return party_prompt_question_;
  }

  // True while a key must reach the game as pressed rather than as its bound
  // action: a keybind slot or a text field is waiting for input. Every action's
  // first key is locked, so Enter, Escape and the arrows still work.
  bool capturing_key() const;

  // The stat the pending AP allocation targets, and its amount selector, for
  // the dialog Tui shows over the main view.
  StatField ap_alloc_field() const {
    return ap_field_;
  }
  const AmountSelector& ap_selector() const {
    return ap_selector_;
  }
  // The skill the pending learn targets, and its amount selector, for the
  // dialog Tui shows over the main view.
  const Skill& skill_learn_skill() const {
    return skill_learn_;
  }
  const AmountSelector& sp_selector() const {
    return sp_selector_;
  }

  // The skill menu, for the overlay Tui shows beside the skill's row, and the
  // skill it was opened on.
  const ItemMenu& skill_menu() const {
    return skill_menu_;
  }
  const Skill& skill_menu_skill() const {
    return skill_menu_skill_;
  }

  // What kSkillInspect draws: the skill, the level bought with points, and the
  // extra levels the book grants.
  const Skill& skill_inspect_skill() const {
    return skill_inspect_;
  }
  int skill_inspect_level() const;
  int skill_inspect_bonus() const;

  // What the pending advancement's dialog draws. The stage is the one above the
  // character's current stage, since the advancement is into it.
  Job job_advance_job() const {
    return job_advance_;
  }
  int job_advance_stage() const {
    return state_.character.proto().job_stage() + 1;
  }
  const ConfirmPrompt& job_advance_prompt() const {
    return job_advance_prompt_;
  }

  // The job menu, for the overlay Tui shows beside the job's row.
  const ItemMenu& job_menu() const {
    return job_menu_;
  }

  // The preset menu, shown beside its row, and what the Move popup behind it
  // needs. kNumStatPresets is the Cancel button below them.
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

  // The character select and its Delete confirmation, for the renderer. Both
  // belong to the controller, like the dailies card.
  const CharacterSelectPanel& character_select_panel() const {
    return character_select_panel_;
  }
  const ConfirmPrompt& character_delete_prompt() const {
    return character_delete_prompt_;
  }
  // The screen the quit dialog was opened over, which Cancel returns to and
  // which stays drawn behind it.
  Screen quit_return() const {
    return quit_return_;
  }
  // Whether the character select is open, which is the other time farming
  // stops: the player is choosing a character, and a map can't be fought by a
  // character who may be about to be swapped out.
  bool OnCharacterSelect() const {
    return screen_ == kCharacterSelect || screen_ == kCharacterMenu ||
           screen_ == kCharacterDelete;
  }
  // Whether a character has just been put into play. Reading it clears it. Tui
  // owns the fight and the watcher, and both belong to the character being
  // played; see Tui::StartPlayingCharacter.
  bool TakeCharacterSwitch();

  const DailiesPanel& dailies_panel() const {
    return dailies_panel_;
  }
  // The Level Up and Combine dialogs for an Arcane Symbol. The controller owns
  // them because neither holds game state, only what Reset set.
  const SymbolLevelPanel& symbol_level_panel() const {
    return symbol_level_panel_;
  }
  const SymbolCombinePanel& symbol_combine_panel() const {
    return symbol_combine_panel_;
  }
  // The golden hammer's confirmation, owned for the same reason.
  const HammerPanel& hammer_panel() const {
    return hammer_panel_;
  }

  // The quit dialog's prompt, for the same reason.
  const ConfirmPrompt& quit_prompt() const {
    return quit_prompt_;
  }

  // The prompt asking whether to start the highlighted boss fight, and what it
  // asks about.
  const ConfirmPrompt& boss_prompt() const {
    return boss_prompt_;
  }
  const std::string& boss_prompt_title() const {
    return boss_prompt_title_;
  }
  // Whether the fight being asked about would be practice. Stored with the
  // title, so the question can't change under the player.
  bool boss_prompt_practice() const {
    return boss_prompt_practice_;
  }
  // The single button that dismisses every one-button screen: a scroll or star
  // force result, or the notice that a fight hasn't reset.
  const ContinuePrompt& notice_prompt() const {
    return notice_prompt_;
  }
  // The button's label: "Continue" for a result, "Close" for a notice.
  const std::string& notice_button() const {
    return notice_button_;
  }
  // The notice's text, one line at a time, and whether it is a refusal, which
  // is drawn in red.
  const std::vector<std::string>& notice_lines() const {
    return notice_lines_;
  }
  bool notice_is_refusal() const {
    return notice_is_refusal_;
  }
  // The fight in progress, or null when the player isn't in one.
  const BossRun* boss_run() const {
    return boss_run_.get();
  }
  // The prompt asking whether to leave a fight.
  const ConfirmPrompt& boss_abort_prompt() const {
    return boss_abort_prompt_;
  }
  // The clear card: what was beaten, what it paid, and the button. Stored here
  // because the run is gone by the time the card is shown.
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
  // Whether the card's cursor is on [Analysis] rather than [Continue].
  bool boss_clear_on_analysis() const {
    return boss_clear_on_analysis_;
  }
  const BossAnalysisPanel& boss_analysis_panel() const {
    return boss_analysis_panel_;
  }
  // What the player earned while the game was closed, and the button that
  // dismisses its card.
  const OfflineReport& offline_report() const {
    return offline_report_;
  }
  const ContinuePrompt& offline_prompt() const {
    return offline_prompt_;
  }
  // Shows that card at launch, before the player does anything, so they see
  // what they earned before the game. A report not worth a card shows nothing.
  void OpenOfflineReport(OfflineReport report);

  // Advances the fight, records a clear, and returns to the previous screen
  // once the ending pause is over. Does nothing while the leave prompt is up,
  // so the clock can't run out while the player decides.
  void AdvanceBossRun(double elapsed_seconds);
  // Toggles the switch at `option` in the boss screen's options row.
  void ToggleBossOption(int option);
  // Charges the potion cost of entering a fight. Both ways into a boss run
  // call it: solo and party.
  void ChargeBossEntry();
  // True while a fight is on screen, when the map shouldn't be farmed because
  // the player is somewhere else.
  bool in_boss_fight() const {
    return boss_run_ != nullptr;
  }

  // True once the quit dialog is confirmed. The controller doesn't own the
  // ftxui screen, so it sets this and Tui does the quitting.
  bool quit_requested() const {
    return quit_requested_;
  }

  // On a screen with a card beside something else, which the arrows can reach.
  // Tab switches between them, and the focused one lights its title.
  bool right_card_focused() const {
    return right_card_focused_;
  }

  // Returns true if the event was consumed.
  bool OnEvent(ftxui::Event event);

  Screen screen() const {
    return screen_;
  }
  // What the Multi-Sell screen has selected. The screen owns the selection;
  // this lets a caller read it without going through the panel.
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
  // Returns the item being scrolled while in kScrollSelect or kScrollResult, or
  // nullptr otherwise.
  const EquipInstance* scroll_item() const;
  // The gear preset every comparison on an item card uses: the open Gear tab,
  // which is also where Equip would put the item. With autoswap off that tab
  // opens on the preset in use, so by default this is what the character is
  // wearing.
  StatPreset ComparisonPreset() const;
  // What the player already wears in the slot the inspected item would fill,
  // for the card beside it. nullptr when there is nothing to compare.
  const EquipTabItem* inspect_comparison() const;
  const EquipTabItem* player_item_comparison() const;
  const EquipInstance* WornForComparison(const EquipPrototype& proto) const;
  // Every slot the open inspect screen's item could go in and what the player
  // wears in each, for the Equipped card and its tab bar. One slot for
  // everything except rings and pendants; empty on a screen with nothing to
  // compare.
  InspectPanel::ComparisonSlots comparison_slots() const;
  // How equipping `item` would change the player's combat power, for the number
  // on its card. Empty when there is nothing to show: a slot this character
  // can't fill, or an item already worn in ComparisonPreset(). `fallback` is
  // the slot to compare against until the Equipped card's tab bar is used; if
  // unset, SlotToFill picks, matching Equip.
  std::optional<int> CombatPowerDelta(
      const EquipTabItem* item,
      std::optional<EquipSlot> fallback = std::nullopt) const;
  // The same for whichever item each screen shows.
  std::optional<int> inspect_delta() const;
  std::optional<int> player_item_delta() const;
  // Returns the item being inspected while in kInspect, or nullptr otherwise.
  // May be an EquipTrace if the selected bag item was destroyed.
  const EquipTabItem* inspect_item() const;
  // The stack inspected in kItemInspect, as a prototype: the screen shows what
  // the item is, not how many are held.
  const ItemPrototype* item_inspect_item() const;
  // Returns the item being star forced while in kStarForce, or nullptr
  // otherwise. Don't call in kStarForceResult (the item may be destroyed).
  const EquipInstance* star_force_item() const;
  // The item the cubing screen is working on, read live rather than cached: a
  // cube destroys nothing, so the screen draws the item's own lines.
  const EquipInstance* cube_item() const;
  // Returns the trace being recovered while in kTraceRecover, or nullptr.
  const EquipTabItem* trace_recover_item() const;

  // Whether `panel` is shown for this character. The equipped panel and the bag
  // unlock as they level; the other two are there from the start. Both the
  // layout and Tab use this, so they can't disagree.
  bool PanelVisible(int panel) const;

 private:
  // Moves focus off a panel that isn't shown. The game starts focused on the
  // equipped panel, which a level 1 character doesn't have yet.
  void EnsureFocusIsVisible();

  // Where the item under the focused panel's cursor is. The only place that
  // reads panel_focus_ to answer that.
  ItemRef SelectedItem() const;

  bool OnMainViewEvent(ftxui::Event event);
  bool OnItemMenuEvent(ftxui::Event event);

  // The three kinds of screen an item menu opens, each preparing the panel for
  // the screen. If the item can't use that screen, it returns the screen it
  // opened instead.
  Screen SeedUpgradeScreen(Screen next);
  Screen SeedSaleScreen(Screen next);
  Screen SeedSymbolScreen(Screen next);
  // The keys every screen made of inspect cards handles, with `back` as the
  // screen to return to. One handler, because to the player they are one
  // screen, opened from the bag, the shop, or another player's sheet.
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
  // Does what the character menu's selected entry does.
  void TakeCharacterMenuEntry();
  // Returns to the game with whoever is now in play. The game saves either way.
  // If `switched`, it also resets the panels as at session start and has Tui
  // rebuild the fight, which resuming must not do.
  void LeaveCharacterSelect(bool switched);
  // Another character is now in play: Tui is told, and any party is left.
  void SwitchedCharacter();
  // Shows the quit dialog over the current screen. Both Escape and the
  // character select's Quit button use this.
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
  // The character the open skill or Hyper Stat card is about; see
  // card_from_inspect_.
  const CharacterInstance& card_character() const;
  bool OnPlayerItemInspectEvent(ftxui::Event event);
  bool OnPartyConfirmEvent(ftxui::Event event);
  // Closes whichever of the two the player is on, returning to the box that
  // opened it.
  void LeaveMultiplayerScreen();
  // Open the party screen and the Players screen, each after checking there
  // is a connection to show.
  void OpenPartySelect();
  void OpenPlayerList();
  // Whether the connection is up. If it isn't, shows the notice and requests a
  // reconnect.
  bool Connected();
  // Opens the Inspect screen on a party member, whose sheet is already in the
  // party state.
  void OpenPlayerInspect(const std::string& account_id);
  // Asks `account_id` to trade, from either menu. The trade screen opens when
  // the server answers, so nothing here changes the screen.
  void AskToTrade(const std::string& account_id);
  // Applies a completed trade to the bag: what was offered leaves, what the
  // other side offered arrives, and the screen returns to the list.
  void ApplyCompletedTrade(const TradeOffer& received);
  // Shows the final confirmation when both have accepted, and removes it when
  // either withdraws.
  void AdvanceTradeConfirm(const TradeState& trade);
  // Opens and closes the trade screen as the server's trade starts and ends. A
  // trade that ends while the player is on it returns them to where they opened
  // it.
  void AdvanceTrade(const MultiplayerSnapshot& lobby);
  // Shows the amount overlay for the currency under the cursor.
  void OpenTradeAmount();
  // Offers the amount the overlay was set to.
  void PutUpTradeAmount();
  // Sends the server this player's whole side of the trade.
  void SendTradeOffer();
  // Enter on a row in any of the three windows.
  void OpenTradeMenu();
  // Enter on the bag's Bank tab, which opens the screen on the bag's Equip tab
  // with both halves' cursors reset.
  void OpenBank();
  // Enter on the Accept button, which toggles. Accepting is refused with a
  // notice if the bag can't hold what the other side offers. This is the only
  // moment to check, since any change to the offers withdraws an acceptance.
  void ToggleTradeAccept();
  // Offer on a bag row: an equip is offered as is, and a stack through the
  // amount overlay. Refused with a notice once eight items are offered.
  void OfferFromBag();
  void OpenTradeItemAmount(int stack);
  void PutUpTradeItemAmount();
  // Inspect on a row in any window. The other side's items are built from
  // network data, since nothing in this client's bag is their item.
  void OpenTradeInspect();
  // Leaves the trade, which ends it for both.
  void LeaveTrade();
  // Requests `account_id`'s sheet and opens the Inspect screen once it arrives.
  // A player from the roster isn't in any party, so their sheet must be fetched
  // before there is anything to draw.
  void WatchForInspect(const std::string& account_id);
  // Checks whether the player the Players list is waiting for has arrived, and
  // opens their screen. Does nothing while no request is pending.
  void AdvanceWatch(const MultiplayerSnapshot& lobby);
  // Cancels the Players list's pending request, if any.
  void StopWatching();
  // Keeps the inspect screen in sync with the lobby, and closes it if the
  // inspected member leaves.
  void RefreshPlayerInspect(const MultiplayerSnapshot& lobby);
  // Does the selected action, either sending it straight to the server or
  // asking for confirmation first.
  void TakePartyAction(PartyAction action);
  // Shows `question` over the party screen; PartyConfirmed() runs on Yes.
  void AskAboutParty(PartyAsk ask, const std::string& question);
  void PartyConfirmed();
  // Shows `message` over the current screen. A refusal is drawn in red.
  void RaisePartyNotice(const std::string& message, bool refusal);
  // Reads what the server says about the party's fight, and puts the player in
  // one that has just started.
  void AdvancePartyFight(const MultiplayerSnapshot& lobby);
  // Opens the fight screen on the fight the party has entered. Whatever the
  // player was doing, they are in the fight now.
  void OpenPartyFight(const MultiplayerSnapshot& lobby);
  // Copies the party into the GameState, so their skills' effects on this
  // character are included in its stats. See GameState::party.
  void SeatParty(const MultiplayerSnapshot& lobby);
  // Whether the fight on screen is the party's rather than a solo one.
  bool in_party_fight() const;
  // Releases the finished run and the fight behind it.
  void DropBossRun();
  // The current lobby, or an empty one for single-player.
  MultiplayerSnapshot Lobby() const;
  bool OnBossSelectEvent(ftxui::Event event);
  bool OnBossConfirmEvent(ftxui::Event event);
  bool OnBossNoticeEvent(ftxui::Event event);
  // Shows a one-button screen with its prompt ready, so every screen dismissed
  // by [Continue] is opened the same way.
  void OpenNotice(Screen screen);
  // The same for a screen that is only a message: `lines` is the text, a
  // refusal is drawn in red, and `button` is the button's label.
  void OpenNotice(Screen screen, std::vector<std::string> lines, bool refusal,
                  const std::string& button);
  // A one-sentence notice split into evenly sized lines. Use this for any
  // notice that names something; otherwise a short name and a long remainder
  // look lopsided.
  void OpenSentenceNotice(Screen screen, const std::string& sentence,
                          bool refusal, const std::string& button);
  // Enter on the menu's Dailies entry: the claim, or a notice that today's is
  // already claimed.
  void OpenDailies();
  bool OnDailiesEvent(ftxui::Event event);
  bool OnDailiesNoticeEvent(ftxui::Event event);
  bool OnBossFightEvent(ftxui::Event event);
  bool OnBossAbortEvent(ftxui::Event event);
  bool OnBossClearEvent(ftxui::Event event);
  bool OnBossAnalysisEvent(ftxui::Event event);
  bool OnOfflineEvent(ftxui::Event event);
  // Releases the finished run and returns to the fight list. Every panel a
  // fight ends on is dismissed with this.
  void LeaveBossRun();
  bool OnMenuBoxEvent(ftxui::Event event);
  // Enter on a row of the open box: opens whatever that entry leads to.
  void OpenBoxEntry();
  // Start and Stop toggle the tool; View shows its overlay.
  void OpenAnalysisEntry(AnalysisEntry entry);
  bool OnAnalysisEvent(ftxui::Event event);
  bool OnKeybindsEvent(ftxui::Event event);
  bool OnOptionsEvent(ftxui::Event event);
  bool OnJukeboxEvent(ftxui::Event event);
  // Puts the captured key in the waiting slot, or says why it couldn't. Ignores
  // events that aren't keys, so the slot keeps waiting.
  void TakeCapturedKey(const ftxui::Event& key);
  // Leaves the Keybinds screen for the box it was opened from.
  void LeaveKeybinds();
  void LeaveOptions();
  void LeaveJukebox();
  // The bank screen: the bag above the account's storage, the menu a row opens,
  // the amount dialog for a balance, and the card Inspect opens.
  bool OnBankEvent(ftxui::Event event);
  bool OnBankMenuEvent(ftxui::Event event);
  bool OnBankAmountEvent(ftxui::Event event);
  // Shows "Inventory full." or "Bank full." if there was no room.
  void MoveInBank();
  void OpenBankAmount();

  // The Link Skills screen and the menus its rows open. The preset menu is the
  // Hyper tab's, so Move uses the same dialog.
  bool OnLinkSkillsEvent(ftxui::Event event);
  bool OnLinkSkillMenuEvent(ftxui::Event event);

  bool OnShopEvent(ftxui::Event event);
  bool OnShopMenuEvent(ftxui::Event event);
  bool OnShopInspectEvent(ftxui::Event event);
  // Scrolls every inspect card back to the top, with the left half of the
  // screen taking the arrows. Called as each such screen opens.
  void OpenInspectCards();
  // The equip the open inspect screen compares against the player's gear, and
  // the slot its Equipped card shows until the arrows move it: where Equip
  // would put it, or, on a member's sheet, the slot they wear it in. A null
  // prototype means nothing to compare.
  struct ComparisonSubject {
    const EquipPrototype* proto = nullptr;
    EquipSlot fallback = EQUIP_SLOT_UNSPECIFIED;
  };
  ComparisonSubject InspectSubject() const;
  // The shop item or buy-back row the shop's inspect screen is showing.
  const EquipPrototype* ShopInspectProto() const;
  // The slot of `proto`'s family the Equipped card shows: the one the arrows
  // were left on, or `fallback` if they haven't been used.
  EquipSlot ComparisonSlot(const EquipPrototype& proto,
                           EquipSlot fallback) const;
  // Moves the Equipped card's tab bar by `step`, wrapping like every tab bar.
  // Returns false for an item with one slot, and the arrows then scroll the
  // card sideways instead.
  bool StepComparisonSlot(int step);
  bool OnShopBuyEvent(ftxui::Event event);
  // Sets up the buy dialog for a buy-back row, which is priced and limited by
  // the sale rather than by the shop's stock.
  void OpenBuyBackDialog(const BuyBackEntry& entry);
  // Spends what a confirmed buy dialog agreed to, on whichever shelf it was
  // opened over.
  void BuyWhatTheDialogAgreedTo();

  GameState& state_;
  // Kept so focus arriving here can clear the Advance tab's gold; otherwise the
  // panel runs itself.
  CharacterPanel& char_panel_;
  EquippedPanel& equip_panel_;
  InventoryPanel& inventory_panel_;
  ScrollPanel& scroll_panel_;
  // Which slot of the inspected ring's family the Equipped card shows, as an
  // index into the family. Unset until the arrows move the tab bar, which lets
  // the card open on the slot Equip would fill. Cleared whenever an inspect
  // screen opens; one member is enough, since only one is open at a time.
  std::optional<int> compare_slot_;
  // The item card, and the preview beside it on kTraceRecover and
  // kStarForce. Kept so the arrows can scroll whichever is being read.
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
  // The member the inspect screen is showing, so the lobby's next update about
  // them is applied to it.
  std::string inspect_account_;
  // Which list the open Inspect screen came from, which Escape returns to.
  bool inspect_from_players_ = false;
  // The player the Players list has requested from the server and is waiting
  // for. Empty once their sheet has arrived, or once they have left.
  std::string inspect_pending_;
  // Whether the open skill or Hyper Stat card shows a party member rather than
  // the player. The card is the same either way, but whose levels it shows and
  // which screen it returns to differ.
  bool card_from_inspect_ = false;
  // The screen the skill card returns to for the player's own character: the
  // main view, or the Link Skills screen that opened it.
  Screen skill_card_return_ = kMain;
  JobInspectPanel& job_inspect_panel_;
  SkillInspectPanel& skill_inspect_panel_;
  BuffInfoPanel& buff_info_panel_;
  MenuPanel& menu_panel_;
  KeybindsPanel& keybinds_panel_;
  OptionsPanel& options_panel_;
  JukeboxPanel& jukebox_panel_;
  // The measurement the Analysis entry starts and stops. The session owns it,
  // not the controller, since it outlives every screen that reads it.
  BattleAnalysis& analysis_;
  KeyMap& keys_;
  ShopPanel& shop_panel_;
  BuyPanel& buy_panel_;
  BankPanel& bank_panel_;
  LinkSkillPanel& link_skill_panel_;
  // Catalog key of the item the buy dialog is open on, so the purchase looks up
  // the prototype rather than trusting a pointer to outlive the screen.
  std::string buy_item_;
  // The shelf row the buy dialog was opened on, so moving the cursor can't buy
  // back a different sale.
  int buy_back_row_ = 0;
  int& panel_focus_;
  Screen screen_ = kMain;
  // The item the open modal is about, set once when the player picks it. One
  // ref serves every modal since only one is open at a time; each accessor
  // below checks screen_ to see whose it is.
  ItemRef subject_;
  // The bag row the open modal is about; recovery and selling equips only work
  // in the bag. One row, for the same reason as subject_.
  int bag_row_ = 0;
  // See right_card_focused(). False when any screen opens, so the arrows start
  // on the list or card the player was reading.
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
  // Rebuilt every time the menu opens: the middle entry reads Activate or
  // Deactivate depending on the switch's current state.
  Skill skill_menu_skill_;
  ItemMenu skill_menu_{{"Inspect", "Activate", "Close"}};
  Job job_advance_ = JOB_UNSPECIFIED;
  ItemMenu job_menu_{{"Inspect", "Advance", "Close"}};
  // What the preset menu and its Move popup are about. Stored rather than read
  // back from the panel, so a swap applies to the preset the menu named.
  PresetKind preset_kind_ = PresetKind::kHyperStats;
  StatPreset preset_slot_ = StatPreset::kFirst;
  ItemMenu preset_menu_{{"Use", "Move", "Close"}};
  // The screen the Move popup returns to, stored for the same reason.
  Screen preset_return_ = kMain;
  // The Move popup's cursor: a preset, or kNumStatPresets for Cancel.
  int preset_move_row_ = 0;
  // The buff the menu, card and confirmation are about. Stored so the answer
  // applies to the buff the question named, wherever the cursor went.
  ConsumableType buff_type_ = CONSUMABLE_TYPE_UNSPECIFIED;
  ItemMenu buff_menu_{{"Disable", "Inspect", "Buy Perm", "Close"}};
  ConfirmPrompt buff_buy_prompt_;
  DailiesPanel dailies_panel_;
  CharacterSelectPanel character_select_panel_;
  ConfirmPrompt character_delete_prompt_;
  // The slot the open Delete confirmation is about, stored when it opens, since
  // the cursor may have moved by the time it is answered.
  int character_delete_slot_ = -1;
  // True from a switch until Tui reads it.
  bool character_switched_ = false;
  // The screen the quit dialog was opened over, which Cancel returns to. The
  // character select is the only screen other than the main view it can be
  // opened from.
  Screen quit_return_ = kMain;
  SymbolLevelPanel symbol_level_panel_;
  ConfirmPrompt hyper_reset_prompt_;
  ConfirmPrompt v_matrix_reset_prompt_;
  // The Hyper Stat the open confirmation is about. Stored rather than read back
  // from the panel, so the answer applies to the stat the question named.
  HyperStatField hyper_field_ = HYPER_STAT_FIELD_UNSPECIFIED;
  StatPreset hyper_preset_ = StatPreset::kFirst;
  ConfirmPrompt ability_reroll_prompt_;
  // The allocation the open Inner Ability confirmation is about, kept separate
  // from the Hyper one above so neither answer can apply to the other.
  StatPreset ability_preset_ = StatPreset::kFirst;
  // See ability_rank_up(). OnEvent clears it before dispatching, so the reroll
  // that sets it keeps it and the key after clears it.
  bool ability_rank_up_ = false;
  SymbolCombinePanel symbol_combine_panel_;
  HammerPanel hammer_panel_;
  // The worn symbol the two symbol dialogs are about. Stored so the answer
  // applies to it, wherever the cursor went meanwhile.
  EquipSlot symbol_slot_ = EQUIP_SLOT_UNSPECIFIED;
  ConfirmPrompt job_advance_prompt_;
  ConfirmPrompt quit_prompt_;
  // The boss confirmation and the fight it asks about. The title is stored so
  // the dialog's question can't change under the player.
  ConfirmPrompt boss_prompt_;
  std::string boss_prompt_title_;
  bool boss_prompt_practice_ = false;
  ContinuePrompt notice_prompt_;
  std::vector<std::string> notice_lines_;
  bool notice_is_refusal_ = false;
  std::string notice_button_;
  ConfirmPrompt boss_abort_prompt_;
  // The connection, or null for single-player.
  MultiplayerSession* multiplayer_ = nullptr;
  // The pending party action and who it is about, stored so a cursor that moved
  // can't answer a different question.
  PartyAsk party_ask_ = PartyAsk::kNone;
  std::string party_target_;
  ConfirmPrompt party_prompt_;
  std::string party_prompt_question_;
  NotificationBox notification_;
  // The last notification shown, so a new one is shown once rather than every
  // frame.
  int64_t notification_seen_ = 0;
  AmountSelector trade_selector_;
  // The final confirmation. The trade state says which side is waiting, so this
  // only holds the cursor.
  ConfirmPrompt trade_prompt_;
  // The confirmation before leaving, which ends the trade for both. It has its
  // own prompt, separate from the final confirmation's: two questions, two
  // cursors.
  ConfirmPrompt trade_leave_prompt_;
  // Which currency the amount overlay is offering, stored when it opens, since
  // the cursor may have moved by the time it is answered. The stack overlay
  // stores the bag index for the same reason.
  TradeCurrency trade_currency_ = TradeCurrency::kMeso;
  int trade_stack_ = -1;
  // The bank's amount overlay, and which balance it moves. Stored when it
  // opens, for the same reason as the trade's.
  AmountSelector bank_selector_;
  BankCurrency bank_currency_ = BankCurrency::kMeso;
  // What the trade screen is inspecting. Built rather than pointed at: the
  // other side's items aren't in any bag this client has, and one path for both
  // sides is easier to keep right.
  std::unique_ptr<EquipTabItem> trade_inspect_equip_;
  const ItemPrototype* trade_inspect_stack_ = nullptr;
  // The screen the trade was opened from, which leaving returns to.
  Screen trade_return_ = kPlayerList;
  // The trade this player left, so a late state update doesn't reopen the
  // screen.
  std::string left_trade_id_;
  ContinuePrompt party_notice_prompt_;
  std::string party_notice_;
  bool party_notice_is_refusal_ = false;
  // The serial of the last notice shown, so each is shown once.
  int64_t party_notice_seen_ = 0;
  // The last trade payment this client added to the bag, so none is applied
  // twice.
  int64_t trade_paid_seen_ = 0;
  bool save_wanted_ = false;
  // What the offline card shows, and its button.
  OfflineReport offline_report_;
  ContinuePrompt offline_prompt_;
  // What the clear card shows, kept while it is up.
  std::string boss_clear_title_;
  BossReward boss_clear_reward_;
  double boss_clear_seconds_ = 0.0;
  ContinuePrompt boss_clear_prompt_;
  bool boss_clear_on_analysis_ = false;
  BossAnalysisPanel boss_analysis_panel_;
  // The fight in progress and its catalog entry, so a clear is recorded under
  // the names the reset clock reads.
  std::unique_ptr<BossRun> boss_run_;
  std::string boss_run_key_;
  std::string boss_run_difficulty_;
  // The party's fight, which the run follows. Null for single-player.
  std::unique_ptr<PartyFightAuthority> party_fight_;
  bool quit_requested_ = false;
  ScrollResult scroll_result_;
  StarForceResult star_force_result_;
  TraceRecoveryResult trace_recovery_result_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_TUI_CONTROLLER_H_
