#include "src/frontend/tui_controller.h"

#include <map>
#include <set>
#include <string>
#include <vector>

#include "ftxui/component/event.hpp"
#include "src/character.h"
#include "src/frontend/bag_panel.h"
#include "src/frontend/equipped_panel.h"
#include "src/frontend/item_menu.h"
#include "src/frontend/scroll_panel.h"
#include "src/game_state.h"
#include "src/protos/equip.pb.h"
#include "src/protos/scroll.pb.h"

namespace ms {

namespace {

ScrollTier TierForLevel(int required_level) {
  if (required_level >= 115) {
    return SCROLL_TIER_3;
  }
  if (required_level >= 75) {
    return SCROLL_TIER_2;
  }
  return SCROLL_TIER_1;
}

}  // namespace

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

std::vector<const Scroll*> TuiController::FilterScrolls(
    const EquipPrototype& proto, const std::map<std::string, Scroll>& scrolls) {
  ScrollTier item_tier = TierForLevel(proto.required_level());
  std::set<int> item_cats(proto.equip_job_categories().begin(),
                          proto.equip_job_categories().end());
  std::vector<const Scroll*> result;
  for (const std::pair<const std::string, Scroll>& kv : scrolls) {
    if (kv.second.tier() != item_tier) {
      continue;
    }
    for (int scroll_cat : kv.second.applicable_job_categories()) {
      if (item_cats.count(scroll_cat)) {
        result.push_back(&kv.second);
        break;
      }
    }
  }
  return result;
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
        std::vector<const Scroll*> filtered = FilterScrolls(
            state_.character.equipped().at(scroll_slot_).prototype(),
            state_.scrolls);
        if (!filtered.empty()) {
          scroll_panel_.SetFilter(std::move(filtered));
          screen_ = kScrollSelect;
        }
      } else if (panel_focus_ == kBagPanel &&
                 active_menu_->selected() == kMenuScroll) {
        scroll_index_ = bag_panel_.selected();
        std::vector<const Scroll*> filtered = FilterScrolls(
            state_.character.inventory()[scroll_index_].prototype(),
            state_.scrolls);
        if (!filtered.empty()) {
          scroll_panel_.SetFilter(std::move(filtered));
          screen_ = kScrollSelect;
        }
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
      std::string equip_name;
      int slots_remaining;
      const EquipInstance* item = nullptr;
      if (panel_focus_ == kEquipPanel) {
        item = &state_.character.equipped().at(scroll_slot_);
      } else {
        item = &state_.character.inventory()[scroll_index_];
      }
      equip_name = item->prototype().name();
      slots_remaining = item->proto().remaining_upgrade_slots();
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
    return false;  // caller forwards navigation events to scroll_component_
  }
  if (screen_ == kScrollResult) {
    if (event == ftxui::Event::Escape || event == ftxui::Event::Return) {
      screen_ = kScrollSelect;
    }
    return true;
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
