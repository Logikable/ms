#include "src/frontend/cards/death_card.h"

#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/chrome.h"
#include "src/frontend/widgets/colors.h"

namespace ms {

ftxui::Element DeathCard() {
  // Five rows inside the border and the same minimum width as the other two
  // cards, so all three have one shape the player learns. Only the colour
  // differs, which is the point: this one is red.
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
