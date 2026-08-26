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

// Two panels, one over the other, where the top one may never take more than
// half the height they share -- rounded down, so the odd row falls to the
// bottom panel. Neither is stretched past the height it asked for.
//
// It also grows to fill its column, which is what leaves the slack between the
// bottom panel and whatever is pinned under it blank.
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
    // A panel squeezed down to nothing is skipped rather than drawn: its box
    // ends above where it starts, and a border asked to draw itself in one
    // paints outside the space it was given.
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
  int reserved = has_right_column ? kRightColumnMin : 0;
  MainWidths widths;
  widths.left =
      std::clamp(terminal_width - reserved, kLeftColumnMin, kLeftColumnMax);
  if (has_right_column) {
    widths.right = std::max(0, terminal_width - widths.left);
  }
  return widths;
}

ftxui::Element MainLayout(MainWidths widths, ftxui::Element character,
                          ftxui::Element combat, ftxui::Element equipped,
                          ftxui::Element inventory, ftxui::Element corner,
                          ftxui::Element exp_bar) {
  // Columns, not bare panels: an hbox hands every child the full height of the
  // row, which would drag a panel's bottom border away from its contents.
  ftxui::Elements columns;
  // Pinned rather than left to the panels: the column is what fixes their
  // width, and the two of them have to agree on one however wide the panel
  // above happens to have drawn itself.
  columns.push_back(ftxui::vbox({
                        std::move(character),
                        // Pinned to the foot, so combat holds the bottom-left
                        // corner however tall the terminal is. It belongs in
                        // this column and not in a row of its own: as a row it
                        // capped the column beside it at its own top edge.
                        ftxui::filler(),
                        std::move(combat),
                    }) |
                    ftxui::size(ftxui::WIDTH, ftxui::EQUAL, widths.left));

  ftxui::Elements right;
  // The pair takes the column between them, the equipped panel held to half of
  // it: there are enough gear slots now to fill a screen, and the bag is the
  // one of the two the player is working out of.
  bool paired = equipped != nullptr && inventory != nullptr;
  if (paired) {
    right.push_back(HalfAndRest(std::move(equipped), std::move(inventory)));
  } else if (equipped != nullptr) {
    right.push_back(std::move(equipped));
  } else if (inventory != nullptr) {
    // The bag shrinks but does not grow, so an empty tab is a few rows rather
    // than a screen of blank.
    right.push_back(std::move(inventory) | ftxui::yflex_shrink);
  }
  if (corner != nullptr) {
    // Pinned to the foot, the mirror of combat. Only where the pair is absent:
    // the pair grows to fill the column itself, and a second thing to grow
    // into the slack would take half of it away.
    if (!paired) {
      right.push_back(ftxui::filler());
    }
    // Against a filler, or the corner takes the column's whole flexed width
    // and stretches across the screen.
    right.push_back(ftxui::hbox({ftxui::filler(), std::move(corner)}));
  }
  if (!right.empty()) {
    columns.push_back(ftxui::vbox(std::move(right)) | ftxui::flex);
  }

  return ftxui::vbox({
      // Flexed, with no filler under it, so the row reaches down to the exp
      // bar rather than stopping at the height of what it holds.
      ftxui::hbox(std::move(columns)) | ftxui::flex,
      std::move(exp_bar),
  });
}

}  // namespace ms
