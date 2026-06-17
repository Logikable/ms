#include "src/frontend/panel_util.h"

#include <string>

#include "ftxui/dom/elements.hpp"
#include "src/protos/equip.pb.h"

namespace ms {
namespace {

constexpr int kSlotWidth = 10;
constexpr int kInfoWidth = 20;

}  // namespace

std::string PadRight(const std::string& s, int width) {
  if ((int)s.size() >= width) {
    return s.substr(0, width);
  }
  return s + std::string(width - (int)s.size(), ' ');
}

void AppendStat(std::string& out, int val, const std::string& label) {
  if (val <= 0) {
    return;
  }
  if (!out.empty()) {
    out += "  ";
  }
  out += "+" + std::to_string(val) + " " + label;
}

std::string FormatSlot(EquipSlot slot) {
  switch (slot) {
    case EQUIP_SLOT_PRIMARY_WEAPON:
      return "Weapon";
    default:
      return "";
  }
}

std::string FormatJobCategories(const EquipPrototype& proto) {
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
      case EQUIP_JOB_CATEGORY_BEGINNER:
        result += "Beginner";
        break;
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

std::string FormatItemEntry(const std::string& name, EquipSlot slot,
                            const std::string& info, int scroll_pass,
                            int scroll_left, int scroll_restore) {
  std::string padded_info = info;
  while ((int)padded_info.size() < kInfoWidth) {
    padded_info += ' ';
  }
  std::string scrolls;
  if (scroll_pass < 0) {
    scrolls = "-";
  } else {
    scrolls = std::to_string(scroll_pass) + "/" + std::to_string(scroll_left) +
              "/" + std::to_string(scroll_restore);
  }
  return PadRight(name, 26) + "  " + PadRight(FormatSlot(slot), kSlotWidth) +
         "  " + padded_info + "  " + scrolls;
}

ftxui::Element ConfirmBar(bool cancel_selected) {
  ftxui::Element confirm = ftxui::text("[Confirm]");
  ftxui::Element cancel = ftxui::text("[Cancel]");
  if (!cancel_selected) {
    confirm = confirm | ftxui::inverted;
  } else {
    cancel = cancel | ftxui::inverted;
  }
  return ftxui::hbox(
      {ftxui::text(" "), confirm, ftxui::text("  "), cancel, ftxui::text(" ")});
}

ftxui::Element ConfirmWindow(bool cancel_selected) {
  return ThemedWindow("", ConfirmBar(cancel_selected) | ftxui::hcenter);
}

ftxui::Color PanelBorderColor() {
  return ftxui::Color::RGB(100, 150, 200);
}

ftxui::Element ThemedWindow(const std::string& title, ftxui::Element content) {
  return ftxui::window(ftxui::text(title) | ftxui::color(PanelBorderColor()),
                       std::move(content) | ftxui::color(ftxui::Color::White)) |
         ftxui::color(PanelBorderColor());
}

ftxui::Element ThemedSeparator() {
  return ftxui::separator() | ftxui::color(PanelBorderColor());
}

}  // namespace ms
