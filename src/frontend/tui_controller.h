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
#include <string>
#include <vector>

#include "ftxui/component/event.hpp"
#include "src/combat/battle_analysis.h"
#include "src/combat/boss_run.h"
#include "src/combat/offline.h"
#include "src/frontend/item_ref.h"
#include "src/frontend/keybinds.h"
#include "src/frontend/panels/character_panel.h"
#include "src/frontend/panels/equipped_panel.h"
#include "src/frontend/panels/inventory_panel.h"
#include "src/frontend/panels/menu_panel.h"
#include "src/frontend/screens/boss_select_panel.h"
#include "src/frontend/screens/buy_panel.h"
#include "src/frontend/screens/hammer_panel.h"
#include "src/frontend/screens/job_inspect_panel.h"
#include "src/frontend/screens/keybinds_panel.h"
#include "src/frontend/screens/map_select_panel.h"
#include "src/frontend/screens/mob_inspect_panel.h"
#include "src/frontend/screens/multi_sell_panel.h"
#include "src/frontend/screens/party_inspect_panel.h"
#include "src/frontend/screens/party_select_panel.h"
#include "src/frontend/screens/scroll_panel.h"
#include "src/frontend/screens/sell_equip_panel.h"
#include "src/frontend/screens/sell_panel.h"
#include "src/frontend/screens/shop_panel.h"
#include "src/frontend/screens/skill_inspect_panel.h"
#include "src/frontend/screens/star_force_panel.h"
#include "src/frontend/screens/symbol_combine_panel.h"
#include "src/frontend/screens/symbol_level_panel.h"
#include "src/frontend/screens/trace_recover_panel.h"
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

namespace ms {

// What a party question is asking about, so answering Yes knows what to do.
enum class PartyAsk { kNone, kKick, kPromote, kLeave };

// An absence shorter than this raises no card. A player who restarted the game
// a minute after closing it does not need to be told what that minute paid.
inline constexpr double kOfflineNoticeSeconds = 60.0;

class TuiController {
 public:
  // panel_focus is a reference shared with panel components and
  // Container::Tab; the controller mutates it as focus changes.
  TuiController(
      GameState& state, CharacterPanel& char_panel, EquippedPanel& equip_panel,
      InventoryPanel& inventory_panel, ScrollPanel& scroll_panel,
      StarForcePanel& star_force_panel, TraceRecoverPanel& trace_recover_panel,
      SellPanel& sell_panel, SellEquipPanel& sell_equip_panel,
      MultiSellPanel& multi_sell_panel, MapSelectPanel& map_select_panel,
      MobInspectPanel& mob_inspect_panel, BossSelectPanel& boss_select_panel,
      PartySelectPanel& party_select_panel,
      PartyInspectPanel& party_inspect_panel, ShopPanel& shop_panel,
      BuyPanel& buy_panel, JobInspectPanel& job_inspect_panel,
      SkillInspectPanel& skill_inspect_panel, MenuPanel& menu_panel,
      KeybindsPanel& keybinds_panel, BattleAnalysis& analysis, KeyMap& keys,
      int& panel_focus, MultiplayerSession* multiplayer = nullptr);

  // Open the equip or bag context menu. Called from MakeComponent callbacks.
  void OpenEquipMenu();
  // Enter in the bag: the context menu on an item, or the shop when the Shop
  // tab is the one showing.
  void OpenInventoryMenu();
  // Float the AP-allocation amount entry over the main view, seeded to spend up
  // to all available AP on `field` (defaulting to the max).
  void OpenApAllocate(StatField field);
  // Float the skill-learning amount entry over the main view, seeded to spend
  // up to the most points `skill` can still take (its stage SP, capped at how
  // far it is below max level).
  void OpenSkillLearn(const Skill& skill);
  // Open the menu Enter on a skill's name raises: read it, switch it on or
  // off, or walk away. The middle entry is offered only by a toggle skill, and
  // is dim until the player has bought it.
  void OpenSkillMenu(const Skill& skill);
  // Open the skill's inspect screen. Copies the skill, as the learn dialog
  // does, so nothing downstream depends on the catalog outliving the screen.
  void OpenSkillInspect(const Skill& skill);
  // Every stat the character has, on a screen of its own.
  void OpenAllStats();
  // Float the job's context menu over the main view: read the job, take it, or
  // walk away. Enter in the Advance tab lands here rather than on the
  // confirmation -- what a job is should be readable before it is chosen.
  void OpenJobMenu(Job job);
  // Float the job-advancement confirmation over the main view. The prompt opens
  // on Cancel: the choice cannot be taken back.
  void OpenJobAdvance(Job job);
  // Open the map selection screen, on the map being farmed.
  void OpenMapSelect();
  // Enter on an entry of the corner menu. Boss opens the boss screen and
  // clears the entry's gold; Settings opens its box over the corner.
  void OpenMenuEntry(MenuEntry entry);

  // Keeps the party screen and the party's fight in step with the connection:
  // hands the panel the lobby as it stands, raises whatever the server has
  // said since the last call, opens the fight screen the moment the party is
  // let into one, and turns the player out of both if the connection goes.
  // Called every tick, before the fight is stepped.
  void AdvanceParty();

  // The word from the server floated over whatever the player is looking at:
  // an action it would not take, something that happened to their party, or
  // the connection going away. A refusal is drawn in red.
  const ContinuePrompt& party_notice_prompt() const {
    return party_notice_prompt_;
  }
  const std::string& party_notice() const {
    return party_notice_;
  }
  bool party_notice_is_refusal() const {
    return party_notice_is_refusal_;
  }
  // The question a party action asks before it is taken, and what it asks.
  const ConfirmPrompt& party_prompt() const {
    return party_prompt_;
  }
  const std::string& party_prompt_question() const {
    return party_prompt_question_;
  }

  // True while a key must reach the game as the player pressed it rather than
  // as the action it is bound to: a keybind slot waiting for the key it will
  // take, or a text field waiting for a letter. Every action's first key is
  // locked, so Enter, Escape and the arrows still work while it is true.
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

  // The skill being inspected while in kSkillInspect, the level the character
  // has spent points to, and the levels their book lends every skill, for the
  // screen Tui draws.
  const Skill& skill_inspect_skill() const {
    return skill_inspect_;
  }
  int skill_inspect_level() const;
  int skill_inspect_bonus() const;

  // The job the pending advancement would take, and its prompt, for the dialog
  // Tui floats over the main view.
  Job job_advance_job() const {
    return job_advance_;
  }
  const ConfirmPrompt& job_advance_prompt() const {
    return job_advance_prompt_;
  }

  // The job menu, for the overlay Tui floats beside the job's row.
  const ItemMenu& job_menu() const {
    return job_menu_;
  }

  // The Level Up dialog for an Arcane Symbol, and the Combine one. Owned
  // rather than handed in: neither carries any game state, only what Reset was
  // told to say.
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
  // The [Continue] every one-button screen is dismissed by -- a scroll or star
  // force result, and the notice that a fight is still on its reset.
  const ContinuePrompt& notice_prompt() const {
    return notice_prompt_;
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
  // The clear card: what was beaten, what it paid, and the button that
  // dismisses it. Held on the controller rather than read off the run, which
  // is gone by the time the card is up.
  const std::string& boss_clear_title() const {
    return boss_clear_title_;
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
  // Raises that card over the main view. Called once at launch, before the
  // player has touched anything: they should see what they were paid before
  // they see the game. A report not worth a card -- too short an absence, or a
  // player who logged off in town -- raises nothing.
  void OpenOfflineReport(OfflineReport report);

  // Steps the fight in progress by elapsed_seconds, records the clear if it
  // ended in one, and takes the screen back to the fight list once the closing
  // beat is up. Does nothing without a fight, and nothing while the leave
  // prompt is up -- the clock must not run out while the player is deciding.
  void AdvanceBossRun(double elapsed_seconds);
  // True while a fight owns the screen, which is when the map should not be
  // farmed: the player is somewhere else.
  bool in_boss_fight() const {
    return boss_run_ != nullptr;
  }

  // True once the player has confirmed the quit dialog. The controller cannot
  // close the terminal itself -- it does not own the ftxui screen -- so it
  // raises this and leaves the leaving to Tui.
  bool quit_requested() const {
    return quit_requested_;
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
  // Returns the item being inspected while in kInspect, or nullptr otherwise.
  // May be an EquipTrace if the selected bag item was destroyed.
  const EquipTabItem* inspect_item() const;
  // Returns the stack being inspected while in kItemInspect, or nullptr. The
  // prototype rather than the stack, because what is on screen is what the
  // item is, not how many of it the player is holding.
  const ItemPrototype* item_inspect_item() const;
  // Returns the item being star forced while in kStarForce, or nullptr
  // otherwise. Do not call in kStarForceResult (item may be destroyed).
  const EquipInstance* star_force_item() const;
  // Returns the trace being recovered while in kTraceRecover, or nullptr.
  const EquipTabItem* trace_recover_item() const;

  // Whether `panel` is on screen for this character. The equipped panel and
  // the bag are handed over as the player levels; the character and combat
  // panels are there from the first frame.
  //
  // Asked by the layout to decide what to draw and by Tab to decide what to
  // skip, so the two cannot disagree about which panels exist.
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
  bool OnInspectEvent(ftxui::Event event);
  bool OnScrollSelectEvent(ftxui::Event event);
  bool OnScrollResultEvent(ftxui::Event event);
  bool OnApAllocEvent(ftxui::Event event);
  bool OnSkillLearnEvent(ftxui::Event event);
  bool OnSkillMenuEvent(ftxui::Event event);
  bool OnSkillInspectEvent(ftxui::Event event);
  bool OnJobMenuEvent(ftxui::Event event);
  bool OnJobInspectEvent(ftxui::Event event);
  bool OnJobAdvanceEvent(ftxui::Event event);
  bool OnQuitEvent(ftxui::Event event);
  bool OnStarForceEvent(ftxui::Event event);
  bool OnStarForceResultEvent(ftxui::Event event);
  bool OnHammerEvent(ftxui::Event event);
  bool OnHammerNoticeEvent(ftxui::Event event);
  bool OnTraceRecoverEvent(ftxui::Event event);
  bool OnTraceRecoverResultEvent(ftxui::Event event);
  bool OnSellEvent(ftxui::Event event);
  bool OnSellEquipEvent(ftxui::Event event);
  bool OnSymbolLevelEvent(ftxui::Event event);
  bool OnSymbolCombineEvent(ftxui::Event event);
  bool OnMultiSellEvent(ftxui::Event event);
  bool OnMapSelectEvent(ftxui::Event event);
  bool OnMapMenuEvent(ftxui::Event event);
  bool OnMobInspectEvent(ftxui::Event event);
  bool OnPartySelectEvent(ftxui::Event event);
  bool OnPartyMenuEvent(ftxui::Event event);
  bool OnPartyInspectEvent(ftxui::Event event);
  bool OnPartyItemInspectEvent(ftxui::Event event);
  bool OnPartyConfirmEvent(ftxui::Event event);
  // Opens the inspect screen on the member playing under `account_id`. Does
  // nothing for a member who has gone since the menu was raised.
  void OpenPartyInspect(const std::string& account_id);
  // Keeps the inspect screen on what the lobby last said, and turns the
  // player out of it when the member they are reading leaves.
  void RefreshPartyInspect(const MultiplayerSnapshot& lobby);
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
  // Rebuilds the rest of the party into the GameState, so that what their
  // skills hold over this character is folded into its stats for the fight.
  // See GameState::party.
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
  // says, and a refusal is drawn in red.
  void OpenNotice(Screen screen, std::vector<std::string> lines, bool refusal);
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
  // Puts the captured key in the waiting slot, or says why it could not go
  // there. Ignores what is not a key at all -- the ticker's own redraw among
  // them -- so the slot goes on waiting for one.
  void TakeCapturedKey(const ftxui::Event& key);
  // Leaves the Keybinds screen for the box it was opened from.
  void LeaveKeybinds();
  bool OnShopEvent(ftxui::Event event);
  bool OnShopMenuEvent(ftxui::Event event);
  bool OnShopInspectEvent(ftxui::Event event);
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
  StarForcePanel& star_force_panel_;
  TraceRecoverPanel& trace_recover_panel_;
  SellPanel& sell_panel_;
  SellEquipPanel& sell_equip_panel_;
  MultiSellPanel& multi_sell_panel_;
  MapSelectPanel& map_select_panel_;
  MobInspectPanel& mob_inspect_panel_;
  BossSelectPanel& boss_select_panel_;
  PartySelectPanel& party_select_panel_;
  PartyInspectPanel& party_inspect_panel_;
  // The member the inspect screen is reading, so the lobby's next word about
  // them lands on it.
  std::string party_inspect_account_;
  JobInspectPanel& job_inspect_panel_;
  SkillInspectPanel& skill_inspect_panel_;
  MenuPanel& menu_panel_;
  KeybindsPanel& keybinds_panel_;
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
  // Each modal remembers the item it was opened on. Where that item lives is
  // settled once, when the player picks it, so nothing downstream has to ask
  // which panel had focus at the time.
  ItemRef scroll_ref_;
  ItemRef inspect_ref_;
  ItemRef star_force_ref_;
  ItemRef hammer_ref_;
  // Recovery is a bag-only affair: a trace cannot be worn.
  int trace_index_ = 0;
  ItemCategory sell_category_ = ITEM_CATEGORY_UNSPECIFIED;
  int sell_index_ = 0;
  // The bag row the equip sale is open on. Bag-only, like recovery: an item
  // has to come off before it can be sold.
  int sell_equip_index_ = 0;
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
  SymbolLevelPanel symbol_level_panel_;
  SymbolCombinePanel symbol_combine_panel_;
  HammerPanel hammer_panel_;
  // The worn symbol the two symbol dialogs are asking about. Held so the
  // answer acts on what was asked, whatever the cursor did in the meantime.
  EquipSlot symbol_slot_ = EQUIP_SLOT_UNSPECIFIED;
  ConfirmPrompt job_advance_prompt_;
  ConfirmPrompt quit_prompt_;
  // The boss confirmation, and the fight it is asking about. The title is
  // held rather than re-read, so the dialog cannot change what it is asking
  // under the player.
  ConfirmPrompt boss_prompt_;
  std::string boss_prompt_title_;
  ContinuePrompt notice_prompt_;
  std::vector<std::string> notice_lines_;
  bool notice_is_refusal_ = false;
  ConfirmPrompt boss_abort_prompt_;
  // The connection, or null for a game played alone.
  MultiplayerSession* multiplayer_ = nullptr;
  // The pending party action and who it is about, held so a cursor that moved
  // under the question cannot answer a different one.
  PartyAsk party_ask_ = PartyAsk::kNone;
  std::string party_target_;
  ConfirmPrompt party_prompt_;
  std::string party_prompt_question_;
  ContinuePrompt party_notice_prompt_;
  std::string party_notice_;
  bool party_notice_is_refusal_ = false;
  // The serial of the last notice raised, so one is shown once.
  int64_t party_notice_seen_ = 0;
  // What the clear card reads from, kept for as long as it is up.
  OfflineReport offline_report_;
  ContinuePrompt offline_prompt_;
  std::string boss_clear_title_;
  BossReward boss_clear_reward_;
  ContinuePrompt boss_clear_prompt_;
  // The fight in progress, and which catalog entry it is being fought
  // against, so the clear can be recorded under the same names the reset
  // clock reads.
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
