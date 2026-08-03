#include "src/frontend/main_layout.h"

#include <utility>

#include "ftxui/dom/elements.hpp"

namespace ms {

ftxui::Element MainLayout(ftxui::Element character, ftxui::Element combat,
                          ftxui::Element equipped, ftxui::Element inventory,
                          ftxui::Element hotkeys, ftxui::Element exp_bar) {
  // Columns, not bare panels. An hbox hands every child the full height of the
  // row, so a panel placed here directly would stretch and its bottom border
  // would drift away from its contents. A vbox leaves slack at the end of the
  // column unused instead.
  ftxui::Elements columns;
  columns.push_back(ftxui::vbox({
      std::move(character),
      // Combat is pinned to the foot of the column, so it sits in the
      // bottom-left corner of the screen however tall the terminal is rather
      // than trailing the character panel down from the top.
      ftxui::filler(),
      std::move(combat),
  }));

  ftxui::Elements right;
  if (equipped != nullptr) {
    right.push_back(std::move(equipped));
  }
  if (inventory != nullptr) {
    right.push_back(std::move(inventory) | ftxui::yflex_shrink);
  }
  if (hotkeys != nullptr) {
    // Pinned to the foot of the column, the mirror of combat on the left. The
    // filler goes in whether or not anything is above it: for the first two
    // levels the tip is the whole right column, and without it the tip would
    // sit at the top of the screen and then jump to the bottom the moment the
    // equipped panel appeared over it.
    right.push_back(ftxui::filler());
    // Against a filler rather than straight into the column. The column is
    // what flexes to fill the width, and a vbox hands that width to every
    // child -- so on its own, at the levels before the equipped panel arrives,
    // the tip would stretch most of the way across the screen. The filler
    // takes the slack and leaves it its own width, in the corner.
    right.push_back(ftxui::hbox({ftxui::filler(), std::move(hotkeys)}));
  }
  if (!right.empty()) {
    columns.push_back(ftxui::vbox(std::move(right)) | ftxui::flex);
  }

  return ftxui::vbox({
      // Flexed, and with no filler under it, so the row reaches down to the
      // exp bar instead of stopping at the height of whatever it holds.
      ftxui::hbox(std::move(columns)) | ftxui::flex,
      std::move(exp_bar),
  });
}

}  // namespace ms
