#include "src/frontend/widgets/chrome.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "ftxui/screen/string.hpp"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/format.h"
#include "src/item/item.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"

namespace ms {
namespace {

constexpr int kSlotWidth = 10;
constexpr int kInfoWidth = 20;
constexpr int kScrollWidth = 6;

// Fills a row with background color rather than block glyphs, so the label can
// sit on top of it without the two fighting over the same characters. The
// label takes one color over the fill and another past it; pass the same color
// twice to hold it steady as the bar moves.
//
// One row per label line, every row filled the same: a bar of two lines is one
// bar, not two stacked.
class ProgressBarNode : public ftxui::Node {
 public:
  ProgressBarNode(float frac, ftxui::Color fill,
                  std::vector<std::string> labels, ftxui::Color label_on_fill,
                  ftxui::Color label_off_fill)
      : frac_(std::clamp(frac, 0.0f, 1.0f)),
        fill_(fill),
        labels_(std::move(labels)),
        label_on_fill_(label_on_fill),
        label_off_fill_(label_off_fill) {
    if (labels_.empty()) {
      labels_.push_back("");
    }
  }

  void ComputeRequirement() override {
    requirement_.min_x = 1;
    requirement_.min_y = static_cast<int>(labels_.size());
  }

  void Render(ftxui::Screen& screen) override {
    const int width = box_.x_max - box_.x_min + 1;
    const int fill_end = box_.x_min + static_cast<int>(frac_ * width);
    for (int row = 0; row < static_cast<int>(labels_.size()); ++row) {
      const int y = box_.y_min + row;
      if (y > box_.y_max) {
        return;
      }
      RenderRow(screen, y, width, fill_end, labels_[row]);
    }
  }

 private:
  void RenderRow(ftxui::Screen& screen, int y, int width, int fill_end,
                 const std::string& label) {
    for (int x = box_.x_min; x <= box_.x_max; ++x) {
      ftxui::Pixel& px = screen.PixelAt(x, y);
      px.character = " ";
      px.background_color = x < fill_end ? fill_ : kBarEmpty;
    }

    const int label_len = static_cast<int>(label.size());
    const int label_x = box_.x_min + (width - label_len) / 2;
    for (int i = 0; i < label_len; ++i) {
      int x = label_x + i;
      if (x < box_.x_min || x > box_.x_max) {
        continue;
      }
      ftxui::Pixel& px = screen.PixelAt(x, y);
      px.character = std::string(1, label[i]);
      px.foreground_color = x < fill_end ? label_on_fill_ : label_off_fill_;
    }
  }

  float frac_;
  ftxui::Color fill_;
  std::vector<std::string> labels_;
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

ftxui::Color MarkColor(CurrencyColor color) {
  static_assert(CurrencyColor_ARRAYSIZE == 4,
                "a new currency colour needs a colour to draw its mark in");
  switch (color) {
    case CURRENCY_COLOR_THEME:
      return kTheme;
    case CURRENCY_COLOR_ORANGE:
      return kOrange;
    case CURRENCY_COLOR_PURPLE:
      return kPurple;
    default:
      return kTheme;
  }
}

ftxui::Color RarityColor(AbilityRank rank) {
  static_assert(AbilityRank_ARRAYSIZE == 5,
                "a new Inner Ability rank needs a colour");
  switch (rank) {
    case ABILITY_RANK_EPIC:
      return kEpic.ToColor();
    case ABILITY_RANK_UNIQUE:
      return kUnique.ToColor();
    case ABILITY_RANK_LEGENDARY:
      return kLegendary.ToColor();
    default:
      return kRare.ToColor();
  }
}

ftxui::Element ProgressBar(float frac, ftxui::Color fill,
                           const std::string& label) {
  return ProgressBar(frac, fill, std::vector<std::string>{label});
}

ftxui::Element ProgressBar(float frac, ftxui::Color fill,
                           const std::string& label, ftxui::Color label_color) {
  return std::make_shared<ProgressBarNode>(
      frac, fill, std::vector<std::string>{label}, label_color, label_color);
}

ftxui::Element ProgressBar(float frac, ftxui::Color fill,
                           const std::vector<std::string>& labels) {
  return std::make_shared<ProgressBarNode>(
      frac, fill, labels, ftxui::Color::Black, ftxui::Color::White);
}

ftxui::Element Floating(ftxui::Element element) {
  return std::make_shared<FloatingNode>(std::move(element));
}

ftxui::Element ResultWindow(const std::string& title,
                            const std::string& subject,
                            std::vector<ftxui::Element> body,
                            ftxui::Color accent) {
  std::vector<ftxui::Element> rows;
  rows.push_back(CenteredRow(subject));
  rows.push_back(AccentSeparator(accent));
  for (ftxui::Element& row : body) {
    rows.push_back(std::move(row));
  }
  rows.push_back(AccentSeparator(accent));
  rows.push_back(CenteredRow(ContinueButton()));
  return AccentWindow(title, ftxui::vbox(std::move(rows)), accent);
}

ftxui::Element DialogWindow(const std::string& title,
                            std::vector<ftxui::Element> body,
                            ftxui::Element buttons, ftxui::Color accent) {
  std::vector<ftxui::Element> rows = std::move(body);
  rows.push_back(AccentSeparator(accent));
  rows.push_back(CenteredRow(std::move(buttons)));
  return AccentWindow(title, ftxui::vbox(std::move(rows)), accent);
}

ftxui::Element EmptyState(const std::string& what, int gutter) {
  return ftxui::text(std::string(gutter, ' ') + "(" + what + ")");
}

std::string AdvanceTabKey(int stage) {
  return "advance:" + std::to_string(stage);
}

std::string EquipGiftTabKey(int stage) {
  return "equip_gift:" + std::to_string(stage);
}

namespace {

// One chip, with its leading pad optional: a chip the left scroll mark stands
// beside gives that column up to the mark.
ftxui::Element Chip(const std::string& label, bool active, bool row_focused,
                    bool unseen, bool left_pad) {
  ftxui::Element chip =
      ftxui::text(std::string(left_pad ? " " : "") + label + " ");
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

}  // namespace

ftxui::Element TabChip(const std::string& label, bool active, bool row_focused,
                       bool unseen) {
  return Chip(label, active, row_focused, unseen, /*left_pad=*/true);
}

// The mark standing where a bar runs off its edge. The left one is drawn in
// the leading chip's own pad, so a bar that scrolls starts its labels in the
// column a bar that fits starts them in -- two bars stacked in a panel line up
// whether or not either overflows. The right one takes a column of its own,
// reserved whether or not there is a mark to put there, so the chips hold
// still as the bar scrolls under them.
namespace {

constexpr char kMoreLeft[] = "‹";   // a single left angle
constexpr char kMoreRight[] = "›";  // and its mirror
constexpr int kMoreWidth = 1;

int ChipWidth(const TabSpec& tab) {
  return ftxui::string_width(tab.label) + 2;  // TabChip pads a space each side
}

// The leftmost chip a window on to `tabs` can start at and still reach
// `active` within `budget` columns. Being leftmost makes it a plain function
// of the selection: stepping right moves the window by the least it can, and
// stepping back moves it to exactly where it was on the way out.
int FirstVisible(const std::vector<TabSpec>& tabs, int active, int budget) {
  int first = 0;
  while (first < active) {
    int used = 0;
    for (int i = first; i <= active; ++i) {
      used += ChipWidth(tabs[i]);
    }
    if (used <= budget) {
      break;
    }
    ++first;
  }
  return first;
}

ftxui::Element MoreMark(const char* glyph, bool show) {
  return ftxui::text(show ? glyph : " ") | ftxui::color(kTheme);
}

}  // namespace

ftxui::Element TabBar(const std::vector<TabSpec>& tabs, int active,
                      bool row_focused, int width) {
  int end = static_cast<int>(tabs.size());
  int whole = 0;
  for (const TabSpec& tab : tabs) {
    whole += ChipWidth(tab);
  }
  // A bar that fits is drawn as it always was, marks and all left off. Their
  // two columns would otherwise indent every bar in the game for the sake of
  // the one that scrolls.
  bool scrolls = width > 0 && whole > width;

  int first = 0;
  int last = end - 1;
  if (scrolls) {
    int budget = width - kMoreWidth;  // the right mark's own column
    first = FirstVisible(tabs, std::clamp(active, 0, last), budget);
    int used = 0;
    for (last = first; last < end; ++last) {
      used += ChipWidth(tabs[last]);
      if (used > budget) {
        break;
      }
    }
    --last;  // the one that did not fit, or the end of the list
    // A budget too small for even one chip still shows the chip the player is
    // on: a bar that draws nothing says less than one that overflows.
    last = std::max(last, first);
  }

  bool more_left = scrolls && first > 0;
  std::vector<ftxui::Element> chips;
  if (more_left) {
    chips.push_back(MoreMark(kMoreLeft, true));
  }
  for (int i = first; i <= last; ++i) {
    chips.push_back(Chip(tabs[i].label, i == active, row_focused,
                         tabs[i].unseen,
                         /*left_pad=*/!(more_left && i == first)));
  }
  if (scrolls) {
    chips.push_back(MoreMark(kMoreRight, last < end - 1));
  }
  return ftxui::hbox(std::move(chips));
}

ftxui::Element ActionButton(const std::string& label, bool focused) {
  ftxui::Element button = ftxui::text("[" + label + "]");
  if (focused) {
    button = button | ftxui::inverted;
  }
  return button;
}

ftxui::Element ContinueButton(const std::string& label) {
  return ActionButton(label, /*focused=*/true);
}

ftxui::Element ButtonRow(const std::string& go, const std::string& leave,
                         bool go_focused, bool leave_focused, bool go_enabled) {
  ftxui::Element go_button = ActionButton(go, go_focused);
  if (!go_enabled) {
    go_button = std::move(go_button) | ftxui::dim;
  }
  return ftxui::hbox({
      ftxui::text(" "),
      std::move(go_button),
      ftxui::text("   "),
      ActionButton(leave, leave_focused),
      ftxui::text(" "),
  });
}

int ScrollWindowStart(int total, int selected, int visible) {
  if (visible <= 0 || total <= visible) {
    return 0;
  }
  // (visible - 1) / 2 rather than visible / 2, which is what yframe uses: an
  // even window puts the cursor a row above its middle either way.
  int first = selected - (visible - 1) / 2;
  return std::max(0, std::min(first, total - visible));
}

std::vector<ftxui::Element> ScrollBarCells(int total, int first_visible,
                                           int visible) {
  if (visible <= 0 || total <= visible) {
    return {};
  }
  // Counted in half rows, so the thumb can start and end halfway down a cell.
  // This is ftxui's own arithmetic, kept so the bar matches the bag's.
  int size = std::max(1, 2 * visible * visible / total);
  int start = 2 * first_visible * visible / total;
  std::vector<ftxui::Element> cells;
  for (int row = 0; row < visible; ++row) {
    bool top = start <= 2 * row && 2 * row <= start + size;
    bool bottom = start <= 2 * row + 1 && 2 * row + 1 <= start + size;
    const char* glyph = top ? (bottom ? "┃" : "╹") : (bottom ? "╻" : " ");
    cells.push_back(ftxui::text(glyph));
  }
  return cells;
}

ftxui::Element ScrollBar(int total, int first_visible, int visible) {
  std::vector<ftxui::Element> cells =
      ScrollBarCells(total, first_visible, visible);
  if (cells.empty()) {
    return ftxui::text("");
  }
  return ftxui::vbox(std::move(cells));
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

ftxui::Element RedUnless(ftxui::Element cell, bool ok) {
  return ok ? cell : std::move(cell) | ftxui::color(kRed);
}

ftxui::Element ThemedSeparator() {
  return AccentSeparator(kTheme);
}

}  // namespace ms
