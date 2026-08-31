#include "src/frontend/panels/advancement_popup_panel.h"

#include "ftxui/dom/elements.hpp"
#include "src/character/job_name.h"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/panel_util.h"
#include "src/protos/character.pb.h"

namespace ms {

ftxui::Element AdvancementPopupPanel(Job from_job, Job to_job) {
  // The level-up card's shape: five rows inside the border, same width floor.
  // No rule across it -- that card has one because it has two things to say.
  //
  // Short job names, because the card is one size and a full name would stretch
  // it. Nothing is lost: the full name was shown in the picker and again in the
  // dialog confirming it.
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
