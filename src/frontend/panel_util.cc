#include "src/frontend/panel_util.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "ftxui/dom/elements.hpp"
#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/frontend/colors.h"
#include "src/protos/equip.pb.h"

namespace ms {
namespace {

constexpr int kSlotWidth = 10;
constexpr int kInfoWidth = 20;

// Fills a row with background color rather than block glyphs, so the label can
// sit on top of it without the two fighting over the same characters. The
// label takes one color over the fill and another past it; pass the same color
// twice to hold it steady as the bar moves.
class ProgressBarNode : public ftxui::Node {
 public:
  ProgressBarNode(float frac, ftxui::Color fill, std::string label,
                  ftxui::Color label_on_fill, ftxui::Color label_off_fill)
      : frac_(std::clamp(frac, 0.0f, 1.0f)),
        fill_(fill),
        label_(std::move(label)),
        label_on_fill_(label_on_fill),
        label_off_fill_(label_off_fill) {
  }

  void ComputeRequirement() override {
    requirement_.min_x = 1;
    requirement_.min_y = 1;
  }

  void Render(ftxui::Screen& screen) override {
    const int y = box_.y_min;
    const int width = box_.x_max - box_.x_min + 1;
    const int fill_end = box_.x_min + static_cast<int>(frac_ * width);

    for (int x = box_.x_min; x <= box_.x_max; ++x) {
      ftxui::Pixel& px = screen.PixelAt(x, y);
      px.character = " ";
      px.background_color = x < fill_end ? fill_ : kBarEmpty;
    }

    const int label_len = static_cast<int>(label_.size());
    const int label_x = box_.x_min + (width - label_len) / 2;
    for (int i = 0; i < label_len; ++i) {
      int x = label_x + i;
      if (x < box_.x_min || x > box_.x_max) {
        continue;
      }
      ftxui::Pixel& px = screen.PixelAt(x, y);
      px.character = std::string(1, label_[i]);
      px.foreground_color = x < fill_end ? label_on_fill_ : label_off_fill_;
    }
  }

 private:
  float frac_;
  ftxui::Color fill_;
  std::string label_;
  ftxui::Color label_on_fill_;
  ftxui::Color label_off_fill_;
};

}  // namespace

std::string PadRight(const std::string& s, int width) {
  if ((int)s.size() >= width) {
    return s.substr(0, width);
  }
  return s + std::string(width - (int)s.size(), ' ');
}

std::string FormatWithCommas(int64_t n) {
  std::string digits = std::to_string(n < 0 ? -n : n);
  int pos = (int)digits.size() - 3;
  while (pos > 0) {
    digits.insert(pos, ",");
    pos -= 3;
  }
  return n < 0 ? "-" + digits : digits;
}

std::string FormatMeso(int64_t meso) {
  return "🪙 " + FormatWithCommas(meso);
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

std::string JobName(Job job) {
  switch (job) {
    case JOB_BEGINNER:
      return "Beginner";
    case JOB_WARRIOR:
      return "Warrior";
    default:
      return "Unknown";
  }
}

std::string StatFieldName(StatField field) {
  switch (field) {
    case STAT_FIELD_STR:
      return "STR";
    case STAT_FIELD_DEX:
      return "DEX";
    case STAT_FIELD_INT:
      return "INT";
    case STAT_FIELD_LUK:
      return "LUK";
    case STAT_FIELD_HP:
      return "HP";
    case STAT_FIELD_MP:
      return "MP";
    default:
      return "";
  }
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

ftxui::Element ProgressBar(float frac, ftxui::Color fill,
                           const std::string& label) {
  return std::make_shared<ProgressBarNode>(
      frac, fill, label, ftxui::Color::Black, ftxui::Color::White);
}

ftxui::Element ProgressBar(float frac, ftxui::Color fill,
                           const std::string& label, ftxui::Color label_color) {
  return std::make_shared<ProgressBarNode>(frac, fill, label, label_color,
                                           label_color);
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

ftxui::Element ThemedWindow(const std::string& title, ftxui::Element content,
                            bool focused) {
  ftxui::Element title_el = ftxui::text(title) | ftxui::color(kTheme);
  if (focused) {
    title_el = title_el | ftxui::inverted;
  }
  return ftxui::window(std::move(title_el),
                       std::move(content) | ftxui::color(ftxui::Color::White)) |
         ftxui::color(kTheme);
}

ftxui::Element ThemedSeparator() {
  return ftxui::separator() | ftxui::color(kTheme);
}

}  // namespace ms
