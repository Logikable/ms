#include "src/frontend/placement.h"

#include <utility>

#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/chrome.h"

namespace ms {

ftxui::Element Centred(ftxui::Element window) {
  return ftxui::center(std::move(window));
}

ftxui::Element CardRow(ftxui::Elements cards) {
  return Centred(ftxui::hbox(std::move(cards)));
}

ftxui::Element Overlay(ftxui::Element screen, ftxui::Element dialog) {
  return ftxui::dbox({
      std::move(screen),
      Centred(ClearUnder(std::move(dialog))),
  });
}

}  // namespace ms
