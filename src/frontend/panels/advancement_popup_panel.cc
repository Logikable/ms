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
  // kCelebrationContentWidth gives.
  return AccentWindow(" Advancement ",
                      ftxui::vbox({
                          ftxui::text(""),
                          CenteredRow(JobName(from_job)),
                          CenteredRow("↓"),
                          CenteredRow(JobName(to_job)),
                          ftxui::text(""),
                      }) | ftxui::size(ftxui::WIDTH, ftxui::GREATER_THAN,
                                       kCelebrationContentWidth),
                      kYellow);
}

}  // namespace ms
