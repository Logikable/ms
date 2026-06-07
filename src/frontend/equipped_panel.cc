#include "src/frontend/equipped_panel.h"

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/equip_instance.h"
#include "src/equip_stats.h"
#include "src/frontend/scroll_panel.h"
#include "src/protos/equip.pb.h"

namespace ms {
namespace {

// Width of the stats info column; slots column follows at a fixed offset.
constexpr int kInfoWidth = 20;

}  // namespace

EquippedPanel::EquippedPanel(CharacterInstance& character, int& panel_focus)
    : character_(character),
      panel_focus_(panel_focus),
      menu_({"Unequip", "Inspect", "Scroll"}) {
}

void EquippedPanel::SetShowSelection(bool show) {
  show_selection_ = show;
}

void EquippedPanel::OpenMenu() {
  menu_.Reset();
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
      while ((int)info.size() < kInfoWidth) {
        info += ' ';
      }
      entries_.push_back(
          PadRight(item.prototype().name(), 18) + "  " + info + "  " +
          std::to_string(item.proto().remaining_upgrade_slots()) + " slots");
    }
    if (!entries_.empty()) {
      selected_ = std::min(selected_, static_cast<int>(entries_.size()) - 1);
    }
    if (entries_.empty()) {
      return ftxui::window(ftxui::text(" Equipped "), ftxui::text("(empty)"));
    }
    if (!show_selection_) {
      std::vector<ftxui::Element> items;
      for (const std::string& e : entries_) {
        items.push_back(ftxui::text("  " + e));
      }
      return ftxui::window(ftxui::text(" Equipped "),
                           ftxui::vbox(std::move(items)));
    }
    return ftxui::window(ftxui::text(" Equipped "), menu->Render());
  });
}

std::string EquippedPanel::PadRight(const std::string& s, int width) {
  if ((int)s.size() >= width) {
    return s.substr(0, width);
  }
  return s + std::string(width - (int)s.size(), ' ');
}

void EquippedPanel::AppendStat(std::string& out, int val,
                               const std::string& name) {
  if (val <= 0) {
    return;
  }
  if (!out.empty()) {
    out += "  ";
  }
  out += "+" + std::to_string(val) + " " + name;
}

}  // namespace ms
