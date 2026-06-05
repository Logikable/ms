#include "src/frontend/tui_controller.h"

#include "ftxui/component/event.hpp"
#include "src/character.h"
#include "src/equip_instance.h"
#include "src/frontend/bag_panel.h"
#include "src/frontend/equipped_panel.h"
#include "src/frontend/scroll_panel.h"
#include "src/frontend/tui_types.h"
#include "src/game_state.h"
#include "src/protos/equip.pb.h"
#include "src/protos/scroll.pb.h"

namespace ms {

TuiController::TuiController(GameState& state, EquippedPanel& equip_panel,
                             BagPanel& bag_panel, ScrollPanel& scroll_panel,
                             int& panel_focus)
    : state_(state),
      equip_panel_(equip_panel),
      bag_panel_(bag_panel),
      scroll_panel_(scroll_panel),
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

bool TuiController::OnEvent(ftxui::Event event) {
  if (screen_ == kItemMenu) {
    Screen next =
        panel_focus_ == kEquipPanel
            ? equip_panel_.OnMenuEvent(event, panel_focus_, scroll_panel_)
            : bag_panel_.OnMenuEvent(event, panel_focus_, scroll_panel_);
    if (next == kScrollSelect) {
      if (panel_focus_ == kEquipPanel) {
        scroll_slot_ = equip_panel_.selected_slot();
      } else {
        scroll_index_ = bag_panel_.selected();
      }
    }
    screen_ = next;
    return true;
  }
  if (screen_ == kScrollSelect) {
    if (event == ftxui::Event::Escape) {
      screen_ = kItemMenu;
      return true;
    }
    if (event == ftxui::Event::Return) {
      const EquipInstance* item =
          panel_focus_ == kEquipPanel
              ? &state_.character.equipped().at(scroll_slot_)
              : &state_.character.inventory()[scroll_index_];
      std::string equip_name = item->prototype().name();
      int slots_remaining = item->proto().remaining_upgrade_slots();
      if (slots_remaining == 0) {
        scroll_result_ = {kScrollNoSlots, equip_name, "", 0};
      } else {
        const Scroll& scroll = scroll_panel_.selected_scroll();
        bool success;
        if (panel_focus_ == kEquipPanel) {
          success = state_.character.ScrollEquipped(scroll_slot_, scroll);
          slots_remaining = state_.character.equipped()
                                .at(scroll_slot_)
                                .proto()
                                .remaining_upgrade_slots();
        } else {
          success = state_.character.ScrollInventory(scroll_index_, scroll);
          slots_remaining = state_.character.inventory()[scroll_index_]
                                .proto()
                                .remaining_upgrade_slots();
        }
        scroll_result_ = {success ? kScrollSuccess : kScrollFail, equip_name,
                          scroll.name(), slots_remaining};
      }
      screen_ = kScrollResult;
      return true;
    }
    return false;  // caller forwards navigation events to scroll panel
  }
  if (screen_ == kScrollResult) {
    if (event == ftxui::Event::Escape || event == ftxui::Event::Return) {
      screen_ = kScrollSelect;
    }
    return true;
  }
  if (event == ftxui::Event::Tab) {
    panel_focus_ = panel_focus_ == kEquipPanel ? kBagPanel : kEquipPanel;
    return true;
  }
  return false;
}

}  // namespace ms
