#include "src/frontend/panels/advancement_popup_panel.h"

#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/panel_util.h"
#include "src/protos/character.pb.h"

namespace ms {

ftxui::Element AdvancementPopupPanel(Job from_job, Job to_job) {
  // Five rows inside the border, the same as the level-up card, with the three
  // that say anything held in the middle of them. No rule across it: the
  // level-up card has one because it has two things to say, and this has one.
  //
  // Same width floor as the level-up card too, and for both the reasons
  // kCelebrationContentWidth gives. Short names for the same reason: the card
  // is one size, and a job whose full name outgrew it would stretch it.
  //
  // The full name has already been shown twice by now -- in the picker and in
  // the dialog confirming it -- so nothing is lost by abbreviating here.
  return AccentWindow(" Advancement ",
                      ftxui::vbox({
                          ftxui::text(""),
                          CenteredRow(ShortJobName(from_job)),
                          CenteredRow("↓"),
                          CenteredRow(ShortJobName(to_job)),
                          ftxui::text(""),
                      }) | ftxui::size(ftxui::WIDTH, ftxui::GREATER_THAN,
                                       kCelebrationContentWidth),
                      kYellow);
}

}  // namespace ms
