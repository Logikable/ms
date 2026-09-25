#include "src/frontend/main_layout.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "ftxui/dom/elements.hpp"
#include "ftxui/dom/node.hpp"
#include "ftxui/dom/requirement.hpp"
#include "ftxui/screen/box.hpp"
#include "ftxui/screen/screen.hpp"

namespace ms {
namespace {

// Two panels, the top one never taking more than half their shared height
// (rounded down, so the odd row goes to the bottom). Neither is stretched past
// the height it asked for, and the node fills its column, so the space under
// the bottom panel is left blank.
class HalfAndRestNode : public ftxui::Node {
 public:
  HalfAndRestNode(ftxui::Element top, ftxui::Element bottom)
      : ftxui::Node({std::move(top), std::move(bottom)}) {
  }

  void ComputeRequirement() override {
    requirement_ = ftxui::Requirement();
    int y = 0;
    for (const ftxui::Element& child : children_) {
      child->ComputeRequirement();
      if (requirement_.focused.Prefer(child->requirement().focused)) {
        requirement_.focused = child->requirement().focused;
        requirement_.focused.box.Shift(0, y);
      }
      y += child->requirement().min_y;
      requirement_.min_x =
          std::max(requirement_.min_x, child->requirement().min_x);
    }
    requirement_.min_y = y;
    requirement_.flex_grow_y = 1;
    requirement_.flex_shrink_y = 1;
  }

  void SetBox(ftxui::Box box) override {
    ftxui::Node::SetBox(box);
    const int height = std::max(0, box.y_max - box.y_min + 1);
    top_rows_ = std::min(children_[0]->requirement().min_y, height / 2);
    bottom_rows_ =
        std::min(children_[1]->requirement().min_y, height - top_rows_);

    ftxui::Box top_box = box;
    top_box.y_max = box.y_min + top_rows_ - 1;
    children_[0]->SetBox(top_box);

    ftxui::Box bottom_box = box;
    bottom_box.y_min = box.y_min + top_rows_;
    bottom_box.y_max = bottom_box.y_min + bottom_rows_ - 1;
    children_[1]->SetBox(bottom_box);
  }

  void Render(ftxui::Screen& screen) override {
    // A panel squeezed to nothing is skipped, since its box would end above
    // where it starts and a border drawn in it would paint outside its space.
    if (top_rows_ > 0) {
      children_[0]->Render(screen);
    }
    if (bottom_rows_ > 0) {
      children_[1]->Render(screen);
    }
  }

 private:
  int top_rows_ = 0;
  int bottom_rows_ = 0;
};

ftxui::Element HalfAndRest(ftxui::Element top, ftxui::Element bottom) {
  return std::make_shared<HalfAndRestNode>(std::move(top), std::move(bottom));
}

}  // namespace

MainWidths ComputeMainWidths(int terminal_width, bool has_right_column) {
  MainWidths widths;
  // The right column's space is reserved whether or not it is shown, so the
  // character panel has the same width before and after the equipped panel
  // unlocks. A panel resizing under the player is worse than the blank columns
  // until then.
  widths.left = std::clamp(terminal_width - kRightColumnMin, kLeftColumnMin,
                           kLeftColumnMax);
  if (has_right_column) {
    widths.right = std::max(0, terminal_width - widths.left);
  }
  return widths;
}

ftxui::Element MainLayout(MainWidths widths, ftxui::Element character,
                          ftxui::Element combat, ftxui::Element equipped,
                          ftxui::Element inventory, ftxui::Element corner,
                          ftxui::Element exp_bar) {
  // Columns, not bare panels: an hbox gives every child the full row height,
  // which would pull a panel's bottom border away from its contents.
  ftxui::Elements columns;
  // Fixed width, because the column sets the width of both its panels, and they
  // must agree however wide the panel above drew itself.
  columns.push_back(ftxui::vbox({
                        std::move(character),
                        // Pinned to the bottom, so combat stays in the
                        // bottom-left corner however tall the terminal is. It
                        // belongs in this column, not a row of its own: as a
                        // row it limited the column beside it to its own top
                        // edge.
                        ftxui::filler(),
                        std::move(combat),
                    }) |
                    ftxui::size(ftxui::WIDTH, ftxui::EQUAL, widths.left));

  ftxui::Elements right;
  // The pair shares the column, with the equipped panel limited to half. There
  // are enough gear slots to fill a screen, and the player mostly works from
  // the bag.
  bool paired = equipped != nullptr && inventory != nullptr;
  if (paired) {
    right.push_back(HalfAndRest(std::move(equipped), std::move(inventory)));
  } else if (equipped != nullptr) {
    right.push_back(std::move(equipped));
  } else if (inventory != nullptr) {
    // The bag shrinks but doesn't grow, so an empty tab is a few rows instead
    // of a screen of blank space.
    right.push_back(std::move(inventory) | ftxui::yflex_shrink);
  }
  if (corner != nullptr) {
    // Pinned to the bottom, like combat on the left. Only when the pair is
    // absent: the pair already grows to fill the column, and a second growing
    // element would take half the space.
    if (!paired) {
      right.push_back(ftxui::filler());
    }
    // Next to a filler, or the corner panel takes the column's full width and
    // stretches across the screen.
    right.push_back(ftxui::hbox({ftxui::filler(), std::move(corner)}));
  }
  if (!right.empty()) {
    columns.push_back(ftxui::vbox(std::move(right)) | ftxui::flex);
  }

  return ftxui::vbox({
      // Flexed, with no filler under it, so the row reaches down to the EXP bar
      // instead of stopping at its content's height.
      ftxui::hbox(std::move(columns)) | ftxui::flex,
      std::move(exp_bar),
  });
}

}  // namespace ms
