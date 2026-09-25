#include "src/frontend/cards/advancement_card.h"

#include "ftxui/dom/elements.hpp"
#include "src/character/job_name.h"
#include "src/frontend/widgets/chrome.h"
#include "src/frontend/widgets/colors.h"
#include "src/protos/character.pb.h"

namespace ms {

ftxui::Element AdvancementCard(Job from_job, Job to_job, int to_stage) {
  // The same shape as the level-up card: five rows inside the border and the
  // same minimum width, but no divider, since that card has one to separate two
  // kinds of information. It uses short job names so the card stays one size;
  // the full name was already shown twice.
  return AccentWindow(" Advancement ",
                      ftxui::vbox({
                          ftxui::text(""),
                          CenteredRow(ShortJobName(from_job)),
                          CenteredRow("↓"),
                          CenteredRow(ShortAdvancementName(to_job, to_stage)),
                          ftxui::text(""),
                      }) | ftxui::size(ftxui::WIDTH, ftxui::GREATER_THAN,
                                       kCelebrationContentWidth),
                      kYellow);
}

}  // namespace ms
