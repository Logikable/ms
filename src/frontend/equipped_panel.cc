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
#include "src/protos/equip.pb.h"

namespace ms {
namespace {

// Two leading spaces match the "  " / "> " cursor added by the entry transform.
constexpr char kColumnHeader[] =
    "  Name              "    // 2 cursor + 18 name
    "  Equip Slot"            // 2 sep + 10 slot
    "  Stats               "  // 2 sep + 20 info
    "  Scrolls";              // 2 sep + label

}  // namespace

EquippedPanel::EquippedPanel(CharacterInstance& character, int& panel_focus)
    : character_(character),
      panel_focus_(panel_focus),
      menu_({"Unequip", "Inspect", "Scroll", "Star Force"}) {
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
  if (event == ftxui::Event::Escape) {
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
  if (event == ftxui::Event::Return) {
    if (menu_.selected() == kMenuAction) {
      character_.Unequip(selected_slot());
      if (character_.equipped().empty()) {
        panel_focus = kBagPanel;
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
  return ftxui::Renderer(menu, [this, menu]() -> ftxui::Element {
    entries_.clear();
    slots_.clear();
    for (const std::pair<const EquipSlot, EquipInstance>& kv :
         character_.equipped()) {
      slots_.push_back(kv.first);
      const EquipInstance& item = kv.second;
      const EquipStats stats = item.stats();
      std::string info;
      AppendStat(info, stats.attack(), "ATT");
      AppendStat(info, stats.magic_attack(), "MATT");
      AppendStat(info, stats.str(), "STR");
      AppendStat(info, stats.dex(), "DEX");
      AppendStat(info, stats.int_(), "INT");
      AppendStat(info, stats.luk(), "LUK");
      entries_.push_back(
          FormatItemEntry(item.prototype().name(), kv.first, info,
                          item.proto().remaining_upgrade_slots()));
    }
    if (!entries_.empty()) {
      selected_ = std::min(selected_, static_cast<int>(entries_.size()) - 1);
    }
    if (entries_.empty()) {
      return ftxui::window(ftxui::text(" Equipped "), ftxui::text("(empty)"));
    }
    return ftxui::window(ftxui::text(" Equipped "),
                         ftxui::vbox({
                             ftxui::text(kColumnHeader),
                             ftxui::separator(),
                             menu->Render(),
                         }));
  });
}

}  // namespace ms
