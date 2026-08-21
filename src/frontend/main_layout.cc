#include "src/frontend/main_layout.h"

#include <utility>

#include "ftxui/dom/elements.hpp"

namespace ms {

ftxui::Element MainLayout(ftxui::Element character, ftxui::Element combat,
                          ftxui::Element equipped, ftxui::Element inventory,
                          ftxui::Element corner, ftxui::Element exp_bar) {
  // Columns, not bare panels: an hbox hands every child the full height of the
  // row, which would drag a panel's bottom border away from its contents.
  ftxui::Elements columns;
  columns.push_back(ftxui::vbox({
      std::move(character),
      // Pinned to the foot, so combat holds the bottom-left corner however
      // tall the terminal is. It belongs in this column and not in a row of
      // its own: as a row it capped the column beside it at its own top edge.
      ftxui::filler(),
      std::move(combat),
  }));

  ftxui::Elements right;
  if (equipped != nullptr) {
    right.push_back(std::move(equipped));
  }
  if (inventory != nullptr) {
    // The bag shrinks but does not grow, so an empty tab is a few rows rather
    // than a screen of blank. It is also the one shrinkable thing in the
    // column, which is what stops ftxui squashing every panel here a share of
    // the overflow instead.
    right.push_back(std::move(inventory) | ftxui::yflex_shrink);
  }
  if (corner != nullptr) {
    // Pinned to the foot, the mirror of combat. The filler goes in whether or
    // not anything is above it, or the corner would sit at the top of the
    // screen until the equipped panel arrived and then jump to the bottom.
    right.push_back(ftxui::filler());
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
