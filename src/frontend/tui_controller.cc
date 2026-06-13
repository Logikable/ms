#include "src/frontend/tui_controller.h"

#include "ftxui/component/event.hpp"
#include "src/character.h"
#include "src/equip_instance.h"
#include "src/frontend/ap_alloc_panel.h"
#include "src/frontend/bag_panel.h"
#include "src/frontend/equipped_panel.h"
#include "src/frontend/scroll_panel.h"
#include "src/frontend/types.h"
#include "src/game_state.h"
#include "src/protos/equip.pb.h"
#include "src/protos/scroll.pb.h"

namespace ms {

TuiController::TuiController(GameState& state, EquippedPanel& equip_panel,
                             BagPanel& bag_panel, ScrollPanel& scroll_panel,
                             ApAllocPanel& ap_alloc_panel, int& panel_focus)
    : state_(state),
      equip_panel_(equip_panel),
      bag_panel_(bag_panel),
      scroll_panel_(scroll_panel),
      ap_alloc_panel_(ap_alloc_panel),
      panel_focus_(panel_focus) {
}

void TuiController::OpenEquipMenu() {
  screen_ = kItemMenu;
  equip_panel_.OpenMenu();
}

void TuiController::OpenBagMenu() {
  screen_ = kItemMenu;
  bag_panel_.OpenMenu();
}

void TuiController::OpenApAlloc() {
  screen_ = kApAlloc;
  ap_alloc_panel_.Reset();
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
          : bag_panel_.OnMenuEvent(event, panel_focus_, scroll_panel_);
  if (next == kInspect) {
    if (panel_focus_ == kEquipPanel) {
      inspect_slot_ = equip_panel_.selected_slot();
    } else {
      inspect_index_ = bag_panel_.selected();
    }
  }
  if (next == kScrollSelect) {
    if (panel_focus_ == kEquipPanel) {
      scroll_slot_ = equip_panel_.selected_slot();
    } else {
      scroll_index_ = bag_panel_.selected();
    }
  }
  if (next == kStarForce) {
    if (panel_focus_ == kEquipPanel) {
      star_force_slot_ = equip_panel_.selected_slot();
    } else {
      star_force_index_ = bag_panel_.selected();
    }
  }
  screen_ = next;
  return true;
}

bool TuiController::OnInspectEvent(ftxui::Event event) {
  if (event == ftxui::Event::Escape || event == ftxui::Event::Return) {
    screen_ = kMain;
  }
  return true;
}

bool TuiController::OnScrollSelectEvent(ftxui::Event event) {
  if (event == ftxui::Event::Escape) {
    screen_ = kItemMenu;
    return true;
  }
  if (event == ftxui::Event::Return) {
    const EquipInstance* item =
        panel_focus_ == kEquipPanel
            ? &state_.character.equipped().at(scroll_slot_)
            : state_.character.inventory().equip_instance(scroll_index_);
    std::string equip_name = item->prototype().name();
    const Scroll& scroll = scroll_panel_.selected_scroll();
    ScrollOutcome outcome;
    int slots_remaining;
    if (panel_focus_ == kEquipPanel) {
      outcome = state_.character.ScrollEquipped(scroll_slot_, scroll);
      slots_remaining = state_.character.equipped()
                            .at(scroll_slot_)
                            .equip_state()
                            .remaining_upgrade_slots();
    } else {
      outcome = state_.character.ScrollInventory(scroll_index_, scroll);
      const EquipInstance* inv_item =
          state_.character.inventory().equip_instance(scroll_index_);
      slots_remaining = inv_item != nullptr
                            ? inv_item->equip_state().remaining_upgrade_slots()
                            : 0;
    }
    scroll_result_ = {outcome, equip_name, scroll.name(), slots_remaining};
    screen_ = kScrollResult;
    return true;
  }
  return false;  // caller forwards navigation events to scroll panel
}

bool TuiController::OnScrollResultEvent(ftxui::Event event) {
  if (event == ftxui::Event::Escape || event == ftxui::Event::Return) {
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
  if (event == ftxui::Event::Escape) {
    screen_ = kMain;
    return true;
  }
  if (event == ftxui::Event::Return) {
    const EquipInstance* item = star_force_item();
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
    return true;
  }
  return true;
}

bool TuiController::OnStarForceResultEvent(ftxui::Event event) {
  if (event == ftxui::Event::Escape || event == ftxui::Event::Return) {
    screen_ =
        star_force_result_.outcome == kStarForceDestroy ? kMain : kStarForce;
  }
  return true;
}

}  // namespace ms
