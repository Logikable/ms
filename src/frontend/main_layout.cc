#include "src/frontend/main_layout.h"

#include <utility>

#include "ftxui/dom/elements.hpp"

namespace ms {

ftxui::Element MainLayout(ftxui::Element character, ftxui::Element combat,
                          ftxui::Element equipped, ftxui::Element inventory,
                          ftxui::Element exp_bar) {
  return ftxui::vbox({
      // Flexed, and with no filler under it, so the row reaches down to the
      // exp bar instead of stopping at the height of whatever it holds.
      ftxui::hbox({
          // Columns, not bare panels. An hbox hands every child the full
          // height of the row, so a panel placed here directly would stretch
          // and its bottom border would drift away from its contents. A vbox
          // leaves slack at the end of the column unused instead.
          ftxui::vbox({
              std::move(character),
              std::move(combat),
          }),
          ftxui::vbox({
              std::move(equipped),
              std::move(inventory) | ftxui::yflex_shrink,
          }) | ftxui::flex,
      }) | ftxui::flex,
      std::move(exp_bar),
  });
}

}  // namespace ms
