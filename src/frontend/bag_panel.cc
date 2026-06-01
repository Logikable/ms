#include "src/frontend/bag_panel.h"

#include <string>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/equip_instance.h"
#include "src/protos/equip.pb.h"

namespace ms {

BagPanel::BagPanel(CharacterInstance& character, int& panel_focus)
    : character_(character), panel_focus_(panel_focus) {
}

ftxui::Component BagPanel::MakeComponent() {
  ftxui::MenuOption opt;
  opt.on_enter = [this]() {
    character_.Equip(selected_);
    selected_ = std::min(selected_,
                         std::max(0, (int)character_.inventory().size() - 1));
    if (character_.inventory().empty()) {
      panel_focus_ = 0;
    }
  };
  ftxui::Component menu = ftxui::Menu(&entries_, &selected_, opt);

  return ftxui::Renderer(menu, [this, menu]() -> ftxui::Element {
    entries_.clear();
    for (const EquipInstance& item : character_.inventory()) {
      const EquipPrototype& proto = item.prototype();
      int level = 1;
      if (proto.required_level() > 0) {
        level = proto.required_level();
      }
      entries_.push_back("[" + PadLeft(std::to_string(entries_.size()), 2) +
                         "] " + PadRight(proto.name(), 18) + "  Lv" +
                         PadRight(std::to_string(level), 3) + "  " +
                         FormatJobCategories(proto));
    }
    if (entries_.empty()) {
      return ftxui::window(ftxui::text(" Bag "), ftxui::text("(empty)"));
    }
    return ftxui::window(ftxui::text(" Bag "), menu->Render());
  });
}

std::string BagPanel::PadRight(const std::string& s, int width) {
  if ((int)s.size() >= width) {
    return s.substr(0, width);
  }
  return s + std::string(width - (int)s.size(), ' ');
}

std::string BagPanel::PadLeft(const std::string& s, int width) {
  if ((int)s.size() >= width) {
    return s.substr(0, width);
  }
  return std::string(width - (int)s.size(), ' ') + s;
}

std::string BagPanel::FormatJobCategories(const EquipPrototype& proto) {
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
  if (result.empty()) {
    return "All";
  }
  return result;
}

}  // namespace ms
