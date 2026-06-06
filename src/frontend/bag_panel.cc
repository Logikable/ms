#include "src/frontend/bag_panel.h"

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/equip_instance.h"
#include "src/frontend/scroll_panel.h"
#include "src/protos/equip.pb.h"

namespace ms {
namespace {

// Width of the level+job info column; slots column follows at a fixed offset.
constexpr int kInfoWidth = 20;

}  // namespace

BagPanel::BagPanel(CharacterInstance& character, int& panel_focus)
    : character_(character),
      panel_focus_(panel_focus),
      menu_({"Equip", "Inspect", "Scroll"}) {
}

void BagPanel::SetShowSelection(bool show) {
  show_selection_ = show;
}

void BagPanel::OpenMenu() {
  menu_.Reset();
}

Screen BagPanel::OnMenuEvent(ftxui::Event event, int& panel_focus,
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
      character_.Equip(selected_);
      if (character_.inventory().empty()) {
        panel_focus = kEquipPanel;
      }
      return kMain;
    }
    if (menu_.selected() == kMenuScroll) {
      if (scroll_panel.SetFilterForPrototype(
              character_.inventory()[selected_].prototype())) {
        return kScrollSelect;
      }
    }
    return kMain;
  }
  return kItemMenu;
}

ftxui::Component BagPanel::MakeComponent(std::function<void()> on_enter) {
  ftxui::MenuOption opt;
  opt.on_enter = [on_enter]() { on_enter(); };
  ftxui::Component menu = ftxui::Menu(&entries_, &selected_, opt);

  // entries_ is rebuilt from inventory() on every render so the display stays
  // in sync with changes made via on_enter.
  return ftxui::Renderer(menu, [this, menu]() -> ftxui::Element {
    entries_.clear();
    for (const EquipInstance& item : character_.inventory()) {
      const EquipPrototype& proto = item.prototype();
      int level = 1;
      if (proto.required_level() > 0) {
        level = proto.required_level();
      }
      std::string info = "Lv" + PadRight(std::to_string(level), 3) + "  " +
                         FormatJobCategories(proto);
      while ((int)info.size() < kInfoWidth) {
        info += ' ';
      }
      entries_.push_back(
          PadRight(proto.name(), 18) + "  " + info + "  " +
          std::to_string(item.proto().remaining_upgrade_slots()) + " slots");
    }
    if (!entries_.empty()) {
      selected_ = std::min(selected_, static_cast<int>(entries_.size()) - 1);
    }
    if (entries_.empty()) {
      return ftxui::window(ftxui::text(" Bag "), ftxui::text("(empty)"));
    }
    if (!show_selection_) {
      std::vector<ftxui::Element> items;
      for (const std::string& e : entries_) {
        items.push_back(ftxui::text("  " + e));
      }
      return ftxui::window(ftxui::text(" Bag "), ftxui::vbox(std::move(items)));
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

// Returns "All" for universal items or a slash-separated list of job names.
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
