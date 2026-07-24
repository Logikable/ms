#include "src/frontend/equipped_panel.h"

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/equip_instance.h"
#include "src/equip_stats.h"
#include "src/frontend/panel_util.h"
#include "src/frontend/scroll_panel.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"

namespace ms {
namespace {

// Two leading spaces match the "  " / "> " cursor added by the entry transform.
constexpr char kColumnHeader[] =
    "  Name                      "  // 2 cursor + 26 name
    "  Equip Slot"                  // 2 sep + 10 slot
    "  Stats               "        // 2 sep + 20 info (10 main + 10 atk)
    "  Scrolls";                    // 2 sep + label

// Returns the item's main stat value for the given job.
// TODO: Demon Avenger's main stat is HP; Xenon's is STR+DEX+LUK combined.
int MainStatValue(const EquipStats& stats, Job job) {
  switch (job) {
    case JOB_WARRIOR:
    case JOB_BEGINNER:
      return stats.str();
    case JOB_ARCHER:
      return stats.dex();
    default:
      return 0;
  }
}

// Returns the main stat label for the given job, or nullptr for unknown jobs.
// TODO: Demon Avenger's main stat is HP; Xenon's is STR+DEX+LUK combined.
const char* MainStatLabel(Job job) {
  switch (job) {
    case JOB_WARRIOR:
    case JOB_BEGINNER:
      return "STR";
    case JOB_ARCHER:
      return "DEX";
    default:
      return nullptr;
  }
}
// Sub-header row: 64 spaces align "Pass/Left/Restore" under the scroll column.
constexpr char kColumnHeader2[] =
    "                              "  // 30
    "                              "  // 30
    "    "                            // 4 → 64 total
    "Pass/Left/Restore";

}  // namespace

EquippedPanel::EquippedPanel(CharacterInstance& character, int& panel_focus)
    : character_(character),
      panel_focus_(panel_focus),
      menu_({"Unequip", "Inspect", "Scroll", "Star Force", "Close"}) {
}

void EquippedPanel::OpenMenu() {
  menu_.Reset();
  EquipSlot slot = selected_slot();
  if (slot != EQUIP_SLOT_UNSPECIFIED) {
    if (!character_.equipped().at(slot).CanStarForce()) {
      menu_.Disable(kMenuStarForce);
    }
  }
}

Screen EquippedPanel::OnMenuEvent(ftxui::Event event, int& panel_focus,
                                  ScrollPanel& scroll_panel) {
  if (IsBack(event)) {
    return kMain;
  }
  if (event == ftxui::Event::ArrowUp) {
    menu_.Up();
    return kItemMenu;
  }
  if (event == ftxui::Event::ArrowDown) {
    menu_.Down();
    return kItemMenu;
  }
  if (IsForward(event)) {
    if (menu_.selected() == kMenuAction) {
      character_.Unequip(selected_slot());
      if (character_.equipped().empty()) {
        panel_focus = kInventoryPanel;
      }
      return kMain;
    }
    if (menu_.selected() == kMenuInspect) {
      return kInspect;
    }
    if (menu_.selected() == kMenuScroll) {
      if (scroll_panel.SetFilterForPrototype(
              character_.equipped().at(selected_slot()).prototype())) {
        return kScrollSelect;
      }
    }
    if (menu_.selected() == kMenuStarForce) {
      return kStarForce;
    }
    return kMain;
  }
  return kItemMenu;
}

EquipSlot EquippedPanel::selected_slot() const {
  if (selected_ >= static_cast<int>(slots_.size())) {
    return EQUIP_SLOT_UNSPECIFIED;
  }
  return slots_[selected_];
}

ftxui::Component EquippedPanel::MakeComponent(std::function<void()> on_enter) {
  ftxui::MenuOption opt;
  opt.on_enter = [on_enter]() { on_enter(); };
  // Suppress the default color inversion so the caret indicator looks the same
  // whether or not the item menu is open.
  opt.entries_option.transform = [](ftxui::EntryState state) -> ftxui::Element {
    return ftxui::text((state.focused ? "> " : "  ") + state.label);
  };
  ftxui::Component menu = ftxui::Menu(&entries_, &selected_, opt);

  // entries_ and slots_ are rebuilt from equipped() on every render so the
  // display stays in sync with changes made via on_enter.
  ftxui::Component renderer =
      ftxui::Renderer(menu, [this, menu]() -> ftxui::Element {
        bool focused = panel_focus_ == kEquipPanel;
        entries_.clear();
        slots_.clear();
        for (const std::pair<const EquipSlot, EquipInstance>& kv :
             character_.equipped()) {
          slots_.push_back(kv.first);
          const EquipInstance& item = kv.second;
          const EquipStats stats = item.stats();
          Job job = character_.proto().job();
          int main_val = MainStatValue(stats, job);
          const char* main_label = MainStatLabel(job);
          int atk_val = 0;
          const char* atk_label = nullptr;
          if (stats.attack() > 0) {
            atk_val = stats.attack();
            atk_label = "ATT";
          } else if (stats.magic_attack() > 0) {
            atk_val = stats.magic_attack();
            atk_label = "MATT";
          }
          std::string main_str;
          if (main_val > 0 && main_label != nullptr) {
            main_str = "+" + std::to_string(main_val) + " " + main_label;
          }
          std::string atk_str;
          if (atk_val > 0 && atk_label != nullptr) {
            atk_str = "+" + std::to_string(atk_val) + " " + atk_label;
          }
          std::string info = PadRight(main_str, 10) + PadRight(atk_str, 10);
          int scroll_pass = item.equip_state().scroll_successes();
          int scroll_left = item.equip_state().remaining_upgrade_slots();
          int scroll_restore =
              item.prototype().upgrade_slots() - scroll_pass - scroll_left;
          entries_.push_back(FormatItemEntry(item.prototype().name(), kv.first,
                                             info, scroll_pass, scroll_left,
                                             scroll_restore));
        }
        if (!entries_.empty()) {
          selected_ =
              std::min(selected_, static_cast<int>(entries_.size()) - 1);
        }
        if (entries_.empty()) {
          return ThemedWindow(" Equipped ", ftxui::text("(empty)"), focused);
        }
        return ThemedWindow(" Equipped ",
                            ftxui::vbox({
                                ftxui::text(kColumnHeader),
                                ftxui::text(kColumnHeader2),
                                ThemedSeparator(),
                                menu->Render(),
                            }),
                            focused);
      });
  return ftxui::CatchEvent(renderer, [on_enter](ftxui::Event event) {
    if (event == ftxui::Event::Character(' ')) {
      on_enter();
      return true;
    }
    return false;
  });
}

}  // namespace ms
