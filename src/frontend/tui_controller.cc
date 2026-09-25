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

// The reply to Accept when the bag can't hold the other side's offer. The
// notice wraps it onto several lines.
constexpr char kBagTooFullMessage[] =
    "Your inventory is too full to accept this trade.";

}  // namespace

namespace {

// Width a notice's sentence is wrapped to: wide enough that the longest takes
// two lines, narrow enough that neither line is tiny.
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
      jukebox_panel_(screens.jukebox_panel),
      analysis_(analysis),
      keys_(keys),
      shop_panel_(screens.shop_panel),
      buy_panel_(screens.buy_panel),
      bank_panel_(screens.bank_panel),
      link_skill_panel_(screens.link_skill_panel),
      character_select_panel_(state),
      panel_focus_(panel_focus),
      multiplayer_(multiplayer) {
  // The Inspect screen's panels call these on Enter. They are connected here
  // because every one of those screens is opened by this controller.
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

// Every screen with an inspect card opens it scrolled to the top, with the left
// half of the screen taking the arrows.
void TuiController::OpenInspectCards() {
  inspect_panel_.Reset();
  compare_slot_.reset();
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
  if (inventory_panel_.on_bank_tab()) {
    OpenBank();
    return;
  }
  screen_ = kItemMenu;
  // Enter on a tab opens the tab's menu; Enter on a row opens the item's.
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
    // Hidden rather than dimmed: no other skill in the book has anything to
    // toggle, and a greyed entry on all of them would suggest something that
    // isn't coming.
    skill_menu_.Hide(kSkillMenuToggle);
  } else if (state_.character.skill_level(skill) <= 0) {
    skill_menu_.Disable(kSkillMenuToggle);
  }
  screen_ = kSkillMenu;
}

void TuiController::OpenSkillInspect(const Skill& skill) {
  skill_inspect_ = skill;
  card_from_inspect_ = false;
  skill_card_return_ = screen_ == kLinkSkillMenu ? kLinkSkills : kMain;
  skill_inspect_panel_.ResetScroll();
  screen_ = kSkillInspect;
}

// The character the open card is about: the player, or the party member on the
// Inspect screen.
const CharacterInstance& TuiController::card_character() const {
  return card_from_inspect_ ? player_inspect_panel_.character()
                            : state_.character;
}

// Read live rather than stored, so spending a point and inspecting again shows
// the new level. This is the learned level; the card decides how to show the
// extra granted levels.
int TuiController::skill_inspect_level() const {
  if (skill_inspect_.link_line() != JOB_UNSPECIFIED) {
    // The account's progress on that line, so a skill viewed before it is
    // equipped shows the level it would have rather than 0.
    return card_character().LinkSkillLevelOffered(skill_inspect_);
  }
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
  // Starts on Cancel: the points are refunded, but the allocation isn't
  // restored, and it is fourteen rows of work.
  hyper_reset_prompt_.Open(/*cancel_selected=*/true);
  screen_ = kHyperReset;
}

void TuiController::OpenVMatrixReset() {
  // Starts on Cancel, like the Hyper question: the points are refunded, but
  // rebuilding a matrix is much more work than fourteen rows.
  v_matrix_reset_prompt_.Open(/*cancel_selected=*/true);
  screen_ = kVMatrixReset;
}

std::string TuiController::hyper_reset_question() const {
  // Uses the tab's own name, so the question says what the row does. No in-use
  // mark, since which preset is in use isn't what is being reset.
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
  // Starts on Confirm: a player rerolling usually rerolls many times, and each
  // costs the same honor whatever comes out.
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
  // The switch is labelled with what pressing it does, i.e. the state it would
  // put the buff in, not its current state.
  buff_menu_.SetLabel(kBuffMenuToggle, state_.character.ConsumableActive(type)
                                           ? "Disable"
                                           : "Enable");
  // A buff already bought can't be bought again. The entry stays on the menu,
  // greyed out, since a missing entry would be more surprising.
  if (state_.character.ConsumableOwned(type)) {
    buff_menu_.Disable(kBuffMenuBuyPerm);
  }
  screen_ = kBuffMenu;
}

void TuiController::OpenBuffBuy(ConsumableType type) {
  buff_type_ = type;
  // Starts on Cancel: buying a buff permanently costs hundreds of millions, so
  // Enter alone must not buy it.
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
  preset_return_ = kMain;
  preset_menu_.Reset();
  // Use does nothing while autoswap is choosing, or on the preset already in
  // use. The entry stays visible either way, since a missing entry would be
  // more surprising.
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
      // Starts on the preset the menu was opened on, since swapping it with
      // itself does nothing.
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
    // Cancel comes after the last preset, and the cursor wraps around.
    preset_move_row_ =
        StepCursor(preset_move_row_, event == ftxui::Event::ArrowUp ? -1 : 1,
                   kNumStatPresets + 1);
    return true;
  }
  if (IsBack(event)) {
    screen_ = preset_return_;
    return true;
  }
  if (!IsForward(event)) {
    return true;
  }
  if (preset_move_row_ < kNumStatPresets) {
    state_.character.SwapPresets(preset_kind_, preset_slot_,
                                 StatPresetAt(preset_move_row_));
  }
  screen_ = preset_return_;
  return true;
}

void TuiController::OpenJobAdvance(Job job) {
  job_advance_ = job;
  // Starts on Cancel: an advancement can't be undone, so Enter alone must not
  // pick a job the player was only reading about.
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
    // The fight stops as soon as this opens (see OnCharacterSelect) and resumes
    // on Escape or Play.
    character_select_panel_.Reset();
    screen_ = kCharacterSelect;
    return;
  }
  if (entry != MenuEntry::kBoss) {
    // The box opens with the cursor still on the entry below it, which the
    // player presses Up to leave.
    menu_panel_.OpenBox(entry);
    screen_ = kMenuBox;
    return;
  }
  // Opening the screen is what the gold was pointing to, so it clears here.
  state_.account.MarkSeen(MenuPanel::boss_seen_key());
  screen_ = kBossSelect;
  boss_select_panel_.Reset();
}

bool TuiController::Connected() {
  MultiplayerSnapshot lobby = Lobby();
  if (lobby.state == ConnectionState::kConnected) {
    return true;
  }
  // Request a reconnect on the way out. Whatever blocked the connection may be
  // fixed (usually a server redeploy), and finding out shouldn't require
  // restarting the game.
  if (multiplayer_ != nullptr) {
    multiplayer_->client().Reconnect();
  }
  // The notice is shown over the main view rather than the box that opened it,
  // so closing it takes the player somewhere real.
  menu_panel_.CloseBox();
  screen_ = kMain;
  RaisePartyNotice(
      lobby.message.empty() ? "Could not reach the server." : lobby.message,
      /*refusal=*/true);
  return false;
}

void TuiController::LeaveMultiplayerScreen() {
  // Back to the box it was opened from, which is still open where the player
  // left it, the same way Keybinds and Options close. Going to the main view
  // instead would leave the box open with the cursor inside it, and the menu
  // row draws no cursor while that is the case.
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

// The preset the Equipped panel is showing: the gear the player is looking at,
// and the gear Equip would replace.
StatPreset TuiController::ComparisonPreset() const {
  return equip_panel_.gear_preset();
}

const EquipInstance* TuiController::WornForComparison(
    const EquipPrototype& proto) const {
  EquipSlot slot = ComparisonSlot(
      proto, state_.character.SlotToFill(proto, ComparisonPreset()));
  if (slot == EQUIP_SLOT_UNSPECIFIED) {
    return nullptr;
  }
  return state_.character.WornAt(ComparisonPreset(), slot);
}

EquipSlot TuiController::ComparisonSlot(const EquipPrototype& proto,
                                        EquipSlot fallback) const {
  std::vector<EquipSlot> family = SlotFamily(proto.equip_slot());
  if (!compare_slot_.has_value() || family.size() < 2) {
    return fallback;
  }
  return family[std::clamp(*compare_slot_, 0,
                           static_cast<int>(family.size()) - 1)];
}

InspectPanel::ComparisonSlots TuiController::comparison_slots() const {
  ComparisonSubject subject = InspectSubject();
  if (subject.proto == nullptr) {
    return {};
  }
  InspectPanel::ComparisonSlots slots;
  EquipSlot showing = ComparisonSlot(*subject.proto, subject.fallback);
  for (EquipSlot slot : SlotFamily(subject.proto->equip_slot())) {
    if (slot == showing) {
      slots.active = static_cast<int>(slots.worn.size());
    }
    slots.worn.push_back(state_.character.WornAt(ComparisonPreset(), slot));
  }
  return slots;
}

// The shop item or buy-back row kShopInspect is showing, as a prototype. A
// stackable has no slot and returns nullptr.
const EquipPrototype* TuiController::ShopInspectProto() const {
  const BuyBackEntry* entry = shop_panel_.selected_buy_back();
  if (entry == nullptr) {
    return shop_panel_.selected_item();
  }
  if (!entry->has_equip()) {
    return nullptr;
  }
  return FindEquipByName(state_.equips, entry->equip().equip_name());
}

TuiController::ComparisonSubject TuiController::InspectSubject() const {
  const EquipTabItem* item = nullptr;
  switch (screen_) {
    // An item already worn is itself the comparison, so it is compared against
    // nothing and its card has no tab bar.
    case kInspect:
      item = subject_.equipped() ? nullptr : inspect_item();
      break;
    case kShopInspect: {
      const EquipPrototype* proto = ShopInspectProto();
      if (proto == nullptr) {
        return {};
      }
      return {proto, state_.character.SlotToFill(*proto, ComparisonPreset())};
    }
    case kBankInspect:
      item = bank_panel_.selected_equip();
      break;
    case kTradeInspect:
      item = trade_inspect_equip();
      break;
    // The member's own slot, not the one Equip would fill: the viewer wants to
    // know what they wear in the same slot, and the member's gear isn't moving.
    case kPlayerItemInspect: {
      const EquipInstance* theirs = player_inspect_panel_.selected_item();
      if (theirs == nullptr) {
        return {};
      }
      return {&theirs->prototype(), player_inspect_panel_.selected_slot()};
    }
    default:
      return {};
  }
  if (item == nullptr) {
    return {};
  }
  return {&item->prototype(),
          state_.character.SlotToFill(item->prototype(), ComparisonPreset())};
}

bool TuiController::StepComparisonSlot(int step) {
  ComparisonSubject subject = InspectSubject();
  if (subject.proto == nullptr) {
    return false;
  }
  std::vector<EquipSlot> family = SlotFamily(subject.proto->equip_slot());
  int count = static_cast<int>(family.size());
  if (count < 2) {
    return false;
  }
  EquipSlot showing = ComparisonSlot(*subject.proto, subject.fallback);
  int at = 0;
  for (int i = 0; i < count; ++i) {
    if (family[i] == showing) {
      at = i;
    }
  }
  compare_slot_ = (at + count + step % count) % count;
  return true;
}

std::optional<int> TuiController::CombatPowerDelta(
    const EquipTabItem* item, std::optional<EquipSlot> fallback) const {
  if (item == nullptr) {
    return std::nullopt;
  }
  const StatPreset gear = ComparisonPreset();
  // The slot the Equipped card is showing, so the number prices the swap the
  // card describes. For a ring, that is whichever of the four the tab bar is
  // on.
  const EquipSlot slot = ComparisonSlot(
      item->prototype(),
      fallback.value_or(state_.character.SlotToFill(item->prototype(), gear)));
  if (slot == EQUIP_SLOT_UNSPECIFIED) {
    return std::nullopt;
  }
  // The activity the preset stands for, so the Boss tab values a piece by what
  // it is worth against a boss. The third preset belongs to no activity and is
  // treated like the first.
  const Activity activity =
      gear == StatPreset::kSecond ? Activity::kBossing : Activity::kFarming;
  const int now =
      CharacterCombatPower(state_.character, state_.skills, activity, gear);
  const int worn =
      CharacterCombatPower(state_.character.Wearing(*item, gear, slot),
                           state_.skills, activity, gear);
  return worn - now;
}

std::optional<int> TuiController::inspect_delta() const {
  // An item already worn would replace itself, and a stackable is never worn.
  // Neither has a number to show.
  if (subject_.equipped()) {
    return std::nullopt;
  }
  return CombatPowerDelta(inspect_item());
}

std::optional<int> TuiController::player_item_delta() const {
  // Priced into the slot the member wears it in, which is the slot this
  // screen's card opens on; see player_item_comparison.
  return CombatPowerDelta(player_inspect_panel_.selected_item(),
                          player_inspect_panel_.selected_slot());
}

// A second copy of a worn ring compares against the worn one, which is what
// Equip would swap it for.
const EquipTabItem* TuiController::inspect_comparison() const {
  // An item already worn is itself the comparison, so there is nothing to
  // compare it with.
  if (subject_.equipped()) {
    return nullptr;
  }
  const EquipTabItem* item = inspect_item();
  if (item == nullptr) {
    return nullptr;
  }
  return WornForComparison(item->prototype());
}

// The viewer's own item in the slot the member's cursor is on. By slot rather
// than by what it would replace: the member's gear isn't moving, and the viewer
// wants to know what they wear in the same slot.
const EquipTabItem* TuiController::player_item_comparison() const {
  const EquipInstance* theirs = player_inspect_panel_.selected_item();
  EquipSlot slot = player_inspect_panel_.selected_slot();
  if (theirs == nullptr || slot == EQUIP_SLOT_UNSPECIFIED) {
    return nullptr;
  }
  return state_.character.WornAt(ComparisonPreset(),
                                 ComparisonSlot(theirs->prototype(), slot));
}

// Keys on the main view, after every screen above it has had a chance. Back
// here means leaving the game, since there is nothing left to back out of.
bool TuiController::OnMainViewEvent(ftxui::Event event) {
  // An open name field takes every key it can: Escape closes the field rather
  // than the game, and Tab must not move focus off a panel mid-edit. This only
  // declines the keys so the panel receives them.
  if (char_panel_.editing_username()) {
    return false;
  }
  if (IsBack(event)) {
    if (expanded_panel_ != kNoPanel) {
      // An expanded panel fills the screen, so Escape closes it rather than the
      // game, like [Close], one view at a time.
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
    // Nowhere to move to: the other panels aren't drawn.
    return true;
  }
  // Move to the next panel on screen, wrapping around. The character panel is
  // always on screen, so this always lands. Backwards adds kNumPanels - 1
  // rather than subtracting 1, so the modulo never sees a negative and both
  // directions share one path.
  int step = event == ftxui::Event::Tab ? 1 : kNumPanels - 1;
  do {
    panel_focus_ = (panel_focus_ + step) % kNumPanels;
  } while (!PanelVisible(panel_focus_));
  // Arriving on a panel counts as viewing whichever tab was left open on it.
  // Without this, a gold tab the player is already on could only be cleared by
  // moving off it and back.
  if (panel_focus_ == kInventoryPanel) {
    inventory_panel_.MarkActiveTabSeen();
  }
  if (panel_focus_ == kCharPanel) {
    char_panel_.MarkActiveTabSeen();
  }
  return true;
}

bool TuiController::OnEvent(ftxui::Event event) {
  // A panel can disappear from under the cursor: the game starts focused on the
  // equipped panel, which a level 1 character hasn't unlocked. This is fixed
  // before dispatch, so no key reaches a panel that isn't drawn.
  EnsureFocusIsVisible();
  // A rank-up stays gold until the player's next action, counted here. It is
  // cleared before dispatch, so the key that raises the rank sets it again on
  // its way through. Custom is the ticker's redraw, not a player action.
  if (event != ftxui::Event::Custom) {
    ability_rank_up_ = false;
    cube_panel_.SetRankUp(false);
  }
  // The notice is drawn over whatever is on screen, so it gets keys before the
  // screen under it.
  if (party_notice_prompt_.open()) {
    party_notice_prompt_.OnEvent(event);
    return true;
  }
  switch (screen_) {
    case kItemMenu:
      return OnItemMenuEvent(event);
    // Both are one screen to the player: any key closes it.
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
    // Read-only screens, so they close the same way.
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
    case kBossAnalysis:
      return OnBossAnalysisEvent(event);
    case kBank:
      return OnBankEvent(event);
    case kBankMenu:
      return OnBankMenuEvent(event);
    case kLinkSkills:
      return OnLinkSkillsEvent(event);
    case kLinkSkillMenu:
      return OnLinkSkillMenuEvent(event);
    case kBankAmount:
      return OnBankAmountEvent(event);
    case kBankInspect:
      return OnCardEvent(event, inspect_panel_, kBank);
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
    case kJukebox:
      return OnJukeboxEvent(event);
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
  // All four act on the item under the cursor. It is resolved here so nothing
  // later needs to check which panel had focus.
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
    // A trace sells for nothing whatever its prototype says, so the dialog is
    // told what the sale will really pay rather than the item's price.
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
      return kMain;  // the row disappeared from under the menu
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
    // Uses the spare's own slot: it can only be fed to the symbol of its area,
    // whichever bag row the cursor was on.
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

// Inspect screens are read-only, so Confirm and Cancel both return to `back`.
// The arrows scroll whichever card has focus (sideways too, for a card narrower
// than its rows), and Tab moves focus to the next card if there is one.
// Everything else is swallowed, since these are modal screens.
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
  if (event == ftxui::Event::ArrowLeft || event == ftxui::Event::ArrowRight) {
    int step = event == ftxui::Event::ArrowRight ? 1 : -1;
    // The Equipped card's tab bar comes first, for an item with a family of
    // slots. Nothing else on that card scrolls sideways (only the set card is
    // ever squeezed), so the two never need the same key.
    if (panel.focused_card() != InspectPanel::kEquippedCard ||
        !StepComparisonSlot(step)) {
      panel.ScrollXBy(step);
    }
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
  // The item card and the scroll list take turns with the arrows. Not while a
  // dialog is up, since the keys belong to it until it closes.
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
    // The panel reads the pin but never writes it: the record belongs to the
    // character and is saved with it.
    state_.character.ToggleScrollPin(scroll_panel_.PinKeyOfSelected());
    scroll_panel_.Resort();
  }
  if (scroll_panel_.TakeScrollChosen()) {
    // Checked before the confirm window opens, so a scroll with no valid target
    // says so instead of asking the player to pay first.
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
    // Paid for before it is used, and only used if paid for. The panel won't
    // confirm what the player can't afford, so this is a second check rather
    // than the first.
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
      // The switch flips and the menu closes, since the player wants to see the
      // change in the book behind it.
      state_.character.ToggleSkill(skill_menu_skill_);
      screen_ = kMain;
      break;
    default:
      screen_ = kMain;
      break;
  }
  return true;
}

// Read-only, so either key leaves, the same way the item inspect screen closes.
// The All Stats screen uses this too, since it has no card to scroll.
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
    // Back to the screen that opened the card: the player's own panels, the
    // Link Skills screen, or the Inspect screen, which opens the same two cards
    // for a member.
    screen_ = card_from_inspect_ ? kPlayerInspect : skill_card_return_;
  }
  return true;
}

void TuiController::OpenQuit() {
  // Starts on Cancel: an accidental Escape doesn't mean the player wants to
  // quit, and a stray Enter after it shouldn't end the session.
  quit_prompt_.Open(/*cancel_selected=*/true);
  quit_return_ = OnCharacterSelect() ? kCharacterSelect : kMain;
  screen_ = kQuit;
}

bool TuiController::OnQuitEvent(ftxui::Event event) {
  ConfirmChoice choice = quit_prompt_.OnEvent(event);
  if (choice == ConfirmChoice::kConfirmed) {
    // Only sets the flag. Tui owns the ftxui screen and is the only thing that
    // can end its loop.
    quit_requested_ = true;
    screen_ = kMain;
  } else if (choice == ConfirmChoice::kCancelled) {
    // Back to where it was opened. The character select has no other way out,
    // so cancelling there must not put the player into a game without choosing
    // a character.
    screen_ = quit_return_;
  }
  return true;
}

void TuiController::SwitchedCharacter() {
  character_switched_ = true;
  // The party included the previous character's sheet, and the new character
  // hasn't asked to join it.
  if (multiplayer_ != nullptr && !Lobby().party.id().empty()) {
    multiplayer_->client().LeaveParty();
  }
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
  if (event == ftxui::Event::ArrowLeft || event == ftxui::Event::ArrowRight) {
    // Left and Right move along the button row, or between the card's Farm/Boss
    // tabs everywhere else; each ignores the other's row.
    int delta = event == ftxui::Event::ArrowLeft ? -1 : 1;
    character_select_panel_.MoveButton(delta);
    character_select_panel_.SwitchActivity(delta);
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
    // Back to the current character: a resume, like Play on their row. Quitting
    // has its own button.
    LeaveCharacterSelect(/*switched=*/false);
    return true;
  }
  // Swallow everything else, since this is a modal screen.
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
      // On the character already in play this resumes, and their running fight
      // is still theirs.
      LeaveCharacterSelect(PlayCharacter(state_, slot));
      return;
    case kCharacterMenuSetOffline:
      SetOfflineCharacter(state_, slot);
      // Straight back to the list, where the check mark has moved: nothing
      // needs confirming, and seeing it move is the feedback.
      character_select_panel_.Refresh();
      screen_ = kCharacterSelect;
      save_wanted_ = true;
      return;
    case kCharacterMenuDelete:
      character_delete_slot_ = slot;
      // The menu closes behind the question, since the question is about the
      // row and the entry has already been pressed.
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
    // Deleting the current character put someone else in play, so the fight and
    // watcher are told either way. The save happens now, so an autosave can't
    // bring the row back.
    SwitchedCharacter();
  }
  character_delete_slot_ = -1;
  character_select_panel_.Refresh();
  screen_ = kCharacterSelect;
  save_wanted_ = true;
  return true;
}

void TuiController::LeaveCharacterSelect(bool switched) {
  if (switched) {
    SwitchedCharacter();
    // The new character is on their own map with their own panels, so the
    // cursor starts where a session starts.
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
      // Nothing to confirm or spend, so the switch takes effect on the key
      // press and the menu closes behind it.
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

// Nothing to select, only text to read, and the card is short enough not to
// scroll. Back returns to the menu it was opened from, as the job card does, so
// the decision is one key press away.
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

// Read-only, so only Up and Down do anything. Back returns to the menu the
// screen was opened from rather than the main view, since the player came to
// decide and the decision is one key press away on the menu.
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
  // The item as it is on the left, with one more star on the right. Tab picks
  // which one the up and down arrows scroll; left and right stay with the
  // button row, so the two never compete for a key.
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
  // Given the meso before any key is handled: the panel greys its own Confirm
  // based on the meso, and a key press shouldn't have to wait for the next
  // render to know it.
  cube_panel_.SetItem(cube_item(), state_.character.meso());
  bool busy = cube_panel_.IsConfirming();
  if (IsBack(event) && !busy) {
    screen_ = kMain;
    return true;
  }
  // Tab moves the arrows to the card beside the shelf, as on the star force
  // screen: a Legendary potential has more rows than a small terminal.
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
    // The window stays up over the item it just rerolled, which is the point:
    // the player watches the lines change and presses again.
    const PotentialRank before = cube_item()->potential().rank();
    CubeItem(state_.character, subject_, cube_panel_.selected_cube());
    // The first cube on an item without potential always gives a Rare
    // potential, so it is a grant rather than a rank-up and the window stays
    // steel blue.
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
  // Two cards, with the tabs between them using Left and Right. Tab picks which
  // card the arrows scroll, starting on the recovered item's.
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
  // Left and Right belong to the tab bar. The panel enforces that rule, so it
  // holds for every caller, not just this handler.
  if (event == ftxui::Event::ArrowLeft) {
    map_select_panel_.ChangePage(-1);
    return true;
  }
  if (event == ftxui::Event::ArrowRight) {
    map_select_panel_.ChangePage(1);
    return true;
  }
  if (IsForward(event)) {
    // The menu decides what to do with the map, since going there is one of
    // three things the player might want.
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
  // Swallow everything else, since this is a modal screen.
  return true;
}

bool TuiController::OnMapMenuEvent(ftxui::Event event) {
  Screen next = map_select_panel_.OnMenuEvent(event);
  if (next == kMain) {
    // Move. Travel is free, so the selected map is always a valid destination,
    // and the fight restarts itself once it sees the new map.
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
  // Back to the list it was opened from, so a player browsing a band's mobs
  // isn't sent back to the main view after each one.
  if (IsBack(event) || IsForward(event)) {
    screen_ = kMapSelect;
    return true;
  }
  // Swallow everything else, since this is a modal screen.
  return true;
}

namespace {

// Whether `account_id` is still on the roster. A player who has left has
// nothing to show.
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
  // Wrapped here rather than by the server, since dialog width is the client's
  // concern, and an unwrapped sentence used to stretch the dialog as far as it
  // went. Line breaks in the message are kept, and each part is wrapped
  // separately.
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
  // Losing the connection closes the multiplayer screens: there is no lobby
  // left to show, and Close should take the player somewhere real.
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
    // Nothing more will arrive, and there is no point watching a fight whose
    // end won't be heard. The rest of the party fights on without them.
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
    // A sheet lists items by name rather than describing them, so it is rebuilt
    // against this build's catalogs, the same way the inspect screen reads one
    // and a save is loaded.
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
    // A fight this build doesn't have, so nothing can be drawn for it.
    party_fight_->Forget();
    return;
  }
  boss_run_key_ = party_fight_->boss_key();
  boss_run_difficulty_ = it->second.difficulties(index).name();
  // Before the run, which calculates the character's damage once, and the party
  // is part of the character's stats for the whole fight.
  SeatParty(lobby);
  ChargeBossEntry();
  boss_run_ =
      std::make_unique<BossRun>(boss_run_key_, it->second, index,
                                party_fight_.get(), party_fight_->practice());
  // Whatever they were doing, they are in a fight now. The Menu box closes too:
  // leaving the fight goes to the main view, whose menu row hides its cursor
  // while a box is open.
  menu_panel_.CloseBox();
  party_select_panel_.CloseMenu();
  party_prompt_.Close();
  screen_ = kBossFight;
}

bool TuiController::in_party_fight() const {
  return party_fight_ != nullptr && party_fight_->fighting();
}

void TuiController::DropBossRun() {
  boss_run_.reset();
  // The party's skills apply to this character only for the fight; afterwards
  // they farm alone.
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
    // Check the roster first: their last sheet is still here, so a screen
    // reading it would keep showing someone who has left.
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
      // Redrawn from the latest data, so a member who levels or changes gear
      // while being viewed shows it.
      player_inspect_panel_.SetPlayer(member.player());
      return;
    }
  }
  // They left or were kicked, so there is nothing left to show.
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
  // They left before their sheet arrived. The player stays on the list rather
  // than seeing an empty sheet.
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
  // They left between the menu opening and Enter. Nothing to show, so the
  // player stays where they were rather than seeing an empty sheet.
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
  // Where the screen returns to, recorded now: it is the list the player
  // pressed Trade on, and they are still on it when the server answers.
  trade_return_ = screen_ == kPartySelect ? kPartySelect : kPlayerList;
  multiplayer_->client().RequestTrade(account_id);
}

void TuiController::AdvanceTrade(const MultiplayerSnapshot& lobby) {
  trade_panel_.SetTrade(lobby.trade);
  // Before anything else about the trade: the payment shows it completed, and
  // the empty state after it would otherwise look like the other side left.
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
  // A trade this player already left: the server hasn't caught up yet, and
  // reopening the screen would trap them on it.
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
  // Recorded before anything moves: offered items are identified by their bag
  // position, which the first removal changes.
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
  // The dialog follows the trade state, not a key press: it opens when both
  // sides have accepted, whichever accepts second, and closes as soon as either
  // withdraws.
  const bool both = trade.mine_accepted() && trade.theirs_accepted();
  // Not over the leave confirmation: the player must answer that one, and the
  // answer may end the trade anyway.
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
  // Starts on the amount already offered, with a button for none: a player
  // changing their mind removes things as often as adding them.
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
  // The whole offer every time, so two changes in flight can't combine into an
  // offer nobody made.
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
    // Asks rather than leaving immediately: Escape is one key away from
    // everywhere, and leaving ends the trade for both players.
    trade_leave_prompt_.Open();
    screen_ = kTradeLeave;
  }
  // Everything else is swallowed: this is a modal screen, and the ticker's
  // redraw also arrives as an event.
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
  // [MAX] reaches the whole stack, however much is already offered: offering
  // something doesn't change what the player owns.
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
  // The item is copied rather than pointed to, because one of the three places
  // it can come from is a snapshot held by value.
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
  OpenInspectCards();
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
  // Stays open either way: it closes when the answer comes back, either the
  // trade completing or the acceptance this cancel just cleared.
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

void TuiController::OpenBank() {
  bank_panel_.Reset();
  screen_ = kBank;
}

bool TuiController::OnBankEvent(ftxui::Event event) {
  if (IsSwitchPanel(event)) {
    bank_panel_.NextZone();
    return true;
  }
  if (event == ftxui::Event::ArrowLeft) {
    bank_panel_.MoveCursor(-1);
    return true;
  }
  if (event == ftxui::Event::ArrowRight) {
    bank_panel_.MoveCursor(1);
    return true;
  }
  if (event == ftxui::Event::ArrowUp) {
    bank_panel_.MoveRow(-1);
    return true;
  }
  if (event == ftxui::Event::ArrowDown) {
    bank_panel_.MoveRow(1);
    return true;
  }
  if (IsForward(event)) {
    switch (bank_panel_.cursor().kind) {
      case BankCursor::Kind::kCurrency:
        OpenBankAmount();
        break;
      case BankCursor::Kind::kTab:
        bank_panel_.OpenTabMenu();
        screen_ = kBankMenu;
        break;
      case BankCursor::Kind::kRow:
        bank_panel_.OpenMenu();
        screen_ = kBankMenu;
        break;
      case BankCursor::Kind::kNothing:
        break;
    }
    return true;
  }
  if (IsBack(event)) {
    screen_ = kMain;
  }
  // Everything else is swallowed: this is a modal screen, and the ticker's
  // redraw also arrives as an event.
  return true;
}

void TuiController::OpenLinkSkills() {
  link_skill_panel_.Reset();
  screen_ = kLinkSkills;
}

bool TuiController::OnLinkSkillsEvent(ftxui::Event event) {
  if (IsSwitchPanel(event)) {
    link_skill_panel_.NextZone(event == ftxui::Event::Tab ? 1 : -1);
    return true;
  }
  if (event == ftxui::Event::ArrowUp || event == ftxui::Event::ArrowDown) {
    link_skill_panel_.MoveRow(event == ftxui::Event::ArrowUp ? -1 : 1);
    return true;
  }
  if (event == ftxui::Event::ArrowLeft || event == ftxui::Event::ArrowRight) {
    link_skill_panel_.MovePreset(event == ftxui::Event::ArrowLeft ? -1 : 1);
    return true;
  }
  if (IsForward(event)) {
    link_skill_panel_.OpenMenu();
    if (link_skill_panel_.menu_open() || link_skill_panel_.preset_menu_open()) {
      screen_ = kLinkSkillMenu;
    }
    return true;
  }
  if (IsBack(event)) {
    screen_ = kMain;
  }
  // Everything else is swallowed: this is a modal screen, and the ticker's
  // redraw also arrives as an event.
  return true;
}

bool TuiController::OnLinkSkillMenuEvent(ftxui::Event event) {
  if (event == ftxui::Event::ArrowUp || event == ftxui::Event::ArrowDown) {
    link_skill_panel_.MoveMenuCursor(event == ftxui::Event::ArrowUp ? -1 : 1);
    return true;
  }
  if (IsBack(event)) {
    link_skill_panel_.CloseMenu();
    screen_ = kLinkSkills;
    return true;
  }
  if (!IsForward(event)) {
    return true;  // The menu is modal: nothing behind it hears a key.
  }
  if (link_skill_panel_.preset_menu_open()) {
    int chosen = link_skill_panel_.preset_menu_selected();
    const StatPreset slot = link_skill_panel_.preset();
    link_skill_panel_.CloseMenu();
    screen_ = kLinkSkills;
    if (chosen == kPresetMenuUse) {
      state_.character.SetSlotInUse(PresetKind::kLinkSkills, slot);
    } else if (chosen == kPresetMenuMove) {
      // The Hyper tab's own popup, which returns to this screen.
      preset_kind_ = PresetKind::kLinkSkills;
      preset_slot_ = slot;
      preset_return_ = kLinkSkills;
      preset_move_row_ = IndexOf(slot);
      screen_ = kPresetMove;
    }
    return true;
  }
  const LinkMenuChoice chosen = link_skill_panel_.menu_choice();
  const Skill* skill = link_skill_panel_.cursor().skill;
  link_skill_panel_.CloseMenu();
  screen_ = kLinkSkills;
  switch (chosen) {
    case LinkMenuChoice::kInspect:
      if (skill != nullptr) {
        // The card shows the level the account has reached, whether or not this
        // character has the skill equipped yet.
        skill_inspect_ = *skill;
        card_from_inspect_ = false;
        skill_card_return_ = kLinkSkills;
        skill_inspect_panel_.ResetScroll();
        screen_ = kSkillInspect;
      }
      break;
    case LinkMenuChoice::kAdd:
      if (!link_skill_panel_.AddSelected()) {
        notification_.Raise({"You already have " +
                             std::to_string(kMaxEquippedLinkSkills) +
                             " skills."});
      }
      break;
    case LinkMenuChoice::kRemove:
      link_skill_panel_.RemoveSelected();
      break;
    case LinkMenuChoice::kClose:
      break;
  }
  return true;
}

void TuiController::OpenBankAmount() {
  bank_currency_ = bank_panel_.cursor().currency;
  bank_selector_.Reset(bank_panel_.held(bank_currency_));
  bank_selector_.set_low(0);
  screen_ = kBankAmount;
}

void TuiController::MoveInBank() {
  std::string error = bank_panel_.MoveSelected();
  if (!error.empty()) {
    notification_.Raise({error});
  }
}

bool TuiController::OnBankMenuEvent(ftxui::Event event) {
  if (event == ftxui::Event::ArrowUp) {
    bank_panel_.MoveMenuCursor(-1);
    return true;
  }
  if (event == ftxui::Event::ArrowDown) {
    bank_panel_.MoveMenuCursor(1);
    return true;
  }
  if (IsBack(event)) {
    bank_panel_.CloseMenu();
    screen_ = kBank;
    return true;
  }
  if (!IsForward(event)) {
    return true;
  }
  int chosen = bank_panel_.menu_selected();
  const bool tab_menu = bank_panel_.tab_menu_open();
  bank_panel_.CloseMenu();
  screen_ = kBank;
  if (tab_menu) {
    if (chosen == kBankTabMenuSort) {
      bank_panel_.SortActiveTab();
    }
    return true;
  }
  if (chosen == kBankMenuMove) {
    MoveInBank();
  } else if (chosen == kBankMenuInspect) {
    OpenInspectCards();
    screen_ = kBankInspect;
  }
  return true;
}

bool TuiController::OnBankAmountEvent(ftxui::Event event) {
  ConfirmChoice choice = bank_selector_.OnEvent(event);
  if (choice == ConfirmChoice::kPending) {
    return true;
  }
  if (choice == ConfirmChoice::kConfirmed) {
    bank_panel_.MoveCurrency(bank_currency_, bank_selector_.value());
  }
  screen_ = kBank;
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
  // The allocation their Character panel shows, so the card and the row behind
  // it always show the same level.
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
  // The card reads the item from the panel's cursor, so no pointer is held
  // across a tick that may rebuild the member.
  if (player_inspect_panel_.selected_item() != nullptr) {
    player_item_panel_.Reset();
    compare_slot_.reset();
    screen_ = kPlayerItemInspect;
  }
}

bool TuiController::OnPlayerInspectEvent(ftxui::Event event) {
  if (IsBack(event)) {
    // An expanded panel fills the screen, so Escape closes it first, like
    // [Close], one view at a time.
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
  // Left and Right belong to the member's Farm/Boss row, if they have one; the
  // panel decides.
  if (player_inspect_panel_.OnAllStatsEvent(event)) {
    return true;
  }
  // Only a key that means leaving closes it. Everything else is swallowed: the
  // ticker's redraw also arrives as an event, and a screen that closed on any
  // unrecognised event was gone by the next frame.
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
    // Five reasons a fight can't be started, each explaining why rather than
    // doing nothing. Party leadership is checked first, since it is about the
    // player rather than the chosen fight.
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
      // Named without the difficulty: clearing any difficulty locks them all,
      // so the one under the cursor may not be the one cleared.
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
    // A party fights together: the server checks every member, keeps one mob
    // roster for all of them, and puts them all in the arena. Alone, or in a
    // party of one, it is the local run below with no network involved.
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
  // Only one option on the row so far. The index is checked rather than
  // assumed, so a second option doesn't trigger this one.
  if (option == 0) {
    state_.boss_options.set_practice(!state_.boss_options.practice());
  }
}

void TuiController::ChargeBossEntry() {
  // Charged on entry, whether or not the fight is won, since the potion is
  // drunk before the fight starts. In a party, each member who has it on is
  // charged on their own client from their own meso.
  state_.character.ChargeConsumable(CONSUMABLE_TYPE_EXTREME_GREEN_POTION, 1);
}

bool TuiController::OnBossNoticeEvent(ftxui::Event event) {
  if (notice_prompt_.OnEvent(event)) {
    // The run is null for a notice shown instead of a fight (no weapon, or a
    // daily already done), and holds a finished run for a fight that ran out of
    // time.
    LeaveBossRun();
  }
  return true;
}

void TuiController::OpenNotice(Screen screen) {
  // The label for a one-button result's button. A notice that ends the flow
  // says "Close" instead; see the overload below.
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
    // Nothing to claim: the character has never had a symbol. It is reported
    // here rather than at the confirmation, where the claim's own refusal is a
    // full bag.
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
    // The only way an offered claim fails is a full bag. Taking only part would
    // lose the rest until tomorrow.
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
  // The arrows move the player between the phase's positions. The player
  // doesn't aim the attack; they choose where to stand.
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
  // Everything else is swallowed: the fight runs by itself.
  return true;
}

bool TuiController::OnBossAbortEvent(ftxui::Event event) {
  ConfirmChoice choice = boss_abort_prompt_.OnEvent(event);
  if (choice == ConfirmChoice::kConfirmed && boss_run_ != nullptr) {
    if (in_party_fight()) {
      // Left the party's fight, which continues without them: they deal no more
      // damage and get no rewards.
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
  // Only the fight screen runs the clock. The leave prompt pauses it while the
  // player decides, and so does the fight's result panel: the run is kept until
  // the player presses the button, so the arena stays behind the panel.
  if (boss_run_ == nullptr) {
    return;
  }
  // Only the fight screen runs the clock, and the leave prompt pauses it,
  // except in a party fight, where the others are still attacking and this
  // player's decision must not slow them down.
  if (screen_ != kBossFight && !(screen_ == kBossAbort && in_party_fight())) {
    return;
  }
  boss_run_->Advance(state_, elapsed_seconds);
  if (!boss_run_->done()) {
    return;
  }
  if (boss_run_->won()) {
    // A practice clear costs nothing: the fight isn't recorded, so the reset
    // clock never sees it.
    if (!boss_run_->practice()) {
      state_.character.RecordBossClear(
          boss_run_key_, boss_run_difficulty_,
          static_cast<int64_t>(std::time(nullptr)));
    }
    // Copied from the run rather than read through it, since the card is still
    // up after the run is gone.
    boss_clear_title_ = boss_run_->title();
    boss_clear_reward_ = boss_run_->reward();
    boss_clear_seconds_ = boss_run_->clear_seconds();
    boss_clear_prompt_.Open();
    boss_clear_on_analysis_ = false;
    screen_ = kBossClear;
    return;
  }
  if (boss_run_->state() == BossRunState::kTimedOut) {
    OpenNotice(kBossNotice, {"Out of time!"}, /*refusal=*/false, "Continue");
    return;
  }
  // Nothing to show when leaving after an abort: the player asked to leave, and
  // telling them they left isn't news.
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

// Escape works from either button: backing out of the card is the same as
// Continue.
bool TuiController::OnBossClearEvent(ftxui::Event event) {
  if (event == ftxui::Event::ArrowLeft || event == ftxui::Event::ArrowRight) {
    boss_clear_on_analysis_ = event == ftxui::Event::ArrowRight;
    return true;
  }
  if (boss_clear_on_analysis_ && IsForward(event)) {
    std::vector<PlayerBreakdown> players = boss_run_->breakdowns();
    players[0].name = state_.character.username();
    boss_analysis_panel_.Open(std::move(players),
                              boss_run_->breakdown().seconds());
    screen_ = kBossAnalysis;
    return true;
  }
  if (boss_clear_prompt_.OnEvent(event)) {
    LeaveBossRun();
  }
  return true;
}

// Back to the card it came from, with the cursor still on [Analysis].
bool TuiController::OnBossAnalysisEvent(ftxui::Event event) {
  if (IsBack(event)) {
    screen_ = kBossClear;
    return true;
  }
  boss_analysis_panel_.OnEvent(event);
  return true;
}

void TuiController::LeaveBossRun() {
  DropBossRun();
  screen_ = kBossSelect;
}

// The box a menu entry opened. It is modal, like every other menu over the main
// view: nothing behind it receives keys while it is open.
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
  // Left and Right still belong to the menu row underneath. Moving off the
  // entry the box belongs to closes the box.
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
    // It opens a screen directly from the menu, so there is nothing to open
    // here.
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
        case SettingsEntry::kJukebox:
          jukebox_panel_.Reset();
          screen_ = kJukebox;
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
  // The box stays open on Start and Stop: the entry just changed to the other
  // one, and the player can see that where they are.
  if (analysis_.stops_on_press()) {
    analysis_.Stop();
  } else {
    analysis_.Start();
  }
}

void TuiController::LeaveKeybinds() {
  // Back to the box it was opened from, which is still open where the player
  // left it.
  screen_ = kMenuBox;
}

void TuiController::LeaveOptions() {
  screen_ = kMenuBox;
}

void TuiController::LeaveJukebox() {
  screen_ = kMenuBox;
}

// Music started on this screen keeps playing after it closes, so there is
// nothing to confirm: Escape is the only way out, and it closes an open mode
// box first.
bool TuiController::OnJukeboxEvent(ftxui::Event event) {
  if (IsSwitchPanel(event)) {
    jukebox_panel_.SwitchHalf();
    return true;
  }
  if (event == ftxui::Event::ArrowUp) {
    jukebox_panel_.MoveRow(-1);
    return true;
  }
  if (event == ftxui::Event::ArrowDown) {
    jukebox_panel_.MoveRow(1);
    return true;
  }
  if (event == ftxui::Event::ArrowLeft) {
    jukebox_panel_.MoveColumn(-1);
    return true;
  }
  if (event == ftxui::Event::ArrowRight) {
    jukebox_panel_.MoveColumn(1);
    return true;
  }
  if (IsForward(event)) {
    jukebox_panel_.Activate();
    return true;
  }
  if (IsBack(event) && !jukebox_panel_.DismissedBox()) {
    LeaveJukebox();
  }
  return true;
}

// Every switch takes effect immediately, so there is nothing to confirm or
// undo: Escape and Close do the same thing.
bool TuiController::OnOptionsEvent(ftxui::Event event) {
  if (event == ftxui::Event::ArrowUp) {
    options_panel_.MoveRow(-1);
    return true;
  }
  if (event == ftxui::Event::ArrowDown) {
    options_panel_.MoveRow(1);
    return true;
  }
  // Left and Right change a volume. Holding one repeats, using the terminal's
  // own key repeat rather than anything counted here.
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
    // The Autoswap switch is one of them, and the character reads it.
    state_.MirrorAccount();
    return true;
  }
  if (IsBack(event)) {
    LeaveOptions();
    return true;
  }
  return true;
}

// The Battle Analysis overlay only reads the tool, so the only keys it handles
// are ones that mean "back".
bool TuiController::OnAnalysisEvent(ftxui::Event event) {
  if (IsBack(event) || IsForward(event)) {
    screen_ = kMenuBox;
  }
  return true;
}

void TuiController::TakeCapturedKey(const ftxui::Event& key) {
  // The ticker sends a redraw event several times a second, and a mouse can
  // move over the terminal. Neither is a key press.
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
    // Escape clears the key under the cursor. If there is nothing to clear (an
    // empty slot, or the Close button), Escape leaves instead.
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
  // Swallow everything else, since this is a modal screen.
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
      // Priced in whatever the shelf it came from uses: the token it names, or
      // meso if it names none.
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
// whether to buy, so the list is where they were going next anyway.
bool TuiController::OnShopInspectEvent(ftxui::Event event) {
  return OnCardEvent(event, inspect_panel_, kShop);
}

// The shelf has one row per sale, so a row offers exactly that sale's amount,
// and an equip row is always one item. It is priced at what the sale paid,
// which is the only price the row has.
void TuiController::OpenBuyBackDialog(const BuyBackEntry& entry) {
  buy_back_row_ = shop_panel_.selected_row();
  if (entry.has_equip()) {
    buy_item_ = entry.equip().equip_name();
    // One item, so the maximum is also one: the row is the sale, and there is
    // no second copy behind it.
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

// Buys what the confirmed dialog agreed to, on whichever shelf it was opened
// on. The selection is read again and checked against what the dialog was
// opened on, so a cursor that moved can't buy something never chosen.
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
  // Read once: both return true only on the frame the player pressed, and
  // reading twice would lose the answer.
  if (buy_choice == ConfirmChoice::kConfirmed) {
    BuyWhatTheDialogAgreedTo();
    // Back to the shop rather than the bag: a player buying one thing often
    // buys another.
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
