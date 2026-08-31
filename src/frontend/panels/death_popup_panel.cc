#include "src/frontend/panels/death_popup_panel.h"

#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/chrome.h"
#include "src/frontend/widgets/colors.h"

namespace ms {

ftxui::Element DeathPopupPanel() {
  // Five rows inside the border and the same width floor as the other two
  // cards, so all three are one shape the player learns rather than three
  // boxes that each have to be read afresh. Only the colour tells them apart,
  // which is the point: this one is red.
  return AccentWindow(" Death ",
                      ftxui::vbox({
                          ftxui::text(""),
                          ftxui::text(""),
                          CenteredRow("You died!"),
                          ftxui::text(""),
                          ftxui::text(""),
                      }) | ftxui::size(ftxui::WIDTH, ftxui::GREATER_THAN,
                                       kCelebrationContentWidth),
                      kRed);
}

}  // namespace ms
