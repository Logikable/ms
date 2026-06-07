#include "src/frontend/inspect_panel.h"

#include <string>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/equip_instance.h"
#include "src/protos/equip.pb.h"

namespace ms {

void InspectPanel::SetItem(const EquipInstance* item) {
  item_ = item;
}

ftxui::Element InspectPanel::Render() const {
  if (item_ == nullptr) {
    return ftxui::window(ftxui::text(" Inspect "), ftxui::text(" (none)"));
  }

  const EquipPrototype& proto = item_->prototype();
  const EquipStats& base = proto.base_stats();
  const EquipStats& scroll = item_->proto().scroll_stats();

  int level = proto.required_level() > 0 ? proto.required_level() : 1;
  std::string level_job =
      " Lv " + std::to_string(level) + "  " + FormatJobCategories(proto);

  std::vector<ftxui::Element> rows;
  rows.push_back(ftxui::text(" " + proto.name()));
  rows.push_back(ftxui::text(level_job));
  rows.push_back(ftxui::separator());

  bool any_stat = false;
  auto AddRow = [&](const std::string& label, int b, int s) {
    std::string line = StatLine(label, b, s);
    if (line.empty()) {
      return;
    }
    rows.push_back(ftxui::text(line));
    any_stat = true;
  };
  AddRow("ATT", base.attack(), scroll.attack());
  AddRow("MATT", base.magic_attack(), scroll.magic_attack());
  AddRow("STR", base.str(), scroll.str());
  AddRow("DEX", base.dex(), scroll.dex());
  AddRow("INT", base.int_(), scroll.int_());
  AddRow("LUK", base.luk(), scroll.luk());
  AddRow("HP", base.max_hp(), scroll.max_hp());
  AddRow("DEF", base.def(), scroll.def());

  if (!any_stat) {
    rows.push_back(ftxui::text(" (no stats)"));
  }

  rows.push_back(ftxui::separator());
  int slots = item_->proto().remaining_upgrade_slots();
  rows.push_back(
      ftxui::text(" " + std::to_string(slots) + " upgrade slots remaining"));

  return ftxui::window(ftxui::text(" Inspect "), ftxui::vbox(std::move(rows)));
}

std::string InspectPanel::StatLine(const std::string& label, int base,
                                   int scroll) {
  if (base == 0 && scroll == 0) {
    return "";
  }
  int total = base + scroll;
  return " " + label + "  +" + std::to_string(total) + " (" +
         std::to_string(base) + " +" + std::to_string(scroll) + ")";
}

std::string InspectPanel::FormatJobCategories(const EquipPrototype& proto) {
  for (int i = 0; i < proto.equip_job_categories_size(); ++i) {
    if (static_cast<EquipJobCategory>(proto.equip_job_categories(i)) ==
        EQUIP_JOB_CATEGORY_UNIVERSAL) {
      return "All";
    }
  }
  std::string result;
  for (int i = 0; i < proto.equip_job_categories_size(); ++i) {
    if (!result.empty()) {
      result += "/";
    }
    switch (static_cast<EquipJobCategory>(proto.equip_job_categories(i))) {
      case EQUIP_JOB_CATEGORY_WARRIOR:
        result += "Warrior";
        break;
      case EQUIP_JOB_CATEGORY_BOWMAN:
        result += "Bowman";
        break;
      case EQUIP_JOB_CATEGORY_MAGICIAN:
        result += "Magician";
        break;
      case EQUIP_JOB_CATEGORY_THIEF:
        result += "Thief";
        break;
      case EQUIP_JOB_CATEGORY_PIRATE:
        result += "Pirate";
        break;
      default:
        break;
    }
  }
  return result.empty() ? "All" : result;
}

}  // namespace ms
