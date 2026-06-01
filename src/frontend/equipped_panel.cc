#include "src/frontend/equipped_panel.h"

#include <string>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/equip_instance.h"
#include "src/protos/equip.pb.h"

namespace ms {

EquippedPanel::EquippedPanel(CharacterInstance& character, int& panel_focus)
    : character_(character), panel_focus_(panel_focus) {
}

ftxui::Component EquippedPanel::MakeComponent() {
  ftxui::MenuOption opt;
  opt.on_enter = [this]() {
    if ((int)slots_.size() > selected_) {
      character_.Unequip(slots_[selected_]);
      selected_ = std::min(selected_,
                           std::max(0, (int)character_.equipped().size() - 1));
      if (character_.equipped().empty()) {
        panel_focus_ = 1;
      }
    }
  };
  ftxui::Component menu = ftxui::Menu(&entries_, &selected_, opt);

  return ftxui::Renderer(menu, [this, menu]() -> ftxui::Element {
    entries_.clear();
    slots_.clear();
    for (const std::pair<const EquipSlot, EquipInstance>& kv :
         character_.equipped()) {
      slots_.push_back(kv.first);
      const EquipInstance& item = kv.second;
      const EquipStats stats = item.stats();
      std::string line = PadRight(item.prototype().name(), 18) + "  ";
      AppendStat(line, stats.attack(), "ATT");
      AppendStat(line, stats.magic_attack(), "MATT");
      AppendStat(line, stats.str(), "STR");
      AppendStat(line, stats.dex(), "DEX");
      AppendStat(line, stats.int_(), "INT");
      AppendStat(line, stats.luk(), "LUK");
      line += "  " + std::to_string(item.proto().remaining_upgrade_slots()) +
              " slots";
      entries_.push_back(line);
    }
    if (entries_.empty()) {
      return ftxui::window(ftxui::text(" Equipped "), ftxui::text("(empty)"));
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
