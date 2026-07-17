#include "src/frontend/tui_controller.h"

#include <string>

#include "ftxui/component/event.hpp"
#include "src/character.h"
#include "src/equip_instance.h"
#include "src/frontend/ap_alloc_panel.h"
#include "src/frontend/equipped_panel.h"
#include "src/frontend/inventory_panel.h"
#include "src/frontend/map_select_panel.h"
#include "src/frontend/panel_util.h"
#include "src/frontend/scroll_panel.h"
#include "src/frontend/star_force_panel.h"
#include "src/frontend/trace_recover_panel.h"
#include "src/frontend/types.h"
#include "src/game_state.h"
#include "src/protos/equip.pb.h"
#include "src/protos/scroll.pb.h"

namespace ms {

TuiController::TuiController(GameState& state, EquippedPanel& equip_panel,
                             InventoryPanel& inventory_panel,
                             ScrollPanel& scroll_panel,
                             ApAllocPanel& ap_alloc_panel,
                             StarForcePanel& star_force_panel,
                             TraceRecoverPanel& trace_recover_panel,
                             SellPanel& sell_panel,
                             MapSelectPanel& map_select_panel, int& panel_focus)
    : state_(state),
      equip_panel_(equip_panel),
      inventory_panel_(inventory_panel),
      scroll_panel_(scroll_panel),
      ap_alloc_panel_(ap_alloc_panel),
      star_force_panel_(star_force_panel),
      trace_recover_panel_(trace_recover_panel),
      sell_panel_(sell_panel),
      map_select_panel_(map_select_panel),
      panel_focus_(panel_focus) {
}

void TuiController::OpenEquipMenu() {
  screen_ = kItemMenu;
  equip_panel_.OpenMenu();
}

void TuiController::OpenInventoryMenu() {
  screen_ = kItemMenu;
  inventory_panel_.OpenMenu();
}

void TuiController::OpenApAlloc() {
  screen_ = kApAlloc;
  ap_alloc_panel_.Reset();
}

void TuiController::OpenMapSelect() {
  screen_ = kMapSelect;
  map_select_panel_.Reset();
}

const EquipTabItem* TuiController::inspect_item() const {
  if (screen_ != kInspect) {
    return nullptr;
  }
  if (panel_focus_ == kEquipPanel) {
    return &state_.character.equipped().at(inspect_slot_);
  }
  return &state_.character.inventory()[inspect_index_];
}

const EquipInstance* TuiController::scroll_item() const {
  if (screen_ != kScrollSelect && screen_ != kScrollResult) {
    return nullptr;
  }
  if (panel_focus_ == kEquipPanel) {
    return &state_.character.equipped().at(scroll_slot_);
  }
  return state_.character.inventory().equip_instance(scroll_index_);
}

bool TuiController::OnEvent(ftxui::Event event) {
  if (screen_ == kItemMenu) {
    return OnItemMenuEvent(event);
  }
  if (screen_ == kInspect) {
    return OnInspectEvent(event);
  }
  if (screen_ == kScrollSelect) {
    return OnScrollSelectEvent(event);
  }
  if (screen_ == kScrollResult) {
    return OnScrollResultEvent(event);
  }
  if (screen_ == kApAlloc) {
    return OnApAllocEvent(event);
  }
  if (screen_ == kStarForce) {
    return OnStarForceEvent(event);
  }
  if (screen_ == kStarForceResult) {
    return OnStarForceResultEvent(event);
  }
  if (screen_ == kTraceRecover) {
    return OnTraceRecoverEvent(event);
  }
  if (screen_ == kTraceRecoverResult) {
    return OnTraceRecoverResultEvent(event);
  }
  if (screen_ == kSell) {
    return OnSellEvent(event);
  }
  if (screen_ == kMapSelect) {
    return OnMapSelectEvent(event);
  }
  if (event == ftxui::Event::Tab) {
    panel_focus_ = (panel_focus_ + 1) % kNumPanels;
    if (panel_focus_ == kCharPanel && state_.character.proto().ap() == 0) {
      panel_focus_ = (panel_focus_ + 1) % kNumPanels;
    }
    return true;
  }
  return false;
}

bool TuiController::OnItemMenuEvent(ftxui::Event event) {
  Screen next =
      panel_focus_ == kEquipPanel
          ? equip_panel_.OnMenuEvent(event, panel_focus_, scroll_panel_)
          : inventory_panel_.OnMenuEvent(event, panel_focus_, scroll_panel_);
  if (next == kInspect) {
    if (panel_focus_ == kEquipPanel) {
      inspect_slot_ = equip_panel_.selected_slot();
    } else {
      inspect_index_ = inventory_panel_.selected();
    }
  }
  if (next == kScrollSelect) {
    if (panel_focus_ == kEquipPanel) {
      scroll_slot_ = equip_panel_.selected_slot();
    } else {
      scroll_index_ = inventory_panel_.selected();
    }
  }
  if (next == kStarForce) {
    if (panel_focus_ == kEquipPanel) {
      star_force_slot_ = equip_panel_.selected_slot();
    } else {
      star_force_index_ = inventory_panel_.selected();
    }
    star_force_panel_.ResetConfirm();
  }
  if (next == kTraceRecover) {
    trace_index_ = inventory_panel_.selected();
    trace_recover_panel_.SetTrace(&state_.character.inventory()[trace_index_]);
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
  if (IsBack(event) && !scroll_panel_.IsConfirming()) {
    if (panel_focus_ == kEquipPanel) {
      equip_panel_.OpenMenu();
    } else {
      inventory_panel_.OpenMenu();
    }
    screen_ = kItemMenu;
    return true;
  }
  if (IsForward(event) && !scroll_panel_.IsConfirming()) {
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
      screen_ = kScrollResult;
      return true;
    }
  }
  scroll_panel_.OnEvent(event);
  if (scroll_panel_.TakeConfirmed()) {
    const EquipInstance* item =
        panel_focus_ == kEquipPanel
            ? &state_.character.equipped().at(scroll_slot_)
            : state_.character.inventory().equip_instance(scroll_index_);
    std::string equip_name = item->prototype().name();
    const Scroll& scroll = scroll_panel_.selected_scroll();
    ScrollOutcome outcome;
    if (panel_focus_ == kEquipPanel) {
      outcome = state_.character.ScrollEquipped(scroll_slot_, scroll);
    } else {
      outcome = state_.character.ScrollInventory(scroll_index_, scroll);
    }
    int slots_remaining =
        item ? item->equip_state().remaining_upgrade_slots() : 0;
    scroll_result_ = {outcome, equip_name, scroll.name(), slots_remaining,
                      scroll.scroll_category()};
    screen_ = kScrollResult;
  }
  return true;
}

bool TuiController::OnScrollResultEvent(ftxui::Event event) {
  if (IsBack(event) || IsForward(event)) {
    screen_ = kScrollSelect;
  }
  return true;
}

bool TuiController::OnApAllocEvent(ftxui::Event event) {
  screen_ = ap_alloc_panel_.OnEvent(event);
  return true;
}

const EquipInstance* TuiController::star_force_item() const {
  if (screen_ != kStarForce) {
    return nullptr;
  }
  if (panel_focus_ == kEquipPanel) {
    return &state_.character.equipped().at(star_force_slot_);
  }
  return state_.character.inventory().equip_instance(star_force_index_);
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
  if (star_force_panel_.TakeConfirmed()) {
    std::string equip_name = item->prototype().name();
    int stars_before = item->stars();
    StarForceOutcome outcome;
    if (panel_focus_ == kEquipPanel) {
      outcome = state_.character.StarForceEquipped(star_force_slot_);
    } else {
      outcome = state_.character.StarForceInventory(star_force_index_);
    }
    int stars_after = stars_before + (outcome == kStarForceSuccess ? 1 : 0);
    star_force_result_ = {outcome, equip_name, stars_before, stars_after};
    screen_ = kStarForceResult;
  }
  return true;
}

bool TuiController::OnStarForceResultEvent(ftxui::Event event) {
  if (IsBack(event) || IsForward(event)) {
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
    screen_ = kTraceRecoverResult;
  }
  return true;
}

bool TuiController::OnTraceRecoverResultEvent(ftxui::Event event) {
  if (IsBack(event) || IsForward(event)) {
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
  if (event == ftxui::Event::ArrowLeft) {
    map_select_panel_.ChangePage(-1);
    return true;
  }
  if (event == ftxui::Event::ArrowRight) {
    map_select_panel_.ChangePage(1);
    return true;
  }
  if (IsForward(event)) {
    // Travel is free, so the highlighted map is always a legal destination.
    // The fight restarts on its own once it sees the new map.
    std::string map = map_select_panel_.selected_map();
    if (!map.empty()) {
      state_.current_map = map;
    }
    screen_ = kMain;
    return true;
  }
  if (IsBack(event)) {
    screen_ = kMain;
    return true;
  }
  // Swallow everything else: this is a modal screen.
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

}  // namespace ms
