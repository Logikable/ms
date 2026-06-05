#include "src/frontend/tui_controller.h"

#include "ftxui/component/event.hpp"
#include "src/character.h"
#include "src/frontend/bag_panel.h"
#include "src/frontend/equipped_panel.h"
#include "src/frontend/item_menu.h"
#include "src/frontend/scroll_panel.h"
#include "src/game_state.h"
#include "src/protos/equip.pb.h"

namespace ms {

TuiController::TuiController(GameState& state, EquippedPanel& equip_panel,
                             BagPanel& bag_panel, ScrollPanel& scroll_panel,
                             int& panel_focus)
    : state_(state),
      equip_panel_(equip_panel),
      bag_panel_(bag_panel),
      scroll_panel_(scroll_panel),
      panel_focus_(panel_focus),
      equip_menu_({"Unequip", "Inspect", "Scroll"}),
      bag_menu_({"Equip", "Inspect", "Scroll"}),
      active_menu_(&equip_menu_) {
}

void TuiController::OpenEquipMenu() {
  screen_ = kItemMenu;
  active_menu_ = &equip_menu_;
  equip_menu_.Reset();
}

void TuiController::OpenBagMenu() {
  screen_ = kItemMenu;
  active_menu_ = &bag_menu_;
  bag_menu_.Reset();
}

bool TuiController::OnEvent(ftxui::Event event) {
  if (screen_ == kItemMenu) {
    if (event == ftxui::Event::Escape) {
      screen_ = kMain;
      return true;
    }
    if (event == ftxui::Event::ArrowUp) {
      active_menu_->Up();
      return true;
    }
    if (event == ftxui::Event::ArrowDown) {
      active_menu_->Down();
      return true;
    }
    if (event == ftxui::Event::Return) {
      if (panel_focus_ == kEquipPanel &&
          active_menu_->selected() == kMenuAction) {
        state_.character.Unequip(equip_panel_.selected_slot());
        if (state_.character.equipped().empty()) {
          panel_focus_ = kBagPanel;
        }
        screen_ = kMain;
      } else if (panel_focus_ == kBagPanel &&
                 active_menu_->selected() == kMenuAction) {
        state_.character.Equip(bag_panel_.selected());
        if (state_.character.inventory().empty()) {
          panel_focus_ = kEquipPanel;
        }
        screen_ = kMain;
      } else if (panel_focus_ == kEquipPanel &&
                 active_menu_->selected() == kMenuScroll) {
        scroll_slot_ = equip_panel_.selected_slot();
        screen_ = kScrollSelect;
      } else if (panel_focus_ == kBagPanel &&
                 active_menu_->selected() == kMenuScroll) {
        scroll_index_ = bag_panel_.selected();
        screen_ = kScrollSelect;
      } else {
        screen_ = kMain;
      }
      return true;
    }
    return true;
  }
  if (screen_ == kScrollSelect) {
    if (event == ftxui::Event::Escape) {
      screen_ = kItemMenu;
      return true;
    }
    if (event == ftxui::Event::Return) {
      if (panel_focus_ == kEquipPanel) {
        state_.character.ScrollEquipped(scroll_slot_,
                                        scroll_panel_.selected_scroll());
      } else {
        state_.character.ScrollInventory(scroll_index_,
                                         scroll_panel_.selected_scroll());
      }
      screen_ = kMain;
      return true;
    }
    return false;  // caller forwards navigation events to scroll_component_
  }
  if (event == ftxui::Event::Tab) {
    if (panel_focus_ == kEquipPanel) {
      panel_focus_ = kBagPanel;
    } else {
      panel_focus_ = kEquipPanel;
    }
    return true;
  }
  return false;
}

}  // namespace ms
