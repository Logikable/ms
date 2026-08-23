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
#include "src/character/character.h"
#include "src/character/character_stats.h"
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
#include "src/frontend/widgets/panel_util.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/item/item.h"
#include "src/protos/boss.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/scroll.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {

TuiController::TuiController(
    GameState& state, CharacterPanel& char_panel, EquippedPanel& equip_panel,
    InventoryPanel& inventory_panel, ScrollPanel& scroll_panel,
    StarForcePanel& star_force_panel, TraceRecoverPanel& trace_recover_panel,
    SellPanel& sell_panel, SellEquipPanel& sell_equip_panel,
    MultiSellPanel& multi_sell_panel, MapSelectPanel& map_select_panel,
    MobInspectPanel& mob_inspect_panel, BossSelectPanel& boss_select_panel,
    ShopPanel& shop_panel, BuyPanel& buy_panel,
    JobInspectPanel& job_inspect_panel, SkillInspectPanel& skill_inspect_panel,
    MenuPanel& menu_panel, KeybindsPanel& keybinds_panel, KeyMap& keys,
    int& panel_focus)
    : state_(state),
      char_panel_(char_panel),
      equip_panel_(equip_panel),
      inventory_panel_(inventory_panel),
      scroll_panel_(scroll_panel),
      star_force_panel_(star_force_panel),
      trace_recover_panel_(trace_recover_panel),
      sell_panel_(sell_panel),
      sell_equip_panel_(sell_equip_panel),
      multi_sell_panel_(multi_sell_panel),
      map_select_panel_(map_select_panel),
      mob_inspect_panel_(mob_inspect_panel),
      boss_select_panel_(boss_select_panel),
      job_inspect_panel_(job_inspect_panel),
      skill_inspect_panel_(skill_inspect_panel),
      menu_panel_(menu_panel),
      keybinds_panel_(keybinds_panel),
      keys_(keys),
      shop_panel_(shop_panel),
      buy_panel_(buy_panel),
      panel_focus_(panel_focus) {
}

void TuiController::OpenEquipMenu() {
  screen_ = kItemMenu;
  equip_panel_.OpenMenu();
}

void TuiController::OpenInventoryMenu() {
  if (inventory_panel_.on_shop_tab()) {
    shop_panel_.Reset();
    screen_ = kShop;
    return;
  }
  screen_ = kItemMenu;
  inventory_panel_.OpenMenu();
}

void TuiController::OpenApAllocate(StatField field) {
  ap_field_ = field;
  ap_selector_.Reset(state_.character.proto().ap());
  screen_ = kApAlloc;
}

void TuiController::OpenSkillLearn(const Skill& skill) {
  skill_learn_ = skill;
  int room = skill.max_level() - state_.character.skill_level(skill);
  int stage = StageForAdvancement(skill.job_advancement());
  sp_selector_.Reset(std::min(state_.character.sp(stage), room));
  screen_ = kSkillLearn;
}

void TuiController::OpenSkillInspect(const Skill& skill) {
  skill_inspect_ = skill;
  skill_inspect_panel_.ResetScroll();
  screen_ = kSkillInspect;
}

// Both read live rather than captured, so a point spent on the skill and then
// inspected again shows the level it is actually at. The learned level: what
// the card makes of the lent ones is the card's business, since it also has
// to say what one more point would buy.
int TuiController::skill_inspect_level() const {
  return state_.character.skill_level(skill_inspect_);
}

int TuiController::skill_inspect_bonus() const {
  return BonusSkillLevels(state_.character, state_.skills);
}

void TuiController::OpenAllStats() {
  screen_ = kAllStats;
}

void TuiController::OpenJobMenu(Job job) {
  job_advance_ = job;
  job_menu_.Reset();
  screen_ = kJobMenu;
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
  if (entry == MenuEntry::kSettings) {
    // The box opens with the cursor still on the entry below it, which is what
    // the player presses Up to leave.
    menu_panel_.OpenSettings();
    screen_ = kSettingsMenu;
    return;
  }
  // Opening the screen is what the gold was leading to, so it stops here.
  state_.character.MarkTabSeen(MenuPanel::boss_seen_key());
  screen_ = kBossSelect;
  boss_select_panel_.Reset();
}

ItemRef TuiController::SelectedItem() const {
  if (panel_focus_ == kEquipPanel) {
    return ItemRef::Equipped(equip_panel_.selected_slot());
  }
  return ItemRef::InBag(inventory_panel_.selected());
}

const EquipTabItem* TuiController::inspect_item() const {
  if (screen_ != kInspect) {
    return nullptr;
  }
  return inspect_ref_.Get(state_.character);
}

const ItemPrototype* TuiController::item_inspect_item() const {
  if (screen_ != kItemInspect) {
    return nullptr;
  }
  const std::vector<StackableItem>& stacks =
      state_.character.stackables(inventory_panel_.active_category());
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
  return scroll_ref_.GetInstance(state_.character);
}

// Keys on the main view, once every screen above it has had its say. A back
// key here means leaving the game, there being nothing left to back out of.
bool TuiController::OnMainViewEvent(ftxui::Event event) {
  if (IsBack(event)) {
    // Opened on Cancel: leaving is not what an accidental Escape means, and a
    // stray Enter behind one should not end the session.
    quit_prompt_.Open(/*cancel_selected=*/true);
    screen_ = kQuit;
    return true;
  }
  if (event != ftxui::Event::Tab && event != ftxui::Event::TabReverse) {
    return false;
  }
  // Round the panels until the next one actually on screen. The character
  // panel always is, so this always lands somewhere. Backwards is a step of
  // kNumPanels - 1 rather than -1, so the modulo is never handed a negative
  // and the two directions are one piece of code.
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
    // Both are screens with nothing to do but read them, so they close the
    // same way.
    case kSkillInspect:
    case kAllStats:
      return OnSkillInspectEvent(event);
    case kJobMenu:
      return OnJobMenuEvent(event);
    case kJobInspect:
      return OnJobInspectEvent(event);
    case kJobAdvance:
      return OnJobAdvanceEvent(event);
    case kStarForce:
      return OnStarForceEvent(event);
    case kStarForceResult:
      return OnStarForceResultEvent(event);
    case kTraceRecover:
      return OnTraceRecoverEvent(event);
    case kTraceRecoverResult:
      return OnTraceRecoverResultEvent(event);
    case kSell:
      return OnSellEvent(event);
    case kSellEquip:
      return OnSellEquipEvent(event);
    case kMultiSell:
      return OnMultiSellEvent(event);
    case kMapSelect:
      return OnMapSelectEvent(event);
    case kMapMenu:
      return OnMapMenuEvent(event);
    case kMobInspect:
      return OnMobInspectEvent(event);
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
    case kSettingsMenu:
      return OnSettingsMenuEvent(event);
    case kKeybinds:
      return OnKeybindsEvent(event);
    case kQuit:
      return OnQuitEvent(event);
    case kMain:
      break;
  }
  return OnMainViewEvent(event);
}

bool TuiController::PanelVisible(int panel) const {
  if (panel == kEquipPanel) {
    return Unlocked(Feature::kEquipped, state_.character);
  }
  if (panel == kInventoryPanel) {
    return Unlocked(Feature::kBag, state_.character);
  }
  if (panel == kMenuPanel) {
    return Unlocked(Feature::kMenu, state_.character);
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

bool TuiController::OnItemMenuEvent(ftxui::Event event) {
  Screen next;
  if (panel_focus_ == kEquipPanel) {
    next = equip_panel_.OnMenuEvent(event, scroll_panel_);
  } else {
    next = inventory_panel_.OnMenuEvent(event, scroll_panel_);
  }
  if (next == kInspect) {
    inspect_ref_ = SelectedItem();
  }
  if (next == kScrollSelect) {
    scroll_ref_ = SelectedItem();
  }
  if (next == kStarForce) {
    star_force_ref_ = SelectedItem();
    star_force_panel_.ResetConfirm();
  }
  if (next == kTraceRecover) {
    trace_index_ = inventory_panel_.selected();
    trace_recover_panel_.SetTrace(&state_.character.inventory()[trace_index_]);
  }
  if (next == kSellEquip) {
    sell_equip_index_ = inventory_panel_.selected();
    const EquipTabItem& item = state_.character.inventory()[sell_equip_index_];
    // A trace pays nothing whatever its prototype says, so the dialog is told
    // what the sale will really hand over rather than what the item cost.
    bool is_trace = state_.character.inventory().equip_instance(
                        sell_equip_index_) == nullptr;
    int price = is_trace ? 0 : item.prototype().sell_price();
    sell_equip_panel_.Reset(item.name(), price);
  }
  if (next == kMultiSell) {
    int tab = inventory_panel_.active_tab();
    multi_sell_panel_.Reset(tab, tab == kEquipTab
                                     ? inventory_panel_.selected()
                                     : inventory_panel_.selected_stack());
  }
  if (next == kSell) {
    sell_category_ = inventory_panel_.active_category();
    sell_index_ = inventory_panel_.selected_stack();
    const StackableItem& stack =
        state_.character.stackables(sell_category_)[sell_index_];
    sell_panel_.Reset(stack.name(), stack.prototype().sell_price(),
                      stack.count());
  }
  screen_ = next;
  return true;
}

bool TuiController::OnInspectEvent(ftxui::Event event) {
  if (IsBack(event) || IsForward(event)) {
    screen_ = kMain;
  }
  return true;
}

bool TuiController::OnScrollSelectEvent(ftxui::Event event) {
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
  scroll_panel_.OnEvent(event);
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
      int slots = item->prototype().upgrade_slots();
      int cap = slots - item->equip_state().scroll_successes();
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
  if (scroll_panel_.TakeConfirmed()) {
    const EquipInstance* item = scroll_ref_.GetInstance(state_.character);
    std::string equip_name = item->prototype().name();
    const Scroll& scroll = scroll_panel_.selected_scroll();
    // Paid for before it is used, and only used if it was paid for. The panel
    // will not confirm what the player cannot afford, so this refusing is a
    // second line rather than the first.
    if (!state_.character.ConsumeStackable(ITEM_CATEGORY_ETC, kSpellTraceName,
                                           scroll_panel_.CostOfSelected())) {
      return true;
    }
    ScrollOutcome outcome = ScrollItem(state_.character, scroll_ref_, scroll);
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
  ap_selector_.OnEvent(event);
  if (ap_selector_.TakeConfirmed()) {
    state_.character.AllocateStat(ap_field_, ap_selector_.value());
    screen_ = kMain;
  } else if (ap_selector_.TakeCancelled()) {
    screen_ = kMain;
  }
  return true;
}

bool TuiController::OnSkillLearnEvent(ftxui::Event event) {
  sp_selector_.OnEvent(event);
  if (sp_selector_.TakeConfirmed()) {
    state_.character.LearnSkill(skill_learn_, sp_selector_.value());
    screen_ = kMain;
  } else if (sp_selector_.TakeCancelled()) {
    screen_ = kMain;
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
    screen_ = kMain;
  }
  return true;
}

bool TuiController::OnQuitEvent(ftxui::Event event) {
  ConfirmChoice choice = quit_prompt_.OnEvent(event);
  if (choice == ConfirmChoice::kConfirmed) {
    // Only raised, never acted on here. Tui owns the ftxui screen and is the
    // only thing that can end its loop.
    quit_requested_ = true;
    screen_ = kMain;
  } else if (choice == ConfirmChoice::kCancelled) {
    screen_ = kMain;
  }
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
      job_inspect_panel_.SetJob(job_advance_);
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
  return star_force_ref_.GetInstance(state_.character);
}

bool TuiController::OnStarForceEvent(ftxui::Event event) {
  if (IsBack(event) && !star_force_panel_.IsConfirming()) {
    screen_ = kMain;
    return true;
  }
  const EquipInstance* item = star_force_item();
  if (item->stars() >= item->max_stars()) {
    return true;
  }
  star_force_panel_.OnEvent(event);
  if (star_force_panel_.TakeCancelled()) {
    screen_ = kMain;
    return true;
  }
  if (star_force_panel_.TakeConfirmed()) {
    std::string equip_name = item->prototype().name();
    int stars_before = item->stars();
    StarForceOutcome outcome = StarForceItem(state_.character, star_force_ref_);
    int stars_after = stars_before + (outcome == kStarForceSuccess ? 1 : 0);
    star_force_result_ = {outcome, equip_name, stars_before, stars_after};
    OpenNotice(kStarForceResult);
  }
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
  return &state_.character.inventory()[trace_index_];
}

bool TuiController::OnTraceRecoverEvent(ftxui::Event event) {
  if (IsBack(event) && !trace_recover_panel_.IsConfirming()) {
    screen_ = kItemMenu;
    return true;
  }
  trace_recover_panel_.OnEvent(event);
  if (trace_recover_panel_.TakeConfirmed()) {
    int base_index = trace_recover_panel_.selected_index();
    std::string equip_name =
        state_.character.inventory()[trace_index_].prototype().name();
    int stars_recovered =
        state_.character.RecoverTrace(trace_index_, base_index);
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

bool TuiController::OnBossSelectEvent(ftxui::Event event) {
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
    if (boss_select_panel_.selected() == nullptr) {
      return true;
    }
    boss_prompt_title_ = boss_select_panel_.selected_title();
    // Four ways a fight is not offered, and each says why rather than doing
    // nothing: a fight that is not built yet, a character with nothing to
    // swing, a fight they have not levelled up to, and one still on its reset.
    // The unbuilt one leads, because none of the others is the reason. Only
    // what is left is worth asking a question about.
    if (boss_select_panel_.selected_coming_soon()) {
      OpenNotice(kBossNotice, {boss_prompt_title_, "is coming soon!"},
                 /*refusal=*/true);
    } else if (EquippedWeapon(state_) == nullptr) {
      OpenNotice(kBossNotice, {"You have no weapon equipped!"},
                 /*refusal=*/true);
    } else if (!boss_select_panel_.selected_unlocked()) {
      OpenNotice(
          kBossNotice,
          {boss_prompt_title_,
           "unlocks at level " +
               std::to_string(boss_select_panel_.selected_unlock_level()) +
               "."},
          /*refusal=*/true);
    } else if (!boss_select_panel_.selected_available()) {
      std::string when =
          boss_select_panel_.selected_reset() == RESET_PERIOD_WEEKLY
              ? "this week"
              : "today";
      OpenNotice(kBossNotice,
                 {boss_prompt_title_, "has already been killed " + when + "."},
                 /*refusal=*/false);
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
  boss_run_ = std::make_unique<BossRun>(
      boss_run_key_, it->second, boss_select_panel_.selected_difficulty());
  screen_ = kBossFight;
  return true;
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
  notice_prompt_.Open();
  screen_ = screen;
}

void TuiController::OpenNotice(Screen screen, std::vector<std::string> lines,
                               bool refusal) {
  notice_lines_ = std::move(lines);
  notice_is_refusal_ = refusal;
  OpenNotice(screen);
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
  if (boss_run_ == nullptr || screen_ != kBossFight) {
    return;
  }
  boss_run_->Advance(state_, elapsed_seconds);
  if (!boss_run_->done()) {
    return;
  }
  if (boss_run_->won()) {
    state_.character.RecordBossClear(boss_run_key_, boss_run_difficulty_,
                                     static_cast<int64_t>(std::time(nullptr)));
    // Copied off the run rather than read back through it: the card is still
    // up when the run goes.
    boss_clear_title_ = boss_run_->title();
    boss_clear_reward_ = boss_run_->reward();
    boss_clear_prompt_.Open();
    screen_ = kBossClear;
    return;
  }
  if (boss_run_->state() == BossRunState::kTimedOut) {
    OpenNotice(kBossNotice, {"Out of time!"}, /*refusal=*/false);
    return;
  }
  // Nothing to dismiss on the way out of an abort: the player asked to leave,
  // and telling them they left is not news.
  boss_run_.reset();
  screen_ = kBossSelect;
}

bool TuiController::OnBossClearEvent(ftxui::Event event) {
  if (boss_clear_prompt_.OnEvent(event)) {
    LeaveBossRun();
  }
  return true;
}

void TuiController::LeaveBossRun() {
  boss_run_.reset();
  screen_ = kBossSelect;
}

// The Settings box. Modal, like every other menu that stands over the main
// view: nothing behind it hears a key while it is up.
bool TuiController::OnSettingsMenuEvent(ftxui::Event event) {
  if (event == ftxui::Event::ArrowUp) {
    menu_panel_.MoveSettingsCursor(1);
    return true;
  }
  if (event == ftxui::Event::ArrowDown) {
    menu_panel_.MoveSettingsCursor(-1);
    return true;
  }
  if (IsBack(event)) {
    menu_panel_.CloseSettings();
    screen_ = kMain;
    return true;
  }
  // Left and Right still belong to the menu row underneath. Walking off the
  // entry the box hangs from puts the box away with it.
  bool sideways =
      event == ftxui::Event::ArrowLeft || event == ftxui::Event::ArrowRight;
  if (sideways && menu_panel_.settings_cursor() < 0) {
    menu_panel_.CloseSettings();
    screen_ = kMain;
    int step = -1;
    if (event == ftxui::Event::ArrowRight) {
      step = 1;
    }
    menu_panel_.MoveCursor(step);
    return true;
  }
  if (IsForward(event) && menu_panel_.settings_cursor() >= 0) {
    switch (menu_panel_.selected_settings_entry()) {
      case SettingsEntry::kKeybinds:
        keybinds_panel_.Reset();
        screen_ = kKeybinds;
        break;
    }
  }
  return true;
}

void TuiController::LeaveKeybinds() {
  // Back to the box it was opened from, which is still standing where the
  // player left it.
  screen_ = kSettingsMenu;
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
      int64_t balance = token == nullptr
                            ? state_.character.meso()
                            : state_.character.CountStackable(*token);
      int price = token == nullptr ? item->shop_price() : item->token_price();
      buy_panel_.Reset(item->name(), price, balance,
                       state_.character.RoomFor(*item),
                       state_.character.CountOwned(*item), token);
    } else {
      buy_item_ = stackable->name();
      buy_panel_.Reset(stackable->name(), stackable->shop_price(),
                       state_.character.meso(),
                       state_.character.RoomFor(*stackable),
                       state_.character.CountStackable(*stackable));
    }
  }
  screen_ = next;
  return true;
}

bool TuiController::OnShopInspectEvent(ftxui::Event event) {
  if (IsBack(event) || IsForward(event)) {
    // Back to the shop rather than the bag: inspecting is how a player decides
    // whether to buy, so the list is where they were going next either way.
    screen_ = kShop;
  }
  // Swallow everything else: this is a modal screen.
  return true;
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
      proto == nullptr ? 0 : state_.character.CountStackable(*proto));
}

// Everything the confirmed dialog buys, whichever shelf it was opened on. The
// selection is re-read rather than remembered, and what it names checked
// against what the dialog was opened on, so a cursor that moved under the
// dialog cannot buy something the player never chose.
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
  buy_panel_.OnEvent(event);
  // Taken once: both of these answer true only on the frame the player
  // pressed, and asking twice throws the answer away.
  if (buy_panel_.TakeConfirmed()) {
    BuyWhatTheDialogAgreedTo();
    // Back to the shop rather than the bag: a player buying one thing is
    // usually buying two.
    screen_ = kShop;
  } else if (buy_panel_.TakeCancelled()) {
    screen_ = kShop;
  }
  return true;
}

bool TuiController::OnSellEvent(ftxui::Event event) {
  sell_panel_.OnEvent(event);
  if (sell_panel_.TakeConfirmed()) {
    state_.character.SellStackable(sell_category_, sell_index_,
                                   sell_panel_.quantity());
    screen_ = kMain;
  } else if (sell_panel_.TakeCancelled()) {
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
    state_.character.SellEquip(sell_equip_index_);
  }
  screen_ = kMain;
  return true;
}

bool TuiController::OnMultiSellEvent(ftxui::Event event) {
  multi_sell_panel_.OnEvent(event);
  if (multi_sell_panel_.TakeConfirmed()) {
    SellBasket(state_.character, multi_sell_panel_.basket());
    screen_ = kMain;
  } else if (multi_sell_panel_.TakeCancelled()) {
    screen_ = kMain;
  }
  return true;
}

}  // namespace ms
