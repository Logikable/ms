#include "src/frontend/widgets/panel_util.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/component/component_base.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/frontend/widgets/colors.h"
#include "src/protos/equip.pb.h"

namespace ms {
namespace {

constexpr int kSlotWidth = 10;
constexpr int kInfoWidth = 20;

// Overrides nothing but Focusable(). ComponentBase's own OnRender and OnEvent
// already forward to a lone child, so everything else passes straight through.
class AlwaysFocusableComponent : public ftxui::ComponentBase {
 public:
  explicit AlwaysFocusableComponent(ftxui::Component child) {
    Add(std::move(child));
  }

  bool Focusable() const override {
    return true;
  }
};

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

// Lays its child out at the child's own size, from the corner of whatever box
// the parent hands over, and tells the parent it needs nothing in return.
class FloatingNode : public ftxui::Node {
 public:
  explicit FloatingNode(ftxui::Element child)
      : ftxui::Node(ftxui::Elements{std::move(child)}) {
  }

  void ComputeRequirement() override {
    ftxui::Node::ComputeRequirement();
    // The child still measured itself above; this drops that on the floor so
    // the parent sizes as though the child were not there.
    requirement_ = ftxui::Requirement();
  }

  void SetBox(ftxui::Box box) override {
    ftxui::Node::SetBox(box);
    children_[0]->SetBox(NaturalBox());
  }

  void Render(ftxui::Screen& screen) override {
    // Laid out a second time, because how much room there is to run off is not
    // known until the screen is in hand, and SetBox runs before that.
    children_[0]->SetBox(FitToScreen(NaturalBox(), screen));
    children_[0]->Render(screen);
  }

 private:
  // The child at its full size in the parent box's top-left corner, reaching
  // past the parent wherever it is the larger of the two.
  ftxui::Box NaturalBox() const {
    const ftxui::Requirement& req = children_[0]->requirement();
    ftxui::Box box;
    box.x_min = box_.x_min;
    box.x_max = box_.x_min + req.min_x - 1;
    box.y_min = box_.y_min;
    box.y_max = box_.y_min + req.min_y - 1;
    return box;
  }

  // Slides a box back until its bottom-right corner is on the screen -- a
  // float that leaves the terminal is not drawn at all, which is worse than
  // one sitting a little higher than it asked to.
  //
  // A float that fits then sits on the screen whole, since it can never hang
  // off both ends at once. One too big to fit has to be clipped somewhere, and
  // this gives up its top-left: an overlay is positioned by empty space above
  // and to the left of what it draws, so that is the end with nothing on it.
  static ftxui::Box FitToScreen(ftxui::Box box, const ftxui::Screen& screen) {
    int over_x = std::max(0, box.x_max - (screen.dimx() - 1));
    box.x_min -= over_x;
    box.x_max -= over_x;
    int over_y = std::max(0, box.y_max - (screen.dimy() - 1));
    box.y_min -= over_y;
    box.y_max -= over_y;
    return box;
  }
};

}  // namespace

const DisplayStat* DisplayStatFor(StatField field) {
  // Both tables spell a stat the same way, so the label is the join between
  // them and neither needs to know the other's order.
  std::string name = StatFieldName(field);
  if (name.empty()) {
    return nullptr;
  }
  for (const DisplayStat& stat : kDisplayStats) {
    if (name == stat.label) {
      return &stat;
    }
  }
  return nullptr;
}

std::string PadRight(const std::string& s, int width) {
  if (static_cast<int>(s.size()) >= width) {
    return s.substr(0, width);
  }
  return s + std::string(width - static_cast<int>(s.size()), ' ');
}

std::string PadLeft(const std::string& s, int width) {
  if (static_cast<int>(s.size()) >= width) {
    return s;
  }
  return std::string(width - s.size(), ' ') + s;
}

std::string FormatWithCommas(int64_t n) {
  std::string digits = std::to_string(n < 0 ? -n : n);
  int pos = static_cast<int>(digits.size()) - 3;
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
    case EQUIP_SLOT_STARS:
      return "Stars";
    default:
      return "";
  }
}

std::string FormatEquipType(EquipType type) {
  switch (type) {
    case EQUIP_TYPE_ONE_HANDED_SWORD:
      return "One-Handed Sword";
    case EQUIP_TYPE_BOW:
      return "Bow";
    case EQUIP_TYPE_WAND:
      return "Wand";
    case EQUIP_TYPE_DAGGER:
      return "Dagger";
    case EQUIP_TYPE_CLAW:
      return "Claw";
    case EQUIP_TYPE_THROWING_STAR:
      return "Throwing Star";
    default:
      return "";  // not yet implemented for other types
  }
}

bool IsActive(const Skill& skill) {
  return skill.kind() == SKILL_KIND_ATTACK || skill.kind() == SKILL_KIND_ACTIVE;
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
    case JOB_SWORDMAN:
      return "Swordman";
    case JOB_ARCHER:
      return "Archer";
    case JOB_MAGICIAN:
      return "Magician";
    case JOB_ROGUE:
      return "Rogue";
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
  std::string padded_info = PadRight(info, kInfoWidth);
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

ftxui::Element Floating(ftxui::Element element) {
  return std::make_shared<FloatingNode>(std::move(element));
}

ftxui::Element ResultWindow(const std::string& title,
                            const std::string& subject,
                            std::vector<ftxui::Element> body) {
  std::vector<ftxui::Element> rows;
  rows.push_back(CenteredRow(subject));
  rows.push_back(ThemedSeparator());
  for (ftxui::Element& row : body) {
    rows.push_back(std::move(row));
  }
  rows.push_back(ThemedSeparator());
  rows.push_back(CenteredRow(ActionButton("Continue", /*focused=*/true)));
  return ThemedWindow(title, ftxui::vbox(std::move(rows)));
}

ftxui::Element EmptyState(const std::string& what, int gutter) {
  return ftxui::text(std::string(gutter, ' ') + "(" + what + ")");
}

std::string AdvanceTabKey(int stage) {
  return "advance:" + std::to_string(stage);
}

ftxui::Element TabChip(const std::string& label, bool active, bool row_focused,
                       bool unseen) {
  ftxui::Element chip = ftxui::text(" " + label + " ");
  if (active && row_focused) {
    return chip | ftxui::color(ftxui::Color::Black) |
           ftxui::bgcolor(ftxui::Color::White);
  }
  chip = chip | ftxui::color(unseen ? kYellow : kTheme);
  if (active) {
    chip = chip | ftxui::inverted;
  }
  return chip;
}

ftxui::Element ActionButton(const std::string& label, bool focused) {
  ftxui::Element button = ftxui::text("[" + label + "]");
  if (focused) {
    button = button | ftxui::inverted;
  }
  return button;
}

ftxui::Color PanelAccent(bool highlighted) {
  return highlighted ? kYellow : kTheme;
}

ftxui::Element AccentWindow(const std::string& title, ftxui::Element content,
                            ftxui::Color accent, bool focused) {
  ftxui::Element title_el = ftxui::text(title) | ftxui::color(accent);
  if (focused) {
    title_el = title_el | ftxui::inverted;
  }
  return ftxui::window(std::move(title_el),
                       std::move(content) | ftxui::color(ftxui::Color::White)) |
         ftxui::color(accent);
}

ftxui::Element ThemedWindow(const std::string& title, ftxui::Element content,
                            bool focused) {
  return AccentWindow(title, std::move(content), kTheme, focused);
}

ftxui::Element CenteredRow(ftxui::Element row) {
  // hcenter on its own does not do this. It centres within the width the widest
  // row of the window sets -- and the row that sets that width is, by
  // definition, the one flush against both borders.
  return ftxui::hbox({
             ftxui::text(" "),
             std::move(row),
             ftxui::text(" "),
         }) |
         ftxui::hcenter;
}

ftxui::Element CenteredRow(const std::string& text) {
  return CenteredRow(ftxui::text(text));
}

ftxui::Element AccentSeparator(ftxui::Color accent) {
  return ftxui::separator() | ftxui::color(accent);
}

ftxui::Element PanelSeparator(bool highlighted) {
  return AccentSeparator(PanelAccent(highlighted));
}

ftxui::Element ThemedSeparator() {
  return AccentSeparator(kTheme);
}

ftxui::Component AlwaysFocusable(ftxui::Component child) {
  return ftxui::Make<AlwaysFocusableComponent>(std::move(child));
}

}  // namespace ms
