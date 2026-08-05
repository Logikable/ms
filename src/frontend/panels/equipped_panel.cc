#include "src/frontend/panels/equipped_panel.h"

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/character/progression.h"
#include "src/frontend/screens/scroll_panel.h"
#include "src/frontend/widgets/panel_util.h"
#include "src/item/equip_instance.h"
#include "src/item/equip_stats.h"
#include "src/item/item.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"

namespace ms {
namespace {

// Two leading spaces match the "  " / "> " cursor added by the entry transform.
constexpr char kColumnHeader[] =
    "  Name                      "  // 2 cursor + 26 name
    "  Equip Slot"                  // 2 sep + 10 slot
    "  Stats               "        // 2 sep + 20 info (10 atk + 10 main)
    "  Scrolls";                    // 2 sep + label

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
  // Entries the player has not reached yet are not drawn at all, ahead of any
  // question about this particular item: a character who has just been handed
  // this panel has no bag to unequip into yet, and asking about scrolls means
  // nothing to them for a long while after that.
  if (!Unlocked(Feature::kUnequip, character_)) {
    menu_.Hide(kMenuAction);
  }
  if (!Unlocked(Feature::kScrolling, character_)) {
    menu_.Hide(kMenuScroll);
  }
  if (!Unlocked(Feature::kStarForce, character_)) {
    menu_.Hide(kMenuStarForce);
  }
  EquipSlot slot = selected_slot();
  if (slot != EQUIP_SLOT_UNSPECIFIED) {
    const EquipInstance& item = character_.equipped().at(slot);
    // Scrolling asks the prototype, not the slot count: a weapon with every
    // slot spent still takes a Clean Slate, so only an item that refuses
    // scrolls outright loses the entry.
    if (!Supports(item.prototype(), UPGRADE_SCROLL)) {
      menu_.Disable(kMenuScroll);
    }
    if (!item.CanStarForce()) {
      menu_.Disable(kMenuStarForce);
    }
  }
}

Screen EquippedPanel::OnMenuEvent(ftxui::Event event,
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
  opt.entries_option.transform =
      [this](ftxui::EntryState state) -> ftxui::Element {
    std::string cursor = state.focused ? "> " : "  ";
    ftxui::Element row = ftxui::text(cursor + state.label);
    int idx = state.index;
    if (idx == selected_) {
      // Records where this row lands so the item menu can open beside it.
      row = std::move(row) | ftxui::reflect(cursor_box_);
    }
    if (idx >= 0 && idx < static_cast<int>(inactive_.size()) &&
        inactive_[idx]) {
      // The item is worn but contributing nothing, so the whole row is dimmed
      // rather than hidden -- it says so without taking the numbers away.
      row |= ftxui::dim;
    }
    return row;
  };
  ftxui::Component menu = ftxui::Menu(&entries_, &selected_, opt);

  // Focusable whether or not anything is worn. Container::Tab asks its active
  // panel whether it is focusable and drops every key when the answer is no,
  // and an ftxui::Menu says no on an empty list -- which would leave this
  // panel unable to handle a key at all the moment the player strips down.
  //
  // entries_ and slots_ are rebuilt from equipped() on every render so the
  // display stays in sync with changes made via on_enter.
  ftxui::Component renderer =
      AlwaysFocusable(ftxui::Renderer(menu, [this, menu]() -> ftxui::Element {
        bool focused = panel_focus_ == kEquipPanel;
        entries_.clear();
        slots_.clear();
        inactive_.clear();
        for (const std::pair<const EquipSlot, EquipInstance>& kv :
             character_.equipped()) {
          slots_.push_back(kv.first);
          const EquipInstance& item = kv.second;
          const EquipStats stats = item.stats();
          Job job = character_.proto().job();
          // One column for the stat this job's damage is built on, which is
          // the same question the character panel and the AP reset ask -- so
          // ask it in the same place rather than switching over jobs here.
          const DisplayStat* main = DisplayStatFor(PrimaryStatField(job));
          int main_val = main != nullptr ? main->GetFrom(stats) : 0;
          const char* main_label = main != nullptr ? main->label : nullptr;
          // There is room for one attack figure, so show the one this job
          // swings with. A wand carries both, and a magician's weapon attack
          // is the half that never reaches the damage chain.
          bool magic = job == JOB_MAGICIAN;
          int atk_val = 0;
          const char* atk_label = nullptr;
          if (!magic && stats.attack() > 0) {
            atk_val = stats.attack();
            atk_label = "ATT";
          } else if (stats.magic_attack() > 0) {
            atk_val = stats.magic_attack();
            atk_label = "MATT";
          } else if (stats.attack() > 0) {
            atk_val = stats.attack();
            atk_label = "ATT";
          }
          std::string main_str;
          if (main_val > 0 && main_label != nullptr) {
            main_str = "+" + std::to_string(main_val) + " " + main_label;
          }
          std::string atk_str;
          if (atk_val > 0 && atk_label != nullptr) {
            atk_str = "+" + std::to_string(atk_val) + " " + atk_label;
          }
          // Attack leads: it is the number that decides a weapon, and the main
          // stat qualifies it.
          std::string info = PadRight(atk_str, 10) + PadRight(main_str, 10);
          inactive_.push_back(!character_.AttackCounts(item.prototype()));
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
          return AccentWindow(" Equipped ", EmptyState("empty"),
                              PanelAccent(highlighted_), focused);
        }
        return AccentWindow(" Equipped ",
                            ftxui::vbox({
                                ftxui::text(kColumnHeader),
                                ftxui::text(kColumnHeader2),
                                PanelSeparator(highlighted_),
                                menu->Render(),
                            }),
                            PanelAccent(highlighted_), focused);
      }));
  return ftxui::CatchEvent(renderer, [this, on_enter](ftxui::Event event) {
    if (event == ftxui::Event::Character(' ')) {
      // There is no item to act on with nothing worn, and the menu opens on
      // whatever selected_slot() names -- which is EQUIP_SLOT_UNSPECIFIED, a
      // slot the map has no entry for. Swallowed rather than passed on, as on
      // an empty tab of the bag.
      if (!character_.equipped().empty()) {
        on_enter();
      }
      return true;
    }
    return false;
  });
}

}  // namespace ms
