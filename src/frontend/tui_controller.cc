#include "src/frontend/tui_controller.h"

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/component/event.hpp"
#include "src/character/arcane_force.h"
#include "src/character/character.h"
#include "src/character/character_stats.h"
#include "src/character/consumables.h"
#include "src/character/dailies.h"
#include "src/character/job_advancement.h"
#include "src/character/progression.h"
#include "src/combat/encounter.h"
#include "src/frontend/panels/equipped_panel.h"
#include "src/frontend/panels/inventory_panel.h"
#include "src/frontend/screens/boss_select_panel.h"
#include "src/frontend/screens/map_select_panel.h"
#include "src/frontend/screens/scroll_panel.h"
#include "src/frontend/screens/star_force_panel.h"
#include "src/frontend/screens/trace_recover_panel.h"
#include "src/frontend/types.h"
#include "src/frontend/widgets/format.h"
#include "src/frontend/widgets/game_names.h"
#include "src/frontend/widgets/keys.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/item/item.h"
#include "src/multiplayer/trade_exchange.h"
#include "src/protos/boss.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/scroll.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {
namespace {

// What Accept answers when the bag could not hold their side. Wrapped by the
// notice itself, which is what puts it over several lines.
constexpr char kBagTooFullMessage[] =
    "Your inventory is too full to accept this trade.";

}  // namespace

namespace {

// Columns a notice's sentence is wrapped to. Wide enough that the longest of
// them takes two lines, narrow enough that neither line is a stub.
constexpr int kNoticeWidth = 34;

}  // namespace

TuiController::TuiController(GameState& state, Screens screens,
                             BattleAnalysis& analysis, KeyMap& keys,
                             int& panel_focus, MultiplayerSession* multiplayer)
    : state_(state),
      char_panel_(screens.char_panel),
      equip_panel_(screens.equip_panel),
      inventory_panel_(screens.inventory_panel),
      scroll_panel_(screens.scroll_panel),
      inspect_panel_(screens.inspect_panel),
      preview_inspect_panel_(screens.preview_inspect_panel),
      star_force_panel_(screens.star_force_panel),
      cube_panel_(screens.cube_panel),
      trace_recover_panel_(screens.trace_recover_panel),
      sell_panel_(screens.sell_panel),
      sell_equip_panel_(screens.sell_equip_panel),
      multi_sell_panel_(screens.multi_sell_panel),
      map_select_panel_(screens.map_select_panel),
      mob_inspect_panel_(screens.mob_inspect_panel),
      boss_select_panel_(screens.boss_select_panel),
      party_select_panel_(screens.party_select_panel),
      player_list_panel_(screens.player_list_panel),
      trade_panel_(screens.trade_panel),
      player_inspect_panel_(screens.player_inspect_panel),
      player_item_panel_(screens.player_item_panel),
      job_inspect_panel_(screens.job_inspect_panel),
      skill_inspect_panel_(screens.skill_inspect_panel),
      buff_info_panel_(screens.buff_info_panel),
      menu_panel_(screens.menu_panel),
      keybinds_panel_(screens.keybinds_panel),
      options_panel_(screens.options_panel),
      analysis_(analysis),
      keys_(keys),
      shop_panel_(screens.shop_panel),
      buy_panel_(screens.buy_panel),
      character_select_panel_(state),
      panel_focus_(panel_focus),
      multiplayer_(multiplayer) {
  // The Inspect screen's own panels answer Enter with these. Wired here
  // rather than by whoever built the screens: every one of them is a screen
  // this controller opens.
  PlayerInspectActions inspect_actions;
  inspect_actions.item = [this]() { OpenPlayerItemInspect(); };
  inspect_actions.skill = [this](const Skill& skill) {
    OpenPlayerSkillInspect(skill);
  };
  inspect_actions.hyper_stat = [this](HyperStatField field) {
    OpenPlayerHyperStatInspect(field);
  };
  inspect_actions.all_stats = [this]() { OpenPlayerAllStats(); };
  player_inspect_panel_.UseActions(std::move(inspect_actions));

  if (multiplayer_ != nullptr) {
    party_fight_ =
        std::make_unique<PartyFightAuthority>(multiplayer_->client());
  }
}

// Every screen that shows an inspect card opens it at the top, with the left
// half of the screen holding the arrows.
void TuiController::OpenInspectCards() {
  inspect_panel_.Reset();
  right_card_focused_ = false;
}

void TuiController::OpenEquipMenu() {
  screen_ = kItemMenu;
  equip_panel_.OpenMenu();
}

void TuiController::ToggleExpanded(int panel) {
  expanded_panel_ = expanded_panel_ == panel ? kNoPanel : panel;
}

void TuiController::OpenInventoryMenu() {
  if (inventory_panel_.on_shop_tab()) {
    shop_panel_.Reset();
    screen_ = kShop;
    return;
  }
  screen_ = kItemMenu;
  // Enter on a tab asks about the tab; Enter on a row asks about the item.
  if (inventory_panel_.on_tab_bar()) {
    inventory_panel_.OpenTabMenu();
  } else {
    inventory_panel_.OpenMenu();
  }
}

void TuiController::OpenApAllocate(StatField field) {
  ap_field_ = field;
  ap_selector_.Reset(state_.character.proto().ap());
  screen_ = kApAlloc;
}

void TuiController::OpenSkillLearn(const Skill& skill) {
  skill_learn_ = skill;
  sp_selector_.Reset(state_.character.LevelsAffordable(skill));
  screen_ = kSkillLearn;
}

void TuiController::OpenSkillMenu(const Skill& skill) {
  skill_menu_skill_ = skill;
  skill_menu_ =
      ItemMenu({"Inspect",
                state_.character.SkillToggledOn(skill.name()) ? "Deactivate"
                                                              : "Activate",
                "Close"});
  skill_menu_.Reset();
  if (!skill.toggle()) {
    // Hidden rather than dim: every other skill in the book is one there is
    // nothing to switch about, and a greyed row on all of them would advertise
    // something that is not coming.
    skill_menu_.Hide(kSkillMenuToggle);
  } else if (state_.character.skill_level(skill) <= 0) {
    skill_menu_.Disable(kSkillMenuToggle);
  }
  screen_ = kSkillMenu;
}

void TuiController::OpenSkillInspect(const Skill& skill) {
  skill_inspect_ = skill;
  card_from_inspect_ = false;
  skill_inspect_panel_.ResetScroll();
  screen_ = kSkillInspect;
}

// Whoever the open card is about: the player, or the party member behind the
// Inspect screen.
const CharacterInstance& TuiController::card_character() const {
  return card_from_inspect_ ? player_inspect_panel_.character()
                            : state_.character;
}

// Read LIVE rather than captured, so a point spent and then inspected again
// shows the level it is at. The learned level: what the card makes of the lent
// ones is its own business.
int TuiController::skill_inspect_level() const {
  return card_character().skill_level(skill_inspect_);
}

int TuiController::skill_inspect_bonus() const {
  return BonusSkillLevels(card_character(), state_.skills);
}

void TuiController::OpenAllStats() {
  screen_ = kAllStats;
}

void TuiController::RaiseHyperStat(HyperStatField field, StatPreset preset) {
  state_.character.AllocateHyperStat(field, preset);
}

void TuiController::LowerHyperStat(HyperStatField field, StatPreset preset) {
  state_.character.RefundHyperStat(field, preset);
}

void TuiController::OpenHyperStatInspect(HyperStatField field,
                                         StatPreset preset) {
  hyper_field_ = field;
  hyper_preset_ = preset;
  card_from_inspect_ = false;
  screen_ = kHyperStatInspect;
}

int TuiController::hyper_inspect_level() const {
  return card_character().hyper_stat_level(hyper_field_, hyper_preset_);
}

int TuiController::hyper_inspect_max_level() const {
  return card_character().max_hyper_stat_level();
}

void TuiController::OpenHyperReset(StatPreset preset) {
  hyper_preset_ = preset;
  // Opens on Cancel: the points come back, but the allocation they were spent
  // on does not, and it is fourteen rows of work.
  hyper_reset_prompt_.Open(/*cancel_selected=*/true);
  screen_ = kHyperReset;
}

void TuiController::OpenVMatrixReset() {
  // Opens on Cancel, as the Hyper question does: the points come back, but the
  // matrix they were spent on is a great deal more work than fourteen rows.
  v_matrix_reset_prompt_.Open(/*cancel_selected=*/true);
  screen_ = kVMatrixReset;
}

std::string TuiController::hyper_reset_question() const {
  // The chip's own name, so the question names what the row does. No mark:
  // which preset is in use is not what is being reset.
  return "Reset " +
         PresetSlotName(hyper_preset_, state_.character.autoswap_presets()) +
         " Hyper Stats?";
}

void TuiController::ToggleAbilityLock(int index, StatPreset preset) {
  const AbilityPreset& ability = state_.character.ability(preset);
  if (index < 0 || index >= ability.lines_size()) {
    return;
  }
  state_.character.LockAbilityLine(index, !ability.lines(index).locked(),
                                   preset);
}

void TuiController::OpenAbilityReroll(StatPreset preset) {
  ability_preset_ = preset;
  // Opens on Confirm: a player rerolling is rerolling repeatedly, and every
  // one of them costs the same honor whatever comes back.
  ability_reroll_prompt_.Open();
  screen_ = kAbilityReroll;
}

std::vector<AbilityLine> TuiController::ability_reroll_lines() const {
  std::vector<AbilityLine> lines;
  for (const AbilityLine& line :
       state_.character.ability(ability_preset_).lines()) {
    if (!line.locked()) {
      lines.push_back(line);
    }
  }
  return lines;
}

void TuiController::ToggleConsumable(ConsumableType type) {
  state_.character.ToggleConsumable(type);
}

void TuiController::OpenBuffMenu(ConsumableType type) {
  buff_type_ = type;
  buff_menu_.Reset();
  // The switch reads as what pressing it does, so the entry is named for the
  // state it would leave the buff in rather than for the state it is in.
  buff_menu_.SetLabel(kBuffMenuToggle, state_.character.ConsumableActive(type)
                                           ? "Disable"
                                           : "Enable");
  // A buff already bought has nothing left to buy. The entry stays on the menu
  // greyed rather than gone: its absence would be the surprise.
  if (state_.character.ConsumableOwned(type)) {
    buff_menu_.Disable(kBuffMenuBuyPerm);
  }
  screen_ = kBuffMenu;
}

void TuiController::OpenBuffBuy(ConsumableType type) {
  buff_type_ = type;
  // Opens on Cancel: buying a buff outright costs hundreds of millions, and
  // Enter alone must not be able to make it.
  buff_buy_prompt_.Open(/*cancel_selected=*/true);
  screen_ = kBuffBuy;
}

int64_t TuiController::buff_buy_price() const {
  const ConsumableInfo* info = ConsumableInfoFor(buff_type_);
  return info == nullptr ? 0 : info->permanent_price;
}

bool TuiController::buff_buy_affordable() const {
  return state_.character.proto().meso() >= buff_buy_price();
}

void TuiController::OpenJobMenu(Job job) {
  job_advance_ = job;
  job_menu_.Reset();
  screen_ = kJobMenu;
}

void TuiController::OpenPresetMenu(PresetKind kind, StatPreset slot) {
  preset_kind_ = kind;
  preset_slot_ = slot;
  preset_menu_.Reset();
  // Nothing to put in use while the autoswap is picking, and nothing to do to
  // the one already in use. The entry stays visible either way: its absence
  // would be the surprise.
  if (state_.character.autoswap_presets() ||
      state_.character.SlotInUse(kind) == slot) {
    preset_menu_.Disable(kPresetMenuUse);
  }
  screen_ = kPresetMenu;
}

bool TuiController::OnPresetMenuEvent(ftxui::Event event) {
  if (event == ftxui::Event::ArrowUp) {
    preset_menu_.Up();
    return true;
  }
  if (event == ftxui::Event::ArrowDown) {
    preset_menu_.Down();
    return true;
  }
  if (IsBack(event)) {
    screen_ = kMain;
    return true;
  }
  if (!IsForward(event)) {
    return true;  // The menu is modal: nothing behind it hears a key.
  }
  switch (preset_menu_.selected()) {
    case kPresetMenuUse:
      state_.character.SetSlotInUse(preset_kind_, preset_slot_);
      break;
    case kPresetMenuMove:
      // Opens on the preset the menu was raised on, which is the one a swap
      // with itself does nothing to.
      preset_move_row_ = IndexOf(preset_slot_);
      screen_ = kPresetMove;
      return true;
    default:
      break;
  }
  screen_ = kMain;
  return true;
}

bool TuiController::OnPresetMoveEvent(ftxui::Event event) {
  if (event == ftxui::Event::ArrowUp || event == ftxui::Event::ArrowDown) {
    // Cancel is the stop past the last preset, and the ring comes round.
    preset_move_row_ =
        StepCursor(preset_move_row_, event == ftxui::Event::ArrowUp ? -1 : 1,
                   kNumStatPresets + 1);
    return true;
  }
  if (IsBack(event)) {
    screen_ = kMain;
    return true;
  }
  if (!IsForward(event)) {
    return true;
  }
  if (preset_move_row_ < kNumStatPresets) {
    state_.character.SwapPresets(preset_kind_, preset_slot_,
                                 StatPresetAt(preset_move_row_));
  }
  screen_ = kMain;
  return true;
}

void TuiController::OpenJobAdvance(Job job) {
  job_advance_ = job;
  // Opens on Cancel: an advancement cannot be undone, so Enter alone must not
  // be able to pick a job the player was only reading.
  job_advance_prompt_.Open(/*cancel_selected=*/true);
  screen_ = kJobAdvance;
}

void TuiController::OpenMapSelect() {
  screen_ = kMapSelect;
  map_select_panel_.Reset();
}

bool TuiController::capturing_key() const {
  if (char_panel_.editing_username()) {
    return true;
  }
  return screen_ == kKeybinds && keybinds_panel_.capturing();
}

void TuiController::OpenMenuEntry(MenuEntry entry) {
  if (entry == MenuEntry::kDailies) {
    OpenDailies();
    return;
  }
  if (entry == MenuEntry::kCharacters) {
    // The fight stops the moment this opens -- see OnCharacterSelect -- and
    // the only way back into the game is to play somebody.
    character_select_panel_.Reset();
    screen_ = kCharacterSelect;
    return;
  }
  if (entry != MenuEntry::kBoss) {
    // The box opens with the cursor still on the entry below it, which is what
    // the player presses Up to leave.
    menu_panel_.OpenBox(entry);
    screen_ = kMenuBox;
    return;
  }
  // Opening the screen is what the gold was leading to, so it stops here.
  state_.account.MarkSeen(MenuPanel::boss_seen_key());
  screen_ = kBossSelect;
  boss_select_panel_.Reset();
}

bool TuiController::Connected() {
  MultiplayerSnapshot lobby = Lobby();
  if (lobby.state == ConnectionState::kConnected) {
    return true;
  }
  // Ask for a fresh attempt on the way out. Whatever turned the connection
  // away may be gone -- a server since deployed is the common one -- and
  // finding out should not cost the player a restart.
  if (multiplayer_ != nullptr) {
    multiplayer_->client().Reconnect();
  }
  // The notice stands over the main view rather than over the box that raised
  // it: closing it should land the player somewhere real.
  menu_panel_.CloseBox();
  screen_ = kMain;
  RaisePartyNotice(
      lobby.message.empty() ? "Could not reach the server." : lobby.message,
      /*refusal=*/true);
  return false;
}

void TuiController::LeaveMultiplayerScreen() {
  // Back to the box it was opened from, which is still standing where the
  // player left it -- the way Keybinds and Options close. Landing on the main
  // view instead leaves the box open with the cursor inside it, and the menu
  // row draws no cursor while that holds.
  screen_ = kMenuBox;
}

void TuiController::OpenPartySelect() {
  if (!Connected()) {
    return;
  }
  party_select_panel_.SetSnapshot(Lobby());
  party_select_panel_.Reset();
  screen_ = kPartySelect;
}

void TuiController::OpenPlayerList() {
  if (!Connected()) {
    return;
  }
  player_list_panel_.SetSnapshot(Lobby());
  player_list_panel_.Reset();
  screen_ = kPlayerList;
}

ItemRef TuiController::SelectedItem() const {
  if (panel_focus_ == kEquipPanel) {
    return ItemRef::Equipped(equip_panel_.selected_slot(),
                             equip_panel_.gear_preset());
  }
  return ItemRef::InBag(inventory_panel_.selected());
}

const EquipTabItem* TuiController::inspect_item() const {
  if (screen_ != kInspect) {
    return nullptr;
  }
  return subject_.Get(state_.character);
}

const ItemPrototype* TuiController::item_inspect_item() const {
  if (screen_ != kItemInspect) {
    return nullptr;
  }
  const std::vector<StackableItem>& stacks = state_.character.stackables();
  int index = inventory_panel_.selected_stack();
  if (index < 0 || index >= static_cast<int>(stacks.size())) {
    return nullptr;
  }
  return &stacks[index].prototype();
}

const EquipInstance* TuiController::scroll_item() const {
  if (screen_ != kScrollSelect && screen_ != kScrollResult) {
    return nullptr;
  }
  return subject_.GetInstance(state_.character);
}

// What the reader's own gear has in the slot `proto` would fill, or nullptr
// when the slot is empty -- and when the item is itself the one worn, which
// would be a card compared against a copy of itself. Against the preset the
// Equipped panel is showing: that is the gear the player is looking at, and
// the one Equip would displace.
StatPreset TuiController::ComparisonPreset() const {
  return equip_panel_.gear_preset();
}

const EquipInstance* TuiController::WornForComparison(
    const EquipPrototype& proto) const {
  EquipSlot slot = state_.character.SlotToFill(proto, ComparisonPreset());
  if (slot == EQUIP_SLOT_UNSPECIFIED) {
    return nullptr;
  }
  return state_.character.WornAt(ComparisonPreset(), slot);
}

std::optional<int> TuiController::CombatPowerDelta(
    const EquipTabItem* item) const {
  if (item == nullptr) {
    return std::nullopt;
  }
  const StatPreset gear = ComparisonPreset();
  if (state_.character.SlotToFill(item->prototype(), gear) ==
      EQUIP_SLOT_UNSPECIFIED) {
    return std::nullopt;
  }
  // The activity the preset stands for, so the Boss tab prices a piece by
  // what it is worth against a boss. The third preset is nobody's activity
  // and reads as the first does.
  const Activity activity =
      gear == StatPreset::kSecond ? Activity::kBossing : Activity::kFarming;
  const int now =
      CharacterCombatPower(state_.character, state_.skills, activity, gear);
  const int worn = CharacterCombatPower(state_.character.Wearing(*item, gear),
                                        state_.skills, activity, gear);
  return worn - now;
}

std::optional<int> TuiController::inspect_delta() const {
  // An item already on the character would replace itself, and a stackable is
  // worn by nobody. Neither has a figure to give.
  if (subject_.equipped()) {
    return std::nullopt;
  }
  return CombatPowerDelta(inspect_item());
}

std::optional<int> TuiController::player_item_delta() const {
  return CombatPowerDelta(player_inspect_panel_.selected_item());
}

const EquipTabItem* TuiController::inspect_comparison() const {
  // An item already on the character is the comparison, so there is nothing
  // to compare it with.
  if (subject_.equipped()) {
    return nullptr;
  }
  const EquipTabItem* item = inspect_item();
  if (item == nullptr) {
    return nullptr;
  }
  return WornForComparison(item->prototype());
}

// The reader's own item in the slot the member's cursor is on. By slot rather
// than by what it would displace: their gear is not going anywhere, and the
// question a reader is asking of it is what they wear in the same place.
const EquipTabItem* TuiController::player_item_comparison() const {
  EquipSlot slot = player_inspect_panel_.selected_slot();
  if (slot == EQUIP_SLOT_UNSPECIFIED) {
    return nullptr;
  }
  return state_.character.WornAt(ComparisonPreset(), slot);
}

// Keys on the main view, once every screen above it has had its say. A back
// key here means leaving the game, there being nothing left to back out of.
bool TuiController::OnMainViewEvent(ftxui::Event event) {
  // An open name field owns every key it can be handed: Escape leaves the
  // field rather than the game, and Tab must not carry focus off a panel
  // mid-edit. This only declines them so the panel gets them.
  if (char_panel_.editing_username()) {
    return false;
  }
  if (IsBack(event)) {
    if (expanded_panel_ != kNoPanel) {
      // An expanded panel fills the screen, so Escape closes it rather than
      // the game -- the same key [Close] is, one view at a time.
      expanded_panel_ = kNoPanel;
      return true;
    }
    OpenQuit();
    return true;
  }
  if (!IsSwitchPanel(event)) {
    return false;
  }
  if (expanded_panel_ != kNoPanel) {
    // Nothing to walk to: the other panels are not drawn.
    return true;
  }
  // Round the panels to the next one on screen; the character panel always is,
  // so this always lands. Backwards steps kNumPanels - 1 rather than -1, so
  // the modulo never sees a negative and both directions are one path.
  int step = event == ftxui::Event::Tab ? 1 : kNumPanels - 1;
  do {
    panel_focus_ = (panel_focus_ + step) % kNumPanels;
  } while (!PanelVisible(panel_focus_));
  // Arriving on a panel is reading whatever tab was left open on it. Without
  // this, a gold tab the player is already standing on could only be cleared
  // by arrowing off it and back.
  if (panel_focus_ == kInventoryPanel) {
    inventory_panel_.MarkActiveTabSeen();
  }
  if (panel_focus_ == kCharPanel) {
    char_panel_.MarkActiveTabSeen();
  }
  return true;
}

bool TuiController::OnEvent(ftxui::Event event) {
  // A panel can go out from under the cursor: the game starts focused on the
  // equipped panel, which a level 1 character has not unlocked. Settled before
  // dispatch so a key never reaches a panel that is not drawn.
  EnsureFocusIsVisible();
  // A rank up is gold until the player's next act, counted here. Cleared
  // BEFORE dispatch, so the keypress that rolls the rank sets it back on its
  // way through. Custom is the ticker's redraw and is nobody acting.
  if (event != ftxui::Event::Custom) {
    ability_rank_up_ = false;
    cube_panel_.SetRankUp(false);
  }
  // The notice floats over whatever is on screen, so it takes keys before the
  // screen under it gets a look.
  if (party_notice_prompt_.open()) {
    party_notice_prompt_.OnEvent(event);
    return true;
  }
  switch (screen_) {
    case kItemMenu:
      return OnItemMenuEvent(event);
    // Both are one screen to the player: anything at all closes it.
    case kInspect:
    case kItemInspect:
      return OnInspectEvent(event);
    case kScrollSelect:
      return OnScrollSelectEvent(event);
    case kScrollResult:
      return OnScrollResultEvent(event);
    case kApAlloc:
      return OnApAllocEvent(event);
    case kSkillLearn:
      return OnSkillLearnEvent(event);
    case kSkillMenu:
      return OnSkillMenuEvent(event);
    // Both are screens with nothing to do but read them, so they close the
    // same way.
    case kSkillInspect:
    case kAllStats:
    case kHyperStatInspect:
      return OnSkillInspectEvent(event);
    case kJobMenu:
      return OnJobMenuEvent(event);
    case kPresetMenu:
      return OnPresetMenuEvent(event);
    case kPresetMove:
      return OnPresetMoveEvent(event);
    case kBuffMenu:
      return OnBuffMenuEvent(event);
    case kBuffInfo:
      return OnBuffInfoEvent(event);
    case kBuffBuy:
      return OnBuffBuyEvent(event);
    case kJobInspect:
      return OnJobInspectEvent(event);
    case kJobAdvance:
      return OnJobAdvanceEvent(event);
    case kStarForce:
      return OnStarForceEvent(event);
    case kCubing:
      return OnCubeEvent(event);
    case kStarForceResult:
      return OnStarForceResultEvent(event);
    case kHammer:
      return OnHammerEvent(event);
    case kTraceRecover:
      return OnTraceRecoverEvent(event);
    case kTraceRecoverResult:
      return OnTraceRecoverResultEvent(event);
    case kSell:
      return OnSellEvent(event);
    case kSellEquip:
      return OnSellEquipEvent(event);
    case kSymbolLevel:
      return OnSymbolLevelEvent(event);
    case kHyperReset:
      return OnHyperResetEvent(event);
    case kVMatrixReset:
      return OnVMatrixResetEvent(event);
    case kAbilityReroll:
      return OnAbilityRerollEvent(event);
    case kSymbolCombine:
      return OnSymbolCombineEvent(event);
    case kMultiSell:
      return OnMultiSellEvent(event);
    case kMapSelect:
      return OnMapSelectEvent(event);
    case kMapMenu:
      return OnMapMenuEvent(event);
    case kMobInspect:
      return OnMobInspectEvent(event);
    case kPlayerList:
      return OnPlayerListEvent(event);
    case kPlayerMenu:
      return OnPlayerMenuEvent(event);
    case kTrade:
      return OnTradeEvent(event);
    case kTradeAmount:
      return OnTradeAmountEvent(event);
    case kTradeMenu:
      return OnTradeMenuEvent(event);
    case kTradeItemAmount:
      return OnTradeItemAmountEvent(event);
    case kTradeConfirm:
      return OnTradeConfirmEvent(event);
    case kTradeLeave:
      return OnTradeLeaveEvent(event);
    case kTradeInspect:
      return OnCardEvent(event, inspect_panel_, kTrade);
    case kPartySelect:
      return OnPartySelectEvent(event);
    case kPartyMenu:
      return OnPartyMenuEvent(event);
    case kPlayerInspect:
      return OnPlayerInspectEvent(event);
    case kPlayerItemInspect:
      return OnPlayerItemInspectEvent(event);
    case kPlayerAllStats:
      return OnPlayerAllStatsEvent(event);
    case kPartyConfirm:
      return OnPartyConfirmEvent(event);
    case kBossSelect:
      return OnBossSelectEvent(event);
    case kBossConfirm:
      return OnBossConfirmEvent(event);
    case kBossNotice:
      return OnBossNoticeEvent(event);
    case kBossFight:
      return OnBossFightEvent(event);
    case kBossAbort:
      return OnBossAbortEvent(event);
    case kBossClear:
      return OnBossClearEvent(event);
    case kShop:
      return OnShopEvent(event);
    case kShopMenu:
      return OnShopMenuEvent(event);
    case kShopInspect:
      return OnShopInspectEvent(event);
    case kShopBuy:
      return OnShopBuyEvent(event);
    case kDailies:
      return OnDailiesEvent(event);
    case kDailiesNotice:
      return OnDailiesNoticeEvent(event);
    case kMenuBox:
      return OnMenuBoxEvent(event);
    case kAnalysis:
      return OnAnalysisEvent(event);
    case kCharacterSelect:
      return OnCharacterSelectEvent(event);
    case kCharacterMenu:
      return OnCharacterMenuEvent(event);
    case kCharacterDelete:
      return OnCharacterDeleteEvent(event);
    case kKeybinds:
      return OnKeybindsEvent(event);
    case kOptions:
      return OnOptionsEvent(event);
    case kOffline:
      return OnOfflineEvent(event);
    case kQuit:
      return OnQuitEvent(event);
    case kMain:
      break;
  }
  return OnMainViewEvent(event);
}

bool TuiController::PanelVisible(int panel) const {
  if (panel == kEquipPanel) {
    return Unlocked(Feature::kEquipped, state_.character, state_.account);
  }
  if (panel == kInventoryPanel) {
    return Unlocked(Feature::kBag, state_.character, state_.account);
  }
  if (panel == kMenuPanel) {
    return Unlocked(Feature::kMenu, state_.character, state_.account);
  }
  return true;
}

void TuiController::EnsureFocusIsVisible() {
  int guard = 0;
  while (!PanelVisible(panel_focus_) && guard < kNumPanels) {
    panel_focus_ = (panel_focus_ + 1) % kNumPanels;
    ++guard;
  }
}

Screen TuiController::SeedUpgradeScreen(Screen next) {
  if (next != kScrollSelect && next != kStarForce && next != kCubing &&
      next != kHammer) {
    return next;
  }
  // All four are about the item under the cursor, settled here so nothing
  // downstream asks which panel had focus.
  subject_ = SelectedItem();
  if (next == kScrollSelect) {
    OpenInspectCards();
  }
  if (next == kStarForce) {
    star_force_panel_.ResetConfirm();
    preview_inspect_panel_.Reset();
    OpenInspectCards();
  }
  if (next == kCubing) {
    cube_panel_.Reset();
    OpenInspectCards();
  }
  if (next == kHammer) {
    hammer_panel_.Reset(state_.character.meso());
  }
  return next;
}

Screen TuiController::SeedSaleScreen(Screen next) {
  if (next == kTraceRecover) {
    preview_inspect_panel_.Reset();
    OpenInspectCards();
    bag_row_ = inventory_panel_.selected();
    trace_recover_panel_.SetTrace(&state_.character.inventory()[bag_row_]);
  }
  if (next == kSellEquip) {
    bag_row_ = inventory_panel_.selected();
    const EquipTabItem& item = state_.character.inventory()[bag_row_];
    // A trace pays nothing whatever its prototype says, so the dialog is told
    // what the sale will really hand over rather than what the item cost.
    bool is_trace =
        state_.character.inventory().equip_instance(bag_row_) == nullptr;
    int price = is_trace ? 0 : SellPrice(item.prototype());
    sell_equip_panel_.Reset(item.name(), price);
  }
  if (next == kMultiSell) {
    int tab = inventory_panel_.active_tab();
    multi_sell_panel_.Reset(tab, tab == kEquipTab
                                     ? inventory_panel_.selected()
                                     : inventory_panel_.selected_stack());
  }
  if (next == kSell) {
    sell_index_ = inventory_panel_.selected_stack();
    if (sell_index_ < 0) {
      return kMain;  // the row went out from under the menu
    }
    const StackableItem& stack = state_.character.stackables()[sell_index_];
    sell_panel_.Reset(stack.name(), stack.prototype().sell_price(),
                      stack.count());
  }
  return next;
}

Screen TuiController::SeedSymbolScreen(Screen next) {
  if (next == kSymbolLevel) {
    symbol_slot_ = equip_panel_.selected_slot();
    const EquipInstance& symbol = *state_.character.equipped().at(symbol_slot_);
    int level = SymbolLevel(symbol.equip_state());
    symbol_level_panel_.Reset(symbol.prototype().name(), level,
                              SymbolLevelUpCost(symbol.prototype(), level),
                              state_.character.meso());
  }
  if (next == kSymbolCombine) {
    // Keyed off the spare's own slot: what it can be fed to is the symbol of
    // its area, whichever bag row the cursor happened to be on.
    symbol_slot_ = state_.character.inventory()[inventory_panel_.selected()]
                       .prototype()
                       .equip_slot();
    const EquipInstance& worn = *state_.character.equipped().at(symbol_slot_);
    int level = SymbolLevel(worn.equip_state());
    symbol_combine_panel_.Reset(
        worn.prototype().name(), level, worn.equip_state().symbol_exp(),
        SymbolExpToNextLevel(level),
        state_.character.SpareSymbolWorths(symbol_slot_));
  }
  return next;
}

bool TuiController::OnItemMenuEvent(ftxui::Event event) {
  Screen next;
  if (panel_focus_ == kEquipPanel) {
    next = equip_panel_.OnMenuEvent(event, scroll_panel_);
  } else if (inventory_panel_.on_tab_bar()) {
    next = inventory_panel_.OnTabMenuEvent(event);
  } else {
    next = inventory_panel_.OnMenuEvent(event, scroll_panel_,
                                        equip_panel_.gear_preset());
  }
  if (next == kInspect) {
    subject_ = SelectedItem();
  }
  if (next == kInspect || next == kItemInspect) {
    OpenInspectCards();
  }
  next = SeedUpgradeScreen(next);
  next = SeedSaleScreen(next);
  next = SeedSymbolScreen(next);
  screen_ = next;
  return true;
}

// Reading is all there is to do on any of the inspect screens, so either of
// Confirm and Cancel leaves for `back`. The arrows move whichever card holds
// them -- sideways too, for a card squeezed narrower than its rows -- and Tab
// hands them to the next card along, whenever there is one. Everything else
// is swallowed: these are modal screens.
bool TuiController::OnCardEvent(ftxui::Event event, InspectPanel& panel,
                                Screen back) {
  if (event == ftxui::Event::ArrowUp) {
    panel.ScrollBy(-1);
    return true;
  }
  if (event == ftxui::Event::ArrowDown) {
    panel.ScrollBy(1);
    return true;
  }
  if (event == ftxui::Event::ArrowLeft) {
    panel.ScrollXBy(-1);
    return true;
  }
  if (event == ftxui::Event::ArrowRight) {
    panel.ScrollXBy(1);
    return true;
  }
  if (IsSwitchPanel(event)) {
    panel.SwapCard(event == ftxui::Event::Tab ? 1 : -1);
    return true;
  }
  if (IsBack(event) || IsForward(event)) {
    screen_ = back;
  }
  return true;
}

bool TuiController::OnInspectEvent(ftxui::Event event) {
  return OnCardEvent(event, inspect_panel_, kMain);
}

bool TuiController::OnScrollSelectEvent(ftxui::Event event) {
  bool busy = scroll_panel_.IsConfirming() || scroll_panel_.IsMenuOpen();
  // The item card takes the arrows in turn with the scroll list. Held back
  // while a dialog is up: the keys are its own until it closes.
  if (!busy && IsSwitchPanel(event)) {
    right_card_focused_ = !right_card_focused_;
    return true;
  }
  if (!busy && right_card_focused_ &&
      (event == ftxui::Event::ArrowUp || event == ftxui::Event::ArrowDown)) {
    inspect_panel_.ScrollBy(event == ftxui::Event::ArrowUp ? -1 : 1);
    return true;
  }
  if (IsBack(event) && !scroll_panel_.IsConfirming() &&
      !scroll_panel_.IsMenuOpen()) {
    if (panel_focus_ == kEquipPanel) {
      equip_panel_.OpenMenu();
    } else {
      inventory_panel_.OpenMenu();
    }
    screen_ = kItemMenu;
    return true;
  }
  ConfirmChoice choice = scroll_panel_.OnEvent(event);
  if (scroll_panel_.TakePinToggled()) {
    // The panel reads the pin but never writes it: the record is the
    // character's and rides the save.
    state_.character.ToggleScrollPin(scroll_panel_.PinKeyOfSelected());
    scroll_panel_.Resort();
  }
  if (scroll_panel_.TakeScrollChosen()) {
    // Asked before the confirm window opens, so a scroll with nowhere to go
    // says so rather than asking the player to pay first.
    const EquipInstance* item = scroll_item();
    const Scroll& scroll = scroll_panel_.selected_scroll();
    int remaining = item->equip_state().remaining_upgrade_slots();
    bool no_slots;
    if (scroll.scroll_category() == SCROLL_CATEGORY_CLEAN_SLATE) {
      int cap = TotalUpgradeSlots(item->prototype(), item->equip_state()) -
                item->equip_state().scroll_successes();
      no_slots = remaining >= cap;
    } else {
      no_slots = remaining == 0;
    }
    if (no_slots) {
      scroll_result_ = {kScrollNoSlots, item->prototype().name(), scroll.name(),
                        remaining, scroll.scroll_category()};
      OpenNotice(kScrollResult);
      return true;
    }
    scroll_panel_.OpenConfirm();
  }
  if (choice == ConfirmChoice::kConfirmed) {
    const EquipInstance* item = subject_.GetInstance(state_.character);
    std::string equip_name = item->prototype().name();
    const Scroll& scroll = scroll_panel_.selected_scroll();
    // Paid for before it is used, and only used if it was paid for. The panel
    // will not confirm what the player cannot afford, so this refusing is a
    // second line rather than the first.
    if (!state_.character.SpendItem(kSpellTraceName,
                                    scroll_panel_.CostOfSelected())) {
      return true;
    }
    ScrollOutcome outcome = ScrollItem(state_.character, subject_, scroll);
    int slots_remaining =
        item ? item->equip_state().remaining_upgrade_slots() : 0;
    scroll_result_ = {outcome, equip_name, scroll.name(), slots_remaining,
                      scroll.scroll_category()};
    OpenNotice(kScrollResult);
  }
  return true;
}

bool TuiController::OnScrollResultEvent(ftxui::Event event) {
  if (notice_prompt_.OnEvent(event)) {
    screen_ = kScrollSelect;
  }
  return true;
}

bool TuiController::OnApAllocEvent(ftxui::Event event) {
  ConfirmChoice choice = ap_selector_.OnEvent(event);
  if (choice == ConfirmChoice::kConfirmed) {
    state_.character.AllocateStat(ap_field_, ap_selector_.value());
    screen_ = kMain;
  } else if (choice == ConfirmChoice::kCancelled) {
    screen_ = kMain;
  }
  return true;
}

bool TuiController::OnSkillLearnEvent(ftxui::Event event) {
  ConfirmChoice choice = sp_selector_.OnEvent(event);
  if (choice == ConfirmChoice::kConfirmed) {
    state_.character.LearnSkill(skill_learn_, sp_selector_.value());
    screen_ = kMain;
  } else if (choice == ConfirmChoice::kCancelled) {
    screen_ = kMain;
  }
  return true;
}

bool TuiController::OnSkillMenuEvent(ftxui::Event event) {
  if (event == ftxui::Event::ArrowUp) {
    skill_menu_.Up();
    return true;
  }
  if (event == ftxui::Event::ArrowDown) {
    skill_menu_.Down();
    return true;
  }
  if (IsBack(event)) {
    screen_ = kMain;
    return true;
  }
  if (!IsForward(event)) {
    return true;  // The menu is modal: nothing behind it hears a key.
  }
  switch (skill_menu_.selected()) {
    case kSkillMenuInspect:
      OpenSkillInspect(skill_menu_skill_);
      break;
    case kSkillMenuToggle:
      // The switch is thrown and the menu closes: what it changed is the book
      // behind it, which the player wants to see.
      state_.character.ToggleSkill(skill_menu_skill_);
      screen_ = kMain;
      break;
    default:
      screen_ = kMain;
      break;
  }
  return true;
}

// Reading is all there is to do here, so either key leaves -- the same way the
// item inspect screen closes. Shared with the All Stats screen, which has no
// card to scroll.
bool TuiController::OnSkillInspectEvent(ftxui::Event event) {
  if (screen_ == kSkillInspect) {
    if (event == ftxui::Event::ArrowUp) {
      skill_inspect_panel_.ScrollBy(-1);
      return true;
    }
    if (event == ftxui::Event::ArrowDown) {
      skill_inspect_panel_.ScrollBy(1);
      return true;
    }
  }
  if (IsBack(event) || IsForward(event)) {
    // Back onto whichever screen raised the card: the player's own panels, or
    // the Inspect screen, which raises the same two over a member's numbers.
    screen_ = card_from_inspect_ ? kPlayerInspect : kMain;
  }
  return true;
}

void TuiController::OpenQuit() {
  // Opened on Cancel: leaving is not what an accidental Escape means, and a
  // stray Enter behind one should not end the session.
  quit_prompt_.Open(/*cancel_selected=*/true);
  quit_return_ = OnCharacterSelect() ? kCharacterSelect : kMain;
  screen_ = kQuit;
}

bool TuiController::OnQuitEvent(ftxui::Event event) {
  ConfirmChoice choice = quit_prompt_.OnEvent(event);
  if (choice == ConfirmChoice::kConfirmed) {
    // Only raised, never acted on here. Tui owns the ftxui screen and is the
    // only thing that can end its loop.
    quit_requested_ = true;
    screen_ = kMain;
  } else if (choice == ConfirmChoice::kCancelled) {
    // Back where it was asked. The character select has no other way out, so
    // cancelling there must not drop the player into a game they have not
    // chosen a character for.
    screen_ = quit_return_;
  }
  return true;
}

bool TuiController::TakeCharacterSwitch() {
  bool switched = character_switched_;
  character_switched_ = false;
  return switched;
}

bool TuiController::OnCharacterSelectEvent(ftxui::Event event) {
  if (event == ftxui::Event::ArrowUp) {
    character_select_panel_.MoveCursor(-1);
    return true;
  }
  if (event == ftxui::Event::ArrowDown) {
    character_select_panel_.MoveCursor(1);
    return true;
  }
  if (event == ftxui::Event::ArrowLeft) {
    character_select_panel_.MoveButton(-1);
    return true;
  }
  if (event == ftxui::Event::ArrowRight) {
    character_select_panel_.MoveButton(1);
    return true;
  }
  if (IsForward(event)) {
    switch (character_select_panel_.Chosen()) {
      case CharacterAction::kMenu:
        character_select_panel_.OpenMenu();
        screen_ = kCharacterMenu;
        return true;
      case CharacterAction::kCreate:
        CreateCharacter(state_);
        LeaveCharacterSelect(/*switched=*/true);
        return true;
      case CharacterAction::kQuit:
        OpenQuit();
        return true;
    }
    return true;
  }
  if (IsBack(event)) {
    // Escape asks the same question the Quit button does: there is nothing
    // behind this screen to go back to.
    OpenQuit();
    return true;
  }
  // Swallow everything else: this is a modal screen.
  return true;
}

bool TuiController::OnCharacterMenuEvent(ftxui::Event event) {
  if (event == ftxui::Event::ArrowUp) {
    character_select_panel_.MoveMenuCursor(-1);
    return true;
  }
  if (event == ftxui::Event::ArrowDown) {
    character_select_panel_.MoveMenuCursor(1);
    return true;
  }
  if (IsForward(event)) {
    TakeCharacterMenuEntry();
    return true;
  }
  if (IsBack(event)) {
    character_select_panel_.CloseMenu();
    screen_ = kCharacterSelect;
    return true;
  }
  return true;
}

void TuiController::TakeCharacterMenuEntry() {
  int slot = character_select_panel_.selected_slot();
  switch (character_select_panel_.menu_selected()) {
    case kCharacterMenuPlay:
      // On the character already in play this is a resume, and the fight
      // they left going is still theirs.
      LeaveCharacterSelect(PlayCharacter(state_, slot));
      return;
    case kCharacterMenuSetOffline:
      SetOfflineCharacter(state_, slot);
      // Straight back to the list, where the check has moved: nothing about
      // it needs confirming, and seeing it move is the answer.
      character_select_panel_.Refresh();
      screen_ = kCharacterSelect;
      save_wanted_ = true;
      return;
    case kCharacterMenuDelete:
      character_delete_slot_ = slot;
      // The menu goes away behind the question: what is being asked about is
      // the row, and the entry that asked has been pressed.
      character_select_panel_.CloseMenu();
      character_delete_prompt_.Open(/*cancel_selected=*/true);
      screen_ = kCharacterDelete;
      return;
    default:
      character_select_panel_.CloseMenu();
      screen_ = kCharacterSelect;
      return;
  }
}

bool TuiController::OnCharacterDeleteEvent(ftxui::Event event) {
  ConfirmChoice choice = character_delete_prompt_.OnEvent(event);
  if (choice == ConfirmChoice::kPending) {
    return true;
  }
  if (choice == ConfirmChoice::kConfirmed &&
      DeleteCharacter(state_, character_delete_slot_)) {
    // Deleting whoever was being played put somebody else in, so the fight
    // and the watcher are told either way -- and the save goes out now, so
    // an autosave cannot bring the row back.
    character_switched_ = true;
  }
  character_delete_slot_ = -1;
  character_select_panel_.Refresh();
  screen_ = kCharacterSelect;
  save_wanted_ = true;
  return true;
}

void TuiController::LeaveCharacterSelect(bool switched) {
  if (switched) {
    character_switched_ = true;
    // Whoever arrived is standing on their own map with their own panels, so
    // the cursor starts where a session starts.
    panel_focus_ = kEquipPanel;
  }
  save_wanted_ = true;
  character_select_panel_.CloseMenu();
  screen_ = kMain;
}

bool TuiController::OnBuffMenuEvent(ftxui::Event event) {
  if (event == ftxui::Event::ArrowUp) {
    buff_menu_.Up();
    return true;
  }
  if (event == ftxui::Event::ArrowDown) {
    buff_menu_.Down();
    return true;
  }
  if (IsBack(event)) {
    screen_ = kMain;
    return true;
  }
  if (!IsForward(event)) {
    return true;  // The menu is modal: nothing behind it hears a key.
  }
  switch (buff_menu_.selected()) {
    case kBuffMenuToggle:
      // Nothing to confirm and nothing to spend, so the switch takes effect on
      // the keypress and the menu closes behind it.
      ToggleConsumable(buff_type_);
      screen_ = kMain;
      break;
    case kBuffMenuInspect:
      buff_info_panel_.SetBuff(buff_type_,
                               state_.character.ConsumableOwned(buff_type_));
      screen_ = kBuffInfo;
      break;
    case kBuffMenuBuyPerm:
      OpenBuffBuy(buff_type_);
      break;
    default:
      screen_ = kMain;
      break;
  }
  return true;
}

// Nothing to point at, only text to read, and the card is short enough that
// nothing scrolls. Back returns to the menu it was opened from, as the job
// card does: the decision is one keypress away there.
bool TuiController::OnBuffInfoEvent(ftxui::Event event) {
  if (IsBack(event)) {
    screen_ = kBuffMenu;
  }
  return true;
}

bool TuiController::OnBuffBuyEvent(ftxui::Event event) {
  ConfirmChoice choice = buff_buy_prompt_.OnEvent(event, buff_buy_affordable());
  if (choice == ConfirmChoice::kPending) {
    return true;
  }
  if (choice == ConfirmChoice::kConfirmed) {
    state_.character.BuyConsumable(buff_type_);
  }
  screen_ = kMain;
  return true;
}

bool TuiController::OnJobMenuEvent(ftxui::Event event) {
  if (event == ftxui::Event::ArrowUp) {
    job_menu_.Up();
    return true;
  }
  if (event == ftxui::Event::ArrowDown) {
    job_menu_.Down();
    return true;
  }
  if (IsBack(event)) {
    screen_ = kMain;
    return true;
  }
  if (!IsForward(event)) {
    return true;  // The menu is modal: nothing behind it hears a key.
  }
  switch (job_menu_.selected()) {
    case kJobMenuInspect:
      job_inspect_panel_.SetJob(job_advance_, job_advance_stage());
      screen_ = kJobInspect;
      break;
    case kJobMenuAdvance:
      OpenJobAdvance(job_advance_);
      break;
    default:
      screen_ = kMain;
      break;
  }
  return true;
}

// Read-only, so Up and Down are the whole of it. Back returns to the menu the
// screen was opened from rather than to the main view: the player came here to
// decide, and the decision is one keypress away on the menu.
bool TuiController::OnJobInspectEvent(ftxui::Event event) {
  if (event == ftxui::Event::ArrowUp) {
    job_inspect_panel_.MoveCursor(-1);
    return true;
  }
  if (event == ftxui::Event::ArrowDown) {
    job_inspect_panel_.MoveCursor(1);
    return true;
  }
  if (IsBack(event)) {
    screen_ = kJobMenu;
  }
  return true;
}

bool TuiController::OnJobAdvanceEvent(ftxui::Event event) {
  ConfirmChoice choice = job_advance_prompt_.OnEvent(event);
  if (choice == ConfirmChoice::kConfirmed) {
    PerformJobAdvancement(state_, job_advance_);
    screen_ = kMain;
  } else if (choice == ConfirmChoice::kCancelled) {
    screen_ = kMain;
  }
  return true;
}

const EquipInstance* TuiController::star_force_item() const {
  if (screen_ != kStarForce) {
    return nullptr;
  }
  return subject_.GetInstance(state_.character);
}

bool TuiController::OnStarForceEvent(ftxui::Event event) {
  bool busy = star_force_panel_.IsConfirming();
  if (IsBack(event) && !busy) {
    screen_ = kMain;
    return true;
  }
  const EquipInstance* item = star_force_item();
  // The item as it stands on the left, the item one star on to the right.
  // Tab picks which of the two the up and down arrows scroll; left and right
  // stay the button row's, so the two never argue over a key.
  bool has_after = item->stars() < item->max_stars();
  if (!busy && has_after && IsSwitchPanel(event)) {
    right_card_focused_ = !right_card_focused_;
    return true;
  }
  if (!busy &&
      (event == ftxui::Event::ArrowUp || event == ftxui::Event::ArrowDown)) {
    int delta = event == ftxui::Event::ArrowUp ? -1 : 1;
    if (right_card_focused_) {
      preview_inspect_panel_.ScrollBy(delta);
    } else {
      inspect_panel_.ScrollBy(delta);
    }
    return true;
  }
  if (!has_after) {
    return true;
  }
  ConfirmChoice choice = star_force_panel_.OnEvent(event);
  if (choice == ConfirmChoice::kCancelled) {
    screen_ = kMain;
    return true;
  }
  if (choice == ConfirmChoice::kConfirmed) {
    std::string equip_name = item->prototype().name();
    int stars_before = item->stars();
    StarForceOutcome outcome = StarForceItem(state_.character, subject_);
    int stars_after = stars_before + (outcome == kStarForceSuccess ? 1 : 0);
    star_force_result_ = {outcome, equip_name, stars_before, stars_after};
    OpenNotice(kStarForceResult);
  }
  return true;
}

const EquipInstance* TuiController::cube_item() const {
  if (screen_ != kCubing) {
    return nullptr;
  }
  return subject_.GetInstance(state_.character);
}

bool TuiController::OnCubeEvent(ftxui::Event event) {
  // Told before it is asked: the panel greys its own Confirm off the purse,
  // and what the purse holds is not something a keypress should have to wait
  // for the next render to learn.
  cube_panel_.SetItem(cube_item(), state_.character.meso());
  bool busy = cube_panel_.IsConfirming();
  if (IsBack(event) && !busy) {
    screen_ = kMain;
    return true;
  }
  // Tab hands the arrows to the card beside the shelf, as the star force
  // screen does: a Legendary potential is more rows than a small terminal has.
  if (!busy && IsSwitchPanel(event)) {
    right_card_focused_ = !right_card_focused_;
    return true;
  }
  if (!busy &&
      (event == ftxui::Event::ArrowUp || event == ftxui::Event::ArrowDown)) {
    int delta = event == ftxui::Event::ArrowUp ? -1 : 1;
    if (right_card_focused_) {
      inspect_panel_.ScrollBy(delta);
    } else {
      cube_panel_.MoveCursor(delta);
    }
    return true;
  }
  if (cube_panel_.OnEvent(event) == ConfirmChoice::kConfirmed) {
    // The window stays up over the item it just rerolled, which is the whole
    // point of it: the player watches the lines change and presses again.
    const PotentialRank before = cube_item()->potential().rank();
    CubeItem(state_.character, subject_, cube_panel_.selected_cube());
    // The first cube into a bare item always hands over a Rare potential, so
    // it is a grant rather than a rank up and the window stays steel blue.
    cube_panel_.SetRankUp(before != POTENTIAL_RANK_UNSPECIFIED &&
                          cube_item()->potential().rank() > before);
  }
  return true;
}

bool TuiController::OnHammerEvent(ftxui::Event event) {
  ConfirmChoice choice = hammer_panel_.OnEvent(event);
  if (choice == ConfirmChoice::kPending) {
    return true;
  }
  if (choice == ConfirmChoice::kConfirmed) {
    HammerItem(state_.character, subject_);
  }
  screen_ = kMain;
  return true;
}

bool TuiController::OnStarForceResultEvent(ftxui::Event event) {
  if (notice_prompt_.OnEvent(event)) {
    screen_ =
        star_force_result_.outcome == kStarForceDestroy ? kMain : kStarForce;
  }
  return true;
}

const EquipTabItem* TuiController::trace_recover_item() const {
  if (screen_ != kTraceRecover) {
    return nullptr;
  }
  return &state_.character.inventory()[bag_row_];
}

bool TuiController::OnTraceRecoverEvent(ftxui::Event event) {
  bool busy = trace_recover_panel_.IsConfirming();
  // Two cards, and the chips between them answer to Left and Right. Tab picks
  // which card the arrows scroll; the recovered item's is where the reader
  // starts.
  if (!busy && IsSwitchPanel(event)) {
    right_card_focused_ = !right_card_focused_;
    return true;
  }
  if (!busy &&
      (event == ftxui::Event::ArrowUp || event == ftxui::Event::ArrowDown)) {
    int delta = event == ftxui::Event::ArrowUp ? -1 : 1;
    if (right_card_focused_) {
      inspect_panel_.ScrollBy(delta);
    } else {
      preview_inspect_panel_.ScrollBy(delta);
    }
    return true;
  }
  if (IsBack(event) && !trace_recover_panel_.IsConfirming()) {
    screen_ = kItemMenu;
    return true;
  }
  if (trace_recover_panel_.OnEvent(event) == ConfirmChoice::kConfirmed) {
    int base_index = trace_recover_panel_.selected_index();
    std::string equip_name =
        state_.character.inventory()[bag_row_].prototype().name();
    int stars_recovered = state_.character.RecoverTrace(bag_row_, base_index);
    trace_recovery_result_ = {equip_name, stars_recovered};
    OpenNotice(kTraceRecoverResult);
  }
  return true;
}

bool TuiController::OnTraceRecoverResultEvent(ftxui::Event event) {
  if (notice_prompt_.OnEvent(event)) {
    screen_ = kMain;
  }
  return true;
}

bool TuiController::OnMapSelectEvent(ftxui::Event event) {
  if (event == ftxui::Event::ArrowUp) {
    map_select_panel_.MoveCursor(-1);
    return true;
  }
  if (event == ftxui::Event::ArrowDown) {
    map_select_panel_.MoveCursor(1);
    return true;
  }
  // Left and Right belong to the chip bar. The panel holds that rule, so it
  // stays true of every caller rather than of this one handler.
  if (event == ftxui::Event::ArrowLeft) {
    map_select_panel_.ChangePage(-1);
    return true;
  }
  if (event == ftxui::Event::ArrowRight) {
    map_select_panel_.ChangePage(1);
    return true;
  }
  if (IsForward(event)) {
    // The menu decides what happens to the map: going there is one of three
    // things the player might want with it.
    map_select_panel_.OpenMenu();
    if (map_select_panel_.menu_open()) {
      screen_ = kMapMenu;
    }
    return true;
  }
  if (IsBack(event)) {
    screen_ = kMain;
    return true;
  }
  // Swallow everything else: this is a modal screen.
  return true;
}

bool TuiController::OnMapMenuEvent(ftxui::Event event) {
  Screen next = map_select_panel_.OnMenuEvent(event);
  if (next == kMain) {
    // Move. Travel is free, so the highlighted map is always a legal
    // destination, and the fight restarts on its own once it sees the new one.
    std::string map = map_select_panel_.selected_map();
    if (!map.empty()) {
      state_.current_map = map;
    }
  } else if (next == kMobInspect) {
    mob_inspect_panel_.SetMap(map_select_panel_.selected_map());
  }
  screen_ = next;
  return true;
}

bool TuiController::OnMobInspectEvent(ftxui::Event event) {
  if (event == ftxui::Event::ArrowUp) {
    mob_inspect_panel_.MoveCursor(-1);
    return true;
  }
  if (event == ftxui::Event::ArrowDown) {
    mob_inspect_panel_.MoveCursor(1);
    return true;
  }
  // Back to the list it was opened from, so a player reading round a band's
  // mobs is not sent home between each one.
  if (IsBack(event) || IsForward(event)) {
    screen_ = kMapSelect;
    return true;
  }
  // Swallow everything else: this is a modal screen.
  return true;
}

namespace {

// Whether `account_id` is still on the roster. A player who has gone leaves
// nothing to read.
bool IsOnline(const MultiplayerSnapshot& lobby, const std::string& account_id) {
  for (const PlayerInfo& player : lobby.online.players()) {
    if (player.account_id() == account_id) {
      return true;
    }
  }
  return false;
}

}  // namespace

MultiplayerSnapshot TuiController::Lobby() const {
  return multiplayer_ == nullptr ? MultiplayerSnapshot()
                                 : multiplayer_->Snapshot();
}

void TuiController::RaisePartyNotice(const std::string& message, bool refusal) {
  // Wrapped here rather than by the server: how wide a dialog is is the
  // client's business, and a sentence written plainly used to stretch one as
  // far as it ran. A break the message made itself is kept, each side of it
  // wrapped on its own.
  party_notice_.clear();
  for (std::size_t at = 0; at <= message.size();) {
    std::size_t end = message.find('\n', at);
    std::size_t stop = end == std::string::npos ? message.size() : end;
    for (const std::string& line :
         WrapBalanced(message.substr(at, stop - at), kNoticeWidth)) {
      if (!party_notice_.empty()) {
        party_notice_ += "\n";
      }
      party_notice_ += line;
    }
    at = stop + 1;
  }
  party_notice_is_refusal_ = refusal;
  party_notice_prompt_.Open();
}

void TuiController::AdvanceParty() {
  MultiplayerSnapshot lobby = Lobby();
  party_select_panel_.SetSnapshot(lobby);
  player_list_panel_.SetSnapshot(lobby);
  // The connection going away turns the player out of the multiplayer
  // screens: there is no lobby left to show them, and Close should land them
  // somewhere real.
  bool on_lobby_screen =
      screen_ == kPartySelect || screen_ == kPartyMenu ||
      screen_ == kPartyConfirm || screen_ == kPlayerInspect ||
      screen_ == kPlayerItemInspect || screen_ == kPlayerList ||
      screen_ == kPlayerMenu || OnTradeScreen();
  if (on_lobby_screen && lobby.state != ConnectionState::kConnected) {
    screen_ = kMain;
    menu_panel_.CloseBox();
    party_select_panel_.CloseMenu();
    player_list_panel_.CloseMenu();
    party_prompt_.Close();
    inspect_pending_.clear();
    RaisePartyNotice(
        lobby.message.empty() ? "Lost the connection." : lobby.message,
        /*refusal=*/true);
    return;
  }
  AdvanceWatch(lobby);
  AdvanceTrade(lobby);
  RefreshPlayerInspect(lobby);
  AdvancePartyFight(lobby);
  if (lobby.notification_serial != notification_seen_) {
    notification_seen_ = lobby.notification_serial;
    notification_.Raise(lobby.notification);
  }
  if (lobby.notice_serial == party_notice_seen_) {
    return;
  }
  party_notice_seen_ = lobby.notice_serial;
  RaisePartyNotice(lobby.notice, lobby.notice_is_refusal);
}

void TuiController::AdvanceNotification(double elapsed_seconds) {
  notification_.Advance(elapsed_seconds);
}

void TuiController::TouchNotification() {
  notification_.Touch();
}

void TuiController::AdvancePartyFight(const MultiplayerSnapshot& lobby) {
  if (party_fight_ == nullptr) {
    return;
  }
  party_fight_->Advance(lobby.account_id);
  if (!party_fight_->fighting()) {
    return;
  }
  if (lobby.state != ConnectionState::kConnected) {
    // Nothing more is coming, and a fight nobody can hear the end of is not
    // one to keep watching. Whoever is left fights on without them.
    if (boss_run_ != nullptr && in_party_fight()) {
      boss_run_->Abort();
    }
    party_fight_->Forget();
    return;
  }
  if (boss_run_ == nullptr) {
    OpenPartyFight(lobby);
  }
}

void TuiController::SeatParty(const MultiplayerSnapshot& lobby) {
  state_.party.clear();
  for (const PartyMember& member : lobby.party.members()) {
    if (member.player().account_id() == lobby.account_id) {
      continue;
    }
    // A sheet names its items rather than describing them, so it is rebuilt
    // against this build's catalogs -- the same way the inspect screen reads
    // one, and the same way a save is loaded.
    CharacterInstance ally(state_.rng, Character());
    ally.RestoreFrom(member.player().sheet(), state_.equips, state_.items);
    ally.set_autoswap_presets(member.player().autoswap_presets());
    ally.UseEquipSets(state_.equip_sets);
    state_.party.push_back(std::move(ally));
  }
}

void TuiController::OpenPartyFight(const MultiplayerSnapshot& lobby) {
  std::map<std::string, Boss>::const_iterator it =
      state_.bosses.find(party_fight_->boss_key());
  int index = party_fight_->difficulty_index();
  if (it == state_.bosses.end() || index < 0 ||
      index >= it->second.difficulties_size()) {
    // A fight this build does not hold. Nothing can be drawn for it.
    party_fight_->Forget();
    return;
  }
  boss_run_key_ = party_fight_->boss_key();
  boss_run_difficulty_ = it->second.difficulties(index).name();
  // Before the run: it works the character's damage out once, and the party
  // is part of what the character is worth for as long as the fight lasts.
  SeatParty(lobby);
  ChargeBossEntry();
  boss_run_ =
      std::make_unique<BossRun>(boss_run_key_, it->second, index,
                                party_fight_.get(), party_fight_->practice());
  // Whatever they were doing, they are in a fight now.
  party_select_panel_.CloseMenu();
  party_prompt_.Close();
  screen_ = kBossFight;
}

bool TuiController::in_party_fight() const {
  return party_fight_ != nullptr && party_fight_->fighting();
}

void TuiController::DropBossRun() {
  boss_run_.reset();
  // The party's skills reach this character for the fight and no longer: what
  // they farm afterwards, they farm alone.
  state_.party.clear();
  if (party_fight_ != nullptr) {
    party_fight_->Forget();
  }
}

void TuiController::RefreshPlayerInspect(const MultiplayerSnapshot& lobby) {
  if (screen_ != kPlayerInspect && screen_ != kPlayerItemInspect) {
    return;
  }
  if (inspect_from_players_) {
    // The roster first: the sheet they last sent is still on hand, so a
    // screen that read it would go on drawing somebody who has gone.
    if (!IsOnline(lobby, inspect_account_)) {
      StopWatching();
      screen_ = kPlayerList;
      return;
    }
    if (lobby.watched.account_id() == inspect_account_) {
      player_inspect_panel_.SetPlayer(lobby.watched);
    }
    return;
  }
  for (const PartyMember& member : lobby.party.members()) {
    if (member.player().account_id() == inspect_account_) {
      // Redrawn from what has just arrived, so a member levelling or
      // re-gearing while they are being read shows it.
      player_inspect_panel_.SetPlayer(member.player());
      return;
    }
  }
  // They left, or were turned out. There is nothing left to read.
  screen_ = kPartySelect;
}

void TuiController::AskAboutParty(PartyAsk ask, const std::string& question) {
  party_ask_ = ask;
  party_target_ = party_select_panel_.selected_member();
  party_prompt_question_ = question;
  party_prompt_.Open(/*cancel_selected=*/true);
  screen_ = kPartyConfirm;
}

void TuiController::TakePartyAction(PartyAction action) {
  switch (action) {
    case PartyAction::kClose:
      LeaveMultiplayerScreen();
      return;
    case PartyAction::kMemberMenu:
      party_select_panel_.OpenMenu();
      screen_ = kPartyMenu;
      return;
    case PartyAction::kLeave:
      AskAboutParty(PartyAsk::kLeave, "Leave the party?");
      return;
    case PartyAction::kCreate:
      multiplayer_->client().CreateParty();
      return;
    case PartyAction::kJoin:
      multiplayer_->client().JoinParty(party_select_panel_.selected_party_id());
      return;
    case PartyAction::kReady:
      multiplayer_->client().SetReady(true);
      return;
    case PartyAction::kUnready:
      multiplayer_->client().SetReady(false);
      return;
  }
}

void TuiController::PartyConfirmed() {
  switch (party_ask_) {
    case PartyAsk::kNone:
      break;
    case PartyAsk::kKick:
      multiplayer_->client().Kick(party_target_);
      break;
    case PartyAsk::kPromote:
      multiplayer_->client().Promote(party_target_);
      break;
    case PartyAsk::kLeave:
      multiplayer_->client().LeaveParty();
      break;
  }
  party_ask_ = PartyAsk::kNone;
}

bool TuiController::OnPlayerListEvent(ftxui::Event event) {
  if (event == ftxui::Event::ArrowUp) {
    player_list_panel_.MoveCursor(-1);
    return true;
  }
  if (event == ftxui::Event::ArrowDown) {
    player_list_panel_.MoveCursor(1);
    return true;
  }
  if (IsBack(event)) {
    LeaveMultiplayerScreen();
    return true;
  }
  if (IsForward(event)) {
    if (player_list_panel_.on_close()) {
      LeaveMultiplayerScreen();
      return true;
    }
    player_list_panel_.OpenMenu();
    screen_ = kPlayerMenu;
  }
  return true;
}

bool TuiController::OnPlayerMenuEvent(ftxui::Event event) {
  if (event == ftxui::Event::ArrowUp) {
    player_list_panel_.MoveMenuCursor(-1);
    return true;
  }
  if (event == ftxui::Event::ArrowDown) {
    player_list_panel_.MoveMenuCursor(1);
    return true;
  }
  if (IsBack(event)) {
    player_list_panel_.CloseMenu();
    screen_ = kPlayerList;
    return true;
  }
  if (!IsForward(event)) {
    return true;
  }
  int chosen = player_list_panel_.menu_selected();
  std::string account = player_list_panel_.selected_account();
  player_list_panel_.CloseMenu();
  screen_ = kPlayerList;
  if (chosen == kPlayerMenuInspect) {
    WatchForInspect(account);
  } else if (chosen == kPlayerMenuTrade) {
    AskToTrade(account);
  }
  return true;
}

bool TuiController::OnPartySelectEvent(ftxui::Event event) {
  if (event == ftxui::Event::ArrowUp) {
    party_select_panel_.MoveCursor(-1);
    return true;
  }
  if (event == ftxui::Event::ArrowDown) {
    party_select_panel_.MoveCursor(1);
    return true;
  }
  if (event == ftxui::Event::ArrowLeft) {
    party_select_panel_.MoveButton(-1);
    return true;
  }
  if (event == ftxui::Event::ArrowRight) {
    party_select_panel_.MoveButton(1);
    return true;
  }
  if (IsForward(event)) {
    TakePartyAction(party_select_panel_.Chosen());
    return true;
  }
  if (IsBack(event)) {
    LeaveMultiplayerScreen();
    return true;
  }
  return true;
}

bool TuiController::OnPartyMenuEvent(ftxui::Event event) {
  if (event == ftxui::Event::ArrowUp) {
    party_select_panel_.MoveMenuCursor(-1);
    return true;
  }
  if (event == ftxui::Event::ArrowDown) {
    party_select_panel_.MoveMenuCursor(1);
    return true;
  }
  if (IsBack(event)) {
    party_select_panel_.CloseMenu();
    screen_ = kPartySelect;
    return true;
  }
  if (!IsForward(event)) {
    return true;
  }
  int chosen = party_select_panel_.menu_selected();
  std::string name = party_select_panel_.selected_member_name();
  std::string account = party_select_panel_.selected_member();
  party_select_panel_.CloseMenu();
  screen_ = kPartySelect;
  if (chosen == kPartyMenuInspect) {
    OpenPlayerInspect(account);
  } else if (chosen == kPartyMenuTrade) {
    AskToTrade(account);
  } else if (chosen == kPartyMenuKick) {
    AskAboutParty(PartyAsk::kKick, "Kick " + name + " from the party?");
  } else if (chosen == kPartyMenuPromote) {
    AskAboutParty(PartyAsk::kPromote, "Promote " + name + " to party leader?");
  }
  return true;
}

void TuiController::WatchForInspect(const std::string& account_id) {
  if (multiplayer_ == nullptr) {
    return;
  }
  inspect_pending_ = account_id;
  multiplayer_->client().WatchPlayer(account_id);
}

void TuiController::AdvanceWatch(const MultiplayerSnapshot& lobby) {
  if (inspect_pending_.empty()) {
    return;
  }
  if (lobby.watched.account_id() == inspect_pending_) {
    inspect_account_ = inspect_pending_;
    inspect_pending_.clear();
    inspect_from_players_ = true;
    player_inspect_panel_.SetPlayer(lobby.watched);
    player_inspect_panel_.Reset();
    screen_ = kPlayerInspect;
    return;
  }
  // Gone before their sheet arrived. The player is left on the list rather
  // than shown an empty sheet.
  if (!IsOnline(lobby, inspect_pending_)) {
    StopWatching();
  }
}

void TuiController::StopWatching() {
  inspect_pending_.clear();
  if (multiplayer_ != nullptr) {
    multiplayer_->client().WatchPlayer("");
  }
}

void TuiController::OpenPlayerInspect(const std::string& account_id) {
  const PartyMember* member = nullptr;
  MultiplayerSnapshot lobby = Lobby();
  for (const PartyMember& in_party : lobby.party.members()) {
    if (in_party.player().account_id() == account_id) {
      member = &in_party;
    }
  }
  // Gone between the menu opening and Enter. Nothing to read, so the player
  // is left where they were rather than shown an empty sheet.
  if (member == nullptr) {
    return;
  }
  inspect_account_ = account_id;
  inspect_from_players_ = false;
  player_inspect_panel_.SetPlayer(member->player());
  player_inspect_panel_.Reset();
  screen_ = kPlayerInspect;
}

void TuiController::AskToTrade(const std::string& account_id) {
  if (multiplayer_ == nullptr || account_id.empty()) {
    return;
  }
  // Where the screen closes back to, taken now: it is the list the player
  // pressed Trade on, and by the time the server answers they are on it.
  trade_return_ = screen_ == kPartySelect ? kPartySelect : kPlayerList;
  multiplayer_->client().RequestTrade(account_id);
}

void TuiController::AdvanceTrade(const MultiplayerSnapshot& lobby) {
  trade_panel_.SetTrade(lobby.trade);
  // Before anything else about the trade: the payment is what says it ended,
  // and the empty state under it would otherwise read as a walk-out.
  if (lobby.trade_serial != trade_paid_seen_) {
    trade_paid_seen_ = lobby.trade_serial;
    ApplyCompletedTrade(lobby.trade_received);
    return;
  }
  const bool open = OnTradeScreen();
  if (lobby.trade.id().empty()) {
    left_trade_id_.clear();
    if (open) {
      trade_panel_.CloseMenu();
      screen_ = trade_return_;
      RaisePartyNotice("They left the trade.", /*refusal=*/false);
    }
    return;
  }
  // A trade this player has already walked out of: the server has not caught
  // up, and standing the screen back up would trap them on it.
  if (open) {
    AdvanceTradeConfirm(lobby.trade);
    return;
  }
  if (lobby.trade.id() == left_trade_id_) {
    return;
  }
  trade_panel_.Reset();
  screen_ = kTrade;
}

void TuiController::ApplyCompletedTrade(const TradeOffer& received) {
  // Taken before a thing moves: what was put up is named by where it sits in
  // the bag, and the first removal makes that untrue.
  const OwnTradeOffer& mine = trade_panel_.own();
  ApplyTrade(state_.character, state_.equips, state_.items, mine.equips,
             mine.ToWire(state_.character), received);
  trade_panel_.Reset();
  trade_prompt_.Close();
  left_trade_id_.clear();
  if (OnTradeScreen()) {
    screen_ = trade_return_;
  }
  notification_.Raise({"Trade complete."});
  save_wanted_ = true;
}

bool TuiController::TakeSaveRequest() {
  bool wanted = save_wanted_;
  save_wanted_ = false;
  return wanted;
}

bool TuiController::trade_waiting() const {
  return Lobby().trade.mine_confirmed();
}

void TuiController::AdvanceTradeConfirm(const TradeState& trade) {
  // The dialog is the STATE's, not a keypress's: it opens on the second
  // acceptance, whichever side gives it, and goes the moment either is taken
  // back.
  const bool both = trade.mine_accepted() && trade.theirs_accepted();
  // Not over the question about walking out: that one is the player's to
  // answer, and its answer may be what ends the trade anyway.
  if (screen_ == kTradeLeave) {
    return;
  }
  if (both && screen_ != kTradeConfirm) {
    trade_panel_.CloseMenu();
    trade_prompt_.Open();
    screen_ = kTradeConfirm;
    return;
  }
  if (!both && screen_ == kTradeConfirm) {
    trade_prompt_.Close();
    screen_ = kTrade;
  }
}

void TuiController::OpenTradeAmount() {
  // Opens on what is already on the table, with a button for none of it: a
  // player changing their mind is taking something back as often as adding.
  trade_currency_ = trade_panel_.cursor().currency;
  trade_selector_.Reset(trade_panel_.held(trade_currency_),
                        trade_panel_.offered(trade_currency_));
  trade_selector_.set_low(0);
  screen_ = kTradeAmount;
}

void TuiController::PutUpTradeAmount() {
  trade_panel_.PutUpCurrency(trade_currency_, trade_selector_.value());
  SendTradeOffer();
}

bool TuiController::OnTradeScreen() const {
  return screen_ == kTrade || screen_ == kTradeAmount ||
         screen_ == kTradeMenu || screen_ == kTradeItemAmount ||
         screen_ == kTradeInspect || screen_ == kTradeConfirm ||
         screen_ == kTradeLeave;
}

void TuiController::SendTradeOffer() {
  if (multiplayer_ == nullptr) {
    return;
  }
  // The whole offer every time: two changes in flight at once cannot then add
  // up to a table nobody laid.
  multiplayer_->client().SetTradeOffer(
      trade_panel_.own().ToWire(state_.character));
}

void TuiController::LeaveTrade() {
  trade_leave_prompt_.Close();
  trade_prompt_.Close();
  trade_panel_.CloseMenu();
  left_trade_id_ = Lobby().trade.id();
  if (multiplayer_ != nullptr) {
    multiplayer_->client().LeaveTrade();
  }
  screen_ = trade_return_;
}

bool TuiController::OnTradeEvent(ftxui::Event event) {
  if (IsSwitchPanel(event)) {
    trade_panel_.NextZone(event == ftxui::Event::Tab ? 1 : -1);
    return true;
  }
  if (event == ftxui::Event::ArrowLeft) {
    trade_panel_.MoveCursor(-1);
    return true;
  }
  if (event == ftxui::Event::ArrowRight) {
    trade_panel_.MoveCursor(1);
    return true;
  }
  if (event == ftxui::Event::ArrowUp) {
    trade_panel_.MoveRow(-1);
    return true;
  }
  if (event == ftxui::Event::ArrowDown) {
    trade_panel_.MoveRow(1);
    return true;
  }
  if (IsForward(event)) {
    TradeCursor::Kind kind = trade_panel_.cursor().kind;
    if (kind == TradeCursor::Kind::kCurrency) {
      OpenTradeAmount();
    } else if (kind == TradeCursor::Kind::kAccept) {
      ToggleTradeAccept();
    } else {
      OpenTradeMenu();
    }
    return true;
  }
  if (IsBack(event)) {
    // Asked rather than done: Escape is one key away from everywhere, and
    // walking out ends the trade for both of them.
    trade_leave_prompt_.Open();
    screen_ = kTradeLeave;
  }
  // Everything else is swallowed: this is a modal screen, and the ticker's
  // redraw arrives as an event too.
  return true;
}

void TuiController::ToggleTradeAccept() {
  if (multiplayer_ == nullptr) {
    return;
  }
  MultiplayerSnapshot lobby = Lobby();
  if (!lobby.trade.mine_accepted() &&
      !HasRoomForTrade(state_.character, state_.items,
                       trade_panel_.own().ToWire(state_.character),
                       lobby.trade.theirs())) {
    RaisePartyNotice(kBagTooFullMessage, /*refusal=*/true);
    return;
  }
  multiplayer_->client().AcceptTrade(!lobby.trade.mine_accepted());
}

void TuiController::OpenTradeMenu() {
  trade_panel_.OpenMenu();
  if (!trade_panel_.menu_open()) {
    return;
  }
  screen_ = kTradeMenu;
}

void TuiController::OfferFromBag() {
  TradeCursor cursor = trade_panel_.cursor();
  if (trade_panel_.on_etc_tab()) {
    OpenTradeItemAmount(cursor.index);
    return;
  }
  trade_panel_.PutUpEquip(cursor.index);
  SendTradeOffer();
}

void TuiController::OpenTradeItemAmount(int stack) {
  const std::vector<StackableItem>& stacks = state_.character.stackables();
  if (stack < 0 || stack >= static_cast<int>(stacks.size())) {
    return;
  }
  trade_stack_ = stack;
  // The whole stack is what [MAX] reaches, however much of it is already on
  // the table: what a player owns is not changed by having offered it.
  trade_selector_.Reset(stacks[stack].count(),
                        trade_panel_.stack_offered(stack));
  screen_ = kTradeItemAmount;
}

void TuiController::PutUpTradeItemAmount() {
  trade_panel_.PutUpStack(trade_stack_, trade_selector_.value());
  SendTradeOffer();
}

void TuiController::OpenTradeInspect() {
  TradeCursor cursor = trade_panel_.cursor();
  trade_inspect_equip_.reset();
  trade_inspect_stack_ = nullptr;
  // The item is COPIED out rather than pointed at: one of the three places it
  // can come from is a snapshot taken by value.
  Equip equip;
  bool is_equip = false;
  std::string stack_name;
  switch (cursor.kind) {
    case TradeCursor::Kind::kBag:
      if (trade_panel_.on_etc_tab()) {
        stack_name = state_.character.stackables()[cursor.index].name();
        break;
      }
      equip = state_.character.inventory()[cursor.index].SavedState();
      is_equip = true;
      break;
    case TradeCursor::Kind::kOffered: {
      const OwnTradeOffer& mine = trade_panel_.own();
      int equips = static_cast<int>(mine.equips.size());
      if (cursor.index >= equips) {
        stack_name = mine.stacks[cursor.index - equips].name();
        break;
      }
      equip =
          state_.character.inventory()[mine.equips[cursor.index]].SavedState();
      is_equip = true;
      break;
    }
    case TradeCursor::Kind::kTheirs: {
      MultiplayerSnapshot lobby = Lobby();
      const TradeOffer& theirs = lobby.trade.theirs();
      if (cursor.index >= theirs.equips_size()) {
        stack_name = theirs.stacks(cursor.index - theirs.equips_size()).name();
        break;
      }
      equip = theirs.equips(cursor.index);
      is_equip = true;
      break;
    }
    default:
      return;
  }
  if (is_equip) {
    const EquipPrototype* proto =
        FindEquipByName(state_.equips, equip.equip_name());
    if (proto == nullptr) {
      return;
    }
    trade_inspect_equip_ = EquipItemFromState(*proto, equip);
  } else {
    trade_inspect_stack_ = FindItemByName(state_.items, stack_name);
    if (trade_inspect_stack_ == nullptr) {
      return;
    }
  }
  inspect_panel_.Reset();
  screen_ = kTradeInspect;
}

bool TuiController::OnTradeMenuEvent(ftxui::Event event) {
  if (event == ftxui::Event::ArrowUp) {
    trade_panel_.MoveMenuCursor(-1);
    return true;
  }
  if (event == ftxui::Event::ArrowDown) {
    trade_panel_.MoveMenuCursor(1);
    return true;
  }
  if (IsBack(event)) {
    trade_panel_.CloseMenu();
    screen_ = kTrade;
    return true;
  }
  if (!IsForward(event)) {
    return true;
  }
  int chosen = trade_panel_.menu_selected();
  TradeCursor cursor = trade_panel_.cursor();
  trade_panel_.CloseMenu();
  screen_ = kTrade;
  if (chosen == kTradeMenuInspect) {
    OpenTradeInspect();
  } else if (chosen == kTradeMenuOffer) {
    OfferFromBag();
  } else if (chosen == kTradeMenuRemove) {
    trade_panel_.TakeBack(cursor.index);
    SendTradeOffer();
  }
  return true;
}

bool TuiController::OnTradeConfirmEvent(ftxui::Event event) {
  const bool waiting = Lobby().trade.mine_confirmed();
  ConfirmChoice choice = trade_prompt_.OnEvent(event, !waiting);
  if (choice == ConfirmChoice::kPending) {
    return true;
  }
  if (multiplayer_ != nullptr) {
    multiplayer_->client().ConfirmTrade(choice == ConfirmChoice::kConfirmed);
  }
  // Up either way: what takes it down is the answer coming back -- the trade
  // going through, or the acceptance this cancel just cleared.
  trade_prompt_.Open(/*cancel_selected=*/choice == ConfirmChoice::kConfirmed);
  return true;
}

bool TuiController::OnTradeLeaveEvent(ftxui::Event event) {
  ConfirmChoice choice = trade_leave_prompt_.OnEvent(event);
  if (choice == ConfirmChoice::kPending) {
    return true;
  }
  if (choice == ConfirmChoice::kConfirmed) {
    LeaveTrade();
    return true;
  }
  screen_ = kTrade;
  return true;
}

bool TuiController::OnTradeItemAmountEvent(ftxui::Event event) {
  ConfirmChoice choice = trade_selector_.OnEvent(event);
  if (choice == ConfirmChoice::kPending) {
    return true;
  }
  screen_ = kTrade;
  if (choice == ConfirmChoice::kConfirmed) {
    PutUpTradeItemAmount();
  }
  return true;
}

bool TuiController::OnTradeAmountEvent(ftxui::Event event) {
  ConfirmChoice choice = trade_selector_.OnEvent(event);
  if (choice == ConfirmChoice::kPending) {
    return true;
  }
  if (choice == ConfirmChoice::kConfirmed) {
    PutUpTradeAmount();
  }
  screen_ = kTrade;
  return true;
}

void TuiController::OpenPlayerSkillInspect(const Skill& skill) {
  skill_inspect_ = skill;
  card_from_inspect_ = true;
  skill_inspect_panel_.ResetScroll();
  screen_ = kSkillInspect;
}

void TuiController::OpenPlayerHyperStatInspect(HyperStatField field) {
  hyper_field_ = field;
  // The allocation their Character panel is reading, so the card and the row
  // behind it never state different levels.
  hyper_preset_ = player_inspect_panel_.preset() == Activity::kBossing
                      ? StatPreset::kSecond
                      : StatPreset::kFirst;
  card_from_inspect_ = true;
  screen_ = kHyperStatInspect;
}

void TuiController::OpenPlayerAllStats() {
  player_inspect_panel_.SyncAllStats();
  screen_ = kPlayerAllStats;
}

void TuiController::OpenPlayerItemInspect() {
  // The card reads the item off the panel's cursor, so there is no pointer
  // held across a tick that may rebuild the member.
  if (player_inspect_panel_.selected_item() != nullptr) {
    screen_ = kPlayerItemInspect;
  }
}

bool TuiController::OnPlayerInspectEvent(ftxui::Event event) {
  if (IsBack(event)) {
    // An expanded panel is the whole screen, so Escape closes that first --
    // the same key [Close] is, one view at a time.
    if (player_inspect_panel_.expanded()) {
      player_inspect_panel_.CloseExpanded();
      return true;
    }
    if (inspect_from_players_) {
      StopWatching();
      screen_ = kPlayerList;
      return true;
    }
    screen_ = kPartySelect;
    return true;
  }
  player_inspect_panel_.OnEvent(event);
  return true;
}

bool TuiController::OnPlayerAllStatsEvent(ftxui::Event event) {
  // Left/Right belong to the member's Farm/Boss row, and only while they have
  // one; the panel says so.
  if (player_inspect_panel_.OnAllStatsEvent(event)) {
    return true;
  }
  // Only a key that MEANS leaving closes it. Anything else is swallowed --
  // the ticker's redraw arrives as an event too, and a screen that closed on
  // whatever it did not recognise was gone by the next frame.
  if (IsBack(event) || IsForward(event)) {
    screen_ = kPlayerInspect;
  }
  return true;
}

bool TuiController::OnPlayerItemInspectEvent(ftxui::Event event) {
  return OnCardEvent(event, player_item_panel_, kPlayerInspect);
}

bool TuiController::OnPartyConfirmEvent(ftxui::Event event) {
  ConfirmChoice choice = party_prompt_.OnEvent(event);
  if (choice == ConfirmChoice::kPending) {
    return true;
  }
  if (choice == ConfirmChoice::kConfirmed) {
    PartyConfirmed();
  }
  party_ask_ = PartyAsk::kNone;
  screen_ = kPartySelect;
  return true;
}

bool TuiController::OnBossSelectEvent(ftxui::Event event) {
  if (IsSwitchPanel(event)) {
    boss_select_panel_.SwitchPanel(event == ftxui::Event::TabReverse ? -1 : 1);
    return true;
  }
  if (event == ftxui::Event::ArrowUp) {
    boss_select_panel_.MoveCursor(-1);
    return true;
  }
  if (event == ftxui::Event::ArrowDown) {
    boss_select_panel_.MoveCursor(1);
    return true;
  }
  if (event == ftxui::Event::ArrowLeft) {
    boss_select_panel_.ChangeDifficulty(-1);
    return true;
  }
  if (event == ftxui::Event::ArrowRight) {
    boss_select_panel_.ChangeDifficulty(1);
    return true;
  }
  if (IsForward(event)) {
    if (boss_select_panel_.focus() == BossPanel::kOptions) {
      ToggleBossOption(boss_select_panel_.selected_option());
      return true;
    }
    if (boss_select_panel_.selected() == nullptr) {
      return true;
    }
    boss_prompt_title_ = boss_select_panel_.selected_title();
    boss_prompt_practice_ = boss_select_panel_.practice();
    MultiplayerSnapshot lobby = Lobby();
    bool led_by_somebody_else =
        !lobby.party.id().empty() &&
        lobby.party.leader_account_id() != lobby.account_id;
    // Five ways a fight is not offered, each saying WHY rather than doing
    // nothing. Whose party it is leads, being about the player rather than the
    // fight they picked.
    if (led_by_somebody_else) {
      OpenNotice(kBossNotice, {"You are not the leader."}, /*refusal=*/true,
                 "Close");
    } else if (boss_select_panel_.selected_coming_soon()) {
      OpenSentenceNotice(kBossNotice, boss_prompt_title_ + " is coming soon!",
                         /*refusal=*/true, "Close");
    } else if (EquippedWeapon(state_) == nullptr) {
      OpenNotice(kBossNotice, {"You have no weapon equipped!"},
                 /*refusal=*/true, "Close");
    } else if (!boss_select_panel_.selected_unlocked()) {
      OpenSentenceNotice(
          kBossNotice,
          boss_prompt_title_ + " unlocks at level " +
              std::to_string(boss_select_panel_.selected_unlock_level()) + ".",
          /*refusal=*/true, "Close");
    } else if (!boss_select_panel_.selected_available() &&
               !boss_select_panel_.practice()) {
      std::string when =
          boss_select_panel_.selected_reset() == RESET_PERIOD_WEEKLY
              ? "this week"
              : "today";
      // Named without the difficulty: a clear of any rung closes them all, so
      // the rung on the cursor may not be the one that was taken.
      OpenSentenceNotice(
          kBossNotice,
          state_.bosses.at(boss_select_panel_.selected_boss()).name() +
              " has already been killed " + when + ".",
          /*refusal=*/false, "Close");
    } else {
      boss_prompt_.Open();
      screen_ = kBossConfirm;
    }
    return true;
  }
  if (IsBack(event)) {
    screen_ = kMain;
    return true;
  }
  return true;
}

bool TuiController::OnBossConfirmEvent(ftxui::Event event) {
  ConfirmChoice choice = boss_prompt_.OnEvent(event);
  if (choice == ConfirmChoice::kCancelled) {
    screen_ = kBossSelect;
    return true;
  }
  if (choice != ConfirmChoice::kConfirmed) {
    return true;
  }
  boss_run_key_ = boss_select_panel_.selected_boss();
  const BossDifficulty* difficulty = boss_select_panel_.selected();
  std::map<std::string, Boss>::const_iterator it =
      state_.bosses.find(boss_run_key_);
  if (difficulty == nullptr || it == state_.bosses.end()) {
    screen_ = kBossSelect;
    return true;
  }
  boss_run_difficulty_ = difficulty->name();
  if (party_fight_ != nullptr && Lobby().party.members_size() >= 2) {
    // A party fights it together: the server checks every member, keeps the
    // one roster they all hit, and stands them all in the arena. Alone, or in
    // a party of one, it is the run below and no network at all.
    multiplayer_->client().StartFight(boss_run_key_,
                                      boss_select_panel_.selected_difficulty(),
                                      PARTY_MODE_SHARED, state_.boss_options);
    screen_ = kBossSelect;
    return true;
  }
  ChargeBossEntry();
  boss_run_ = std::make_unique<BossRun>(
      boss_run_key_, it->second, boss_select_panel_.selected_difficulty(),
      /*authority=*/nullptr, state_.boss_options.practice());
  screen_ = kBossFight;
  return true;
}

void TuiController::ToggleBossOption(int option) {
  // One switch on the row today. The index is taken rather than assumed, so a
  // second lands here and nowhere else.
  if (option == 0) {
    state_.boss_options.set_practice(!state_.boss_options.practice());
  }
}

void TuiController::ChargeBossEntry() {
  // Charged on the way in, whether or not the fight is won: the potion is
  // drunk before the doors open. A party charges every member who has it on,
  // each on their own client and out of their own purse.
  state_.character.ChargeConsumable(CONSUMABLE_TYPE_EXTREME_GREEN_POTION, 1);
}

bool TuiController::OnBossNoticeEvent(ftxui::Event event) {
  if (notice_prompt_.OnEvent(event)) {
    // The run is null for a notice raised instead of a fight -- no weapon, or
    // a daily already taken -- and holds a finished one for a fight that ran
    // out of clock.
    LeaveBossRun();
  }
  return true;
}

void TuiController::OpenNotice(Screen screen) {
  // The word a one-button result is dismissed by. A notice that is the end of
  // it says so instead -- see the overload below.
  notice_button_ = "Continue";
  notice_prompt_.Open();
  screen_ = screen;
}

void TuiController::OpenNotice(Screen screen, std::vector<std::string> lines,
                               bool refusal, const std::string& button) {
  OpenNotice(screen);
  notice_lines_ = std::move(lines);
  notice_is_refusal_ = refusal;
  notice_button_ = button;
}

void TuiController::OpenSentenceNotice(Screen screen,
                                       const std::string& sentence,
                                       bool refusal,
                                       const std::string& button) {
  OpenNotice(screen, WrapBalanced(sentence, kNoticeWidth), refusal, button);
}

void TuiController::OpenDailies() {
  int64_t now = static_cast<int64_t>(std::time(nullptr));
  if (!DailiesAvailable(state_.character.DailiesClaimedAt(), now)) {
    OpenNotice(kDailiesNotice, {"Already claimed today."}, /*refusal=*/false,
               "Close");
    return;
  }
  std::vector<const EquipPrototype*> claimable =
      ClaimableSymbols(state_.character, state_.equips);
  if (claimable.empty()) {
    // Nothing to offer: a character who has never held a symbol. Said here
    // rather than at the confirm, where the claim's own refusal is a full bag.
    OpenNotice(kDailiesNotice, {"You have no dailies to claim."},
               /*refusal=*/false, "Close");
    return;
  }
  std::vector<DailiesPanel::Reward> rewards;
  for (const EquipPrototype* proto : claimable) {
    rewards.push_back({proto->name(), kSymbolsPerDay});
  }
  dailies_panel_.Reset(std::move(rewards));
  screen_ = kDailies;
}

bool TuiController::OnDailiesEvent(ftxui::Event event) {
  ConfirmChoice choice = dailies_panel_.OnEvent(event);
  if (choice == ConfirmChoice::kPending) {
    return true;
  }
  if (choice == ConfirmChoice::kConfirmed &&
      !ClaimDailies(state_.character, state_.equips,
                    static_cast<int64_t>(std::time(nullptr)))) {
    // The one way a claim the player was offered does not happen: the bag
    // filled up. Taking half of it would cost them the rest until tomorrow.
    OpenNotice(kDailiesNotice, {"Not enough room in your bag."},
               /*refusal=*/true, "Close");
    return true;
  }
  screen_ = kMain;
  return true;
}

bool TuiController::OnDailiesNoticeEvent(ftxui::Event event) {
  if (notice_prompt_.OnEvent(event)) {
    screen_ = kMain;
  }
  return true;
}

bool TuiController::OnBossFightEvent(ftxui::Event event) {
  if (IsBack(event)) {
    boss_abort_prompt_.Open(/*cancel_selected=*/true);
    screen_ = kBossAbort;
    return true;
  }
  // The arrows walk the player between the phase's spots. The swing is not
  // theirs to aim: what they choose is where to stand.
  if (boss_run_ != nullptr) {
    if (event == ftxui::Event::ArrowLeft) {
      boss_run_->MovePlayer(-1, 0);
    } else if (event == ftxui::Event::ArrowRight) {
      boss_run_->MovePlayer(1, 0);
    } else if (event == ftxui::Event::ArrowUp) {
      boss_run_->MovePlayer(0, -1);
    } else if (event == ftxui::Event::ArrowDown) {
      boss_run_->MovePlayer(0, 1);
    }
  }
  // Everything else is swallowed: the fight plays itself out.
  return true;
}

bool TuiController::OnBossAbortEvent(ftxui::Event event) {
  ConfirmChoice choice = boss_abort_prompt_.OnEvent(event);
  if (choice == ConfirmChoice::kConfirmed && boss_run_ != nullptr) {
    if (in_party_fight()) {
      // Walked out of the party's fight, which goes on without them: they
      // deal no more damage and are paid nothing.
      party_fight_->Leave();
    }
    boss_run_->Abort();
  }
  if (choice != ConfirmChoice::kPending) {
    screen_ = kBossFight;
  }
  return true;
}

void TuiController::AdvanceBossRun(double elapsed_seconds) {
  // Only the fight screen runs the clock. The leave prompt stops it while the
  // player decides, and so does whatever the fight ended on -- the run is kept
  // until they press the button, so the arena stays behind the panel.
  if (boss_run_ == nullptr) {
    return;
  }
  // Only the fight screen runs the clock, and the leave prompt stops it -- but
  // not in a party's fight, where the others are still swinging and a question
  // this player is answering must not cost them.
  if (screen_ != kBossFight && !(screen_ == kBossAbort && in_party_fight())) {
    return;
  }
  boss_run_->Advance(state_, elapsed_seconds);
  if (!boss_run_->done()) {
    return;
  }
  if (boss_run_->won()) {
    // A practice clear spends nothing: the fight is not written down, so the
    // reset clock never hears about it.
    if (!boss_run_->practice()) {
      state_.character.RecordBossClear(
          boss_run_key_, boss_run_difficulty_,
          static_cast<int64_t>(std::time(nullptr)));
    }
    // Copied off the run rather than read back through it: the card is still
    // up when the run goes.
    boss_clear_title_ = boss_run_->title();
    boss_clear_reward_ = boss_run_->reward();
    boss_clear_seconds_ = boss_run_->clear_seconds();
    boss_clear_prompt_.Open();
    screen_ = kBossClear;
    return;
  }
  if (boss_run_->state() == BossRunState::kTimedOut) {
    OpenNotice(kBossNotice, {"Out of time!"}, /*refusal=*/false, "Continue");
    return;
  }
  // Nothing to dismiss on the way out of an abort: the player asked to leave,
  // and telling them they left is not news.
  DropBossRun();
  screen_ = kBossSelect;
}

void TuiController::OpenOfflineReport(OfflineReport report) {
  if (!report.farmed || report.absence < kOfflineNoticeSeconds) {
    return;
  }
  offline_report_ = std::move(report);
  offline_prompt_.Open();
  screen_ = kOffline;
}

bool TuiController::OnOfflineEvent(ftxui::Event event) {
  if (offline_prompt_.OnEvent(event)) {
    screen_ = kMain;
  }
  return true;
}

bool TuiController::OnBossClearEvent(ftxui::Event event) {
  if (boss_clear_prompt_.OnEvent(event)) {
    LeaveBossRun();
  }
  return true;
}

void TuiController::LeaveBossRun() {
  DropBossRun();
  screen_ = kBossSelect;
}

// The box a menu entry raised. Modal, like every other menu that stands over
// the main view: nothing behind it hears a key while it is up.
bool TuiController::OnMenuBoxEvent(ftxui::Event event) {
  if (event == ftxui::Event::ArrowUp) {
    menu_panel_.MoveBoxCursor(1);
    return true;
  }
  if (event == ftxui::Event::ArrowDown) {
    menu_panel_.MoveBoxCursor(-1);
    return true;
  }
  if (IsBack(event)) {
    menu_panel_.CloseBox();
    screen_ = kMain;
    return true;
  }
  // Left and Right still belong to the menu row underneath. Walking off the
  // entry the box hangs from puts the box away with it.
  bool sideways =
      event == ftxui::Event::ArrowLeft || event == ftxui::Event::ArrowRight;
  if (sideways && menu_panel_.box_cursor() < 0) {
    menu_panel_.CloseBox();
    screen_ = kMain;
    int step = -1;
    if (event == ftxui::Event::ArrowRight) {
      step = 1;
    }
    menu_panel_.MoveCursor(step);
    return true;
  }
  if (IsForward(event) && menu_panel_.box_cursor() >= 0) {
    OpenBoxEntry();
  }
  return true;
}

void TuiController::OpenBoxEntry() {
  switch (menu_panel_.box_entry()) {
    // It opens a screen straight from the menu, so there is nothing here to
    // open.
    case MenuEntry::kBoss:
      return;
    case MenuEntry::kMultiplayer:
      switch (menu_panel_.selected_multiplayer_entry()) {
        case MultiplayerEntry::kPlayers:
          OpenPlayerList();
          return;
        case MultiplayerEntry::kParty:
          OpenPartySelect();
          return;
      }
      return;
    case MenuEntry::kAnalysis:
      OpenAnalysisEntry(menu_panel_.selected_analysis_entry());
      return;
    case MenuEntry::kSettings:
      switch (menu_panel_.selected_settings_entry()) {
        case SettingsEntry::kKeybinds:
          keybinds_panel_.Reset();
          screen_ = kKeybinds;
          return;
        case SettingsEntry::kOptions:
          options_panel_.Reset();
          screen_ = kOptions;
          return;
      }
      return;
  }
}

void TuiController::OpenAnalysisEntry(AnalysisEntry entry) {
  if (entry == AnalysisEntry::kView) {
    screen_ = kAnalysis;
    return;
  }
  // The box stays open on Start and Stop: the entry it was pressed on has
  // just become the other one, and the player can read that where they are.
  if (analysis_.stops_on_press()) {
    analysis_.Stop();
  } else {
    analysis_.Start();
  }
}

void TuiController::LeaveKeybinds() {
  // Back to the box it was opened from, which is still standing where the
  // player left it.
  screen_ = kMenuBox;
}

void TuiController::LeaveOptions() {
  screen_ = kMenuBox;
}

// Every switch takes effect where it is thrown, so there is nothing to
// confirm and nothing to undo: Escape and Close are the same door.
bool TuiController::OnOptionsEvent(ftxui::Event event) {
  if (event == ftxui::Event::ArrowUp) {
    options_panel_.MoveRow(-1);
    return true;
  }
  if (event == ftxui::Event::ArrowDown) {
    options_panel_.MoveRow(1);
    return true;
  }
  // A volume moves under Left and Right. Holding one repeats, which is the
  // terminal's own key repeat rather than anything counted here.
  if (event == ftxui::Event::ArrowLeft) {
    options_panel_.Adjust(-1);
    return true;
  }
  if (event == ftxui::Event::ArrowRight) {
    options_panel_.Adjust(1);
    return true;
  }
  if (IsForward(event)) {
    if (options_panel_.on_close()) {
      LeaveOptions();
      return true;
    }
    options_panel_.Toggle();
    // The Autoswap switch is among them, and the character reads it.
    state_.ApplyPresetOptions();
    return true;
  }
  if (IsBack(event)) {
    LeaveOptions();
    return true;
  }
  return true;
}

// The Battle Analysis overlay reads the tool and does nothing to it, so any
// key that means "back" is all it answers to.
bool TuiController::OnAnalysisEvent(ftxui::Event event) {
  if (IsBack(event) || IsForward(event)) {
    screen_ = kMenuBox;
  }
  return true;
}

void TuiController::TakeCapturedKey(const ftxui::Event& key) {
  // The ticker posts a redraw several times a second, and a mouse can move
  // over the terminal. Neither is somebody pressing a key.
  if (key == ftxui::Event::Custom || key.is_mouse() ||
      key.is_cursor_position() || key.is_cursor_shape()) {
    return;
  }
  keybinds_panel_.StopCapture();
  KeyAction action = keybinds_panel_.selected_action();
  int slot = keybinds_panel_.selected_slot();
  switch (keys_.Bind(action, slot, key)) {
    case BindOutcome::kBound:
      break;
    case BindOutcome::kReserved:
      keybinds_panel_.ShowRefusal(keys_.LabelOf(key) + " belongs to " +
                                  KeyActionName(keys_.ReservedFor(key)) +
                                  " and cannot move.");
      break;
    case BindOutcome::kUnsupported:
      keybinds_panel_.ShowRefusal("That key cannot be bound.");
      break;
  }
}

bool TuiController::OnKeybindsEvent(ftxui::Event event) {
  if (keybinds_panel_.capturing()) {
    TakeCapturedKey(event);
    return true;
  }
  if (event == ftxui::Event::ArrowUp) {
    keybinds_panel_.MoveRow(-1);
    return true;
  }
  if (event == ftxui::Event::ArrowDown) {
    keybinds_panel_.MoveRow(1);
    return true;
  }
  if (event == ftxui::Event::ArrowLeft) {
    keybinds_panel_.MoveSlot(-1);
    return true;
  }
  if (event == ftxui::Event::ArrowRight) {
    keybinds_panel_.MoveSlot(1);
    return true;
  }
  if (IsForward(event)) {
    if (keybinds_panel_.on_close()) {
      LeaveKeybinds();
      return true;
    }
    keybinds_panel_.StartCapture();
    return true;
  }
  if (IsBack(event)) {
    // Escape clears the key under the cursor. With nothing there to clear --
    // an empty slot, or the Close button -- it is the way out instead.
    KeyAction action = keybinds_panel_.selected_action();
    int slot = keybinds_panel_.selected_slot();
    if (keybinds_panel_.on_close() || keys_.Label(action, slot).empty()) {
      LeaveKeybinds();
      return true;
    }
    keys_.Unbind(action, slot);
    return true;
  }
  return true;
}

bool TuiController::OnShopEvent(ftxui::Event event) {
  if (IsBack(event)) {
    screen_ = kMain;
    return true;
  }
  if (IsForward(event)) {
    shop_panel_.OpenMenu();
    if (shop_panel_.menu_open()) {
      screen_ = kShopMenu;
    }
    return true;
  }
  shop_panel_.OnEvent(event);
  // Swallow everything else: this is a modal screen.
  return true;
}

bool TuiController::OnShopMenuEvent(ftxui::Event event) {
  Screen next = shop_panel_.OnMenuEvent(event);
  if (next == kShopBuy && shop_panel_.selected_buy_back() != nullptr) {
    OpenBuyBackDialog(*shop_panel_.selected_buy_back());
    screen_ = next;
    return true;
  }
  if (next == kShopBuy) {
    const EquipPrototype* item = shop_panel_.selected_item();
    const ItemPrototype* stackable = shop_panel_.selected_stackable();
    if (item == nullptr && stackable == nullptr) {
      screen_ = kShop;
      return true;
    }
    if (item != nullptr) {
      buy_item_ = item->name();
      // Priced in whatever the shelf it came off asks for: the token it names,
      // or meso when it names none.
      const ItemPrototype* token = shop_panel_.selected_token();
      int64_t balance = token == nullptr ? state_.character.meso()
                                         : state_.character.CountItem(*token);
      int price = token == nullptr ? item->shop_price() : item->token_price();
      buy_panel_.Reset(item->name(), price, balance,
                       state_.character.RoomFor(*item),
                       state_.character.CountOwned(*item), token);
    } else {
      buy_item_ = stackable->name();
      buy_panel_.Reset(stackable->name(), stackable->shop_price(),
                       state_.character.meso(),
                       state_.character.RoomFor(*stackable),
                       state_.character.CountItem(*stackable));
    }
  }
  if (next == kShopInspect) {
    OpenInspectCards();
  }
  screen_ = next;
  return true;
}

// Back to the shop rather than the bag: inspecting is how a player decides
// whether to buy, so the list is where they were going next either way.
bool TuiController::OnShopInspectEvent(ftxui::Event event) {
  return OnCardEvent(event, inspect_panel_, kShop);
}

// The shelf holds one row per sale, so the amount a row offers is the whole
// of that sale and an equip is always the one item. Priced at what the sale
// paid, which is the only price the row has.
void TuiController::OpenBuyBackDialog(const BuyBackEntry& entry) {
  buy_back_row_ = shop_panel_.selected_row();
  if (entry.has_equip()) {
    buy_item_ = entry.equip().equip_name();
    // One item, so one is also the ceiling: the row IS the sale, and there is
    // no second copy of it behind the first.
    buy_panel_.Reset(buy_item_, static_cast<int>(entry.unit_price()),
                     state_.character.meso(),
                     std::min(1, state_.character.inventory().room()),
                     /*owned=*/0);
    return;
  }
  buy_item_ = entry.stack().name();
  const ItemPrototype* proto = FindItemByName(state_.items, buy_item_);
  buy_panel_.Reset(
      buy_item_, static_cast<int>(entry.unit_price()), state_.character.meso(),
      std::min(entry.stack().count(),
               proto == nullptr ? 0 : state_.character.RoomFor(*proto)),
      proto == nullptr ? 0 : state_.character.CountItem(*proto));
}

// Everything the confirmed dialog buys, whichever shelf it was opened on. The
// selection is RE-READ and checked against what the dialog was opened on, so a
// cursor that moved underneath cannot buy something never chosen.
void TuiController::BuyWhatTheDialogAgreedTo() {
  const BuyBackEntry* entry = shop_panel_.selected_buy_back();
  if (entry != nullptr) {
    std::string name = entry->has_equip() ? entry->equip().equip_name()
                                          : entry->stack().name();
    if (shop_panel_.selected_row() == buy_back_row_ && name == buy_item_) {
      state_.character.BuyBack(buy_back_row_, buy_panel_.quantity(),
                               state_.equips, state_.items);
    }
    return;
  }
  const EquipPrototype* item = shop_panel_.selected_item();
  const ItemPrototype* stackable = shop_panel_.selected_stackable();
  const ItemPrototype* token = shop_panel_.selected_token();
  if (item != nullptr && item->name() == buy_item_) {
    if (token != nullptr) {
      state_.character.BuyWithToken(*item, *token, buy_panel_.quantity());
    } else {
      state_.character.Buy(*item, buy_panel_.quantity());
    }
  } else if (stackable != nullptr && stackable->name() == buy_item_) {
    state_.character.Buy(*stackable, buy_panel_.quantity());
  }
}

bool TuiController::OnShopBuyEvent(ftxui::Event event) {
  ConfirmChoice buy_choice = buy_panel_.OnEvent(event);
  // Taken once: both of these answer true only on the frame the player
  // pressed, and asking twice throws the answer away.
  if (buy_choice == ConfirmChoice::kConfirmed) {
    BuyWhatTheDialogAgreedTo();
    // Back to the shop rather than the bag: a player buying one thing is
    // usually buying two.
    screen_ = kShop;
  } else if (buy_choice == ConfirmChoice::kCancelled) {
    screen_ = kShop;
  }
  return true;
}

bool TuiController::OnSellEvent(ftxui::Event event) {
  ConfirmChoice sell_choice = sell_panel_.OnEvent(event);
  if (sell_choice == ConfirmChoice::kConfirmed) {
    state_.character.SellStackable(sell_index_, sell_panel_.quantity());
    screen_ = kMain;
  } else if (sell_choice == ConfirmChoice::kCancelled) {
    screen_ = kMain;
  }
  return true;
}

bool TuiController::OnSellEquipEvent(ftxui::Event event) {
  ConfirmChoice choice = sell_equip_panel_.OnEvent(event);
  if (choice == ConfirmChoice::kPending) {
    return true;
  }
  if (choice == ConfirmChoice::kConfirmed) {
    state_.character.SellEquip(bag_row_);
  }
  screen_ = kMain;
  return true;
}

bool TuiController::OnSymbolLevelEvent(ftxui::Event event) {
  ConfirmChoice choice = symbol_level_panel_.OnEvent(event);
  if (choice == ConfirmChoice::kPending) {
    return true;
  }
  if (choice == ConfirmChoice::kConfirmed) {
    state_.character.LevelUpSymbol(symbol_slot_);
  }
  screen_ = kMain;
  return true;
}

bool TuiController::OnHyperResetEvent(ftxui::Event event) {
  ConfirmChoice choice = hyper_reset_prompt_.OnEvent(event);
  if (choice == ConfirmChoice::kPending) {
    return true;
  }
  if (choice == ConfirmChoice::kConfirmed) {
    state_.character.ResetHyperStats(hyper_preset_);
  }
  screen_ = kMain;
  return true;
}

bool TuiController::OnVMatrixResetEvent(ftxui::Event event) {
  ConfirmChoice choice = v_matrix_reset_prompt_.OnEvent(event);
  if (choice == ConfirmChoice::kPending) {
    return true;
  }
  if (choice == ConfirmChoice::kConfirmed) {
    state_.character.ResetVMatrix(state_.skills);
  }
  screen_ = kMain;
  return true;
}

bool TuiController::OnAbilityRerollEvent(ftxui::Event event) {
  ConfirmChoice choice = ability_reroll_prompt_.OnEvent(event);
  if (choice == ConfirmChoice::kPending) {
    return true;
  }
  if (choice == ConfirmChoice::kConfirmed) {
    const AbilityRank before = state_.character.ability(ability_preset_).rank();
    state_.character.ResetAbility(ability_preset_);
    ability_rank_up_ =
        state_.character.ability(ability_preset_).rank() > before;
  }
  screen_ = kMain;
  return true;
}

bool TuiController::OnSymbolCombineEvent(ftxui::Event event) {
  ConfirmChoice choice = symbol_combine_panel_.OnEvent(event);
  if (choice == ConfirmChoice::kConfirmed) {
    state_.character.CombineSymbols(symbol_slot_,
                                    symbol_combine_panel_.quantity());
    screen_ = kMain;
  } else if (choice == ConfirmChoice::kCancelled) {
    screen_ = kMain;
  }
  return true;
}

bool TuiController::OnMultiSellEvent(ftxui::Event event) {
  ConfirmChoice choice = multi_sell_panel_.OnEvent(event);
  if (choice == ConfirmChoice::kConfirmed) {
    SellBasket(state_.character, multi_sell_panel_.basket());
    screen_ = kMain;
  } else if (choice == ConfirmChoice::kCancelled) {
    screen_ = kMain;
  }
  return true;
}

}  // namespace ms
