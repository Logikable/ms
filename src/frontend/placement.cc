#include "src/frontend/placement.h"

#include <utility>

#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/chrome.h"

namespace ms {

ftxui::Element Centred(ftxui::Element window) {
  return ftxui::center(std::move(window));
}

ftxui::Element SideBySide(ftxui::Elements cards) {
  return Centred(ftxui::hbox(std::move(cards)));
}

ftxui::Element Overlay(ftxui::Element screen, ftxui::Element dialog) {
  return ftxui::dbox({
      std::move(screen),
      Centred(ClearUnder(std::move(dialog))),
  });
}

ftxui::Element BottomRight(ftxui::Element screen, ftxui::Element box) {
  // The fillers push the box into the corner. The screen behind sets how far
  // that is, because an overlaid vbox with no flexing child has no height of
  // its own.
  return ftxui::dbox({
      std::move(screen),
      ftxui::vbox({
          ftxui::filler(),
          ftxui::hbox({ftxui::filler(), ClearUnder(std::move(box))}),
      }),
  });
}

}  // namespace ms
