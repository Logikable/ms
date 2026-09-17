#include "src/frontend/cards/advancement_card.h"

#include "ftxui/dom/elements.hpp"
#include "src/character/job_name.h"
#include "src/frontend/widgets/chrome.h"
#include "src/frontend/widgets/colors.h"
#include "src/protos/character.pb.h"

namespace ms {

ftxui::Element AdvancementCard(Job from_job, Job to_job, int to_stage) {
  // The level-up card's shape: five rows inside the border, same width floor,
  // and no rule -- that card has one because it says two things. SHORT job
  // names, the card being one size; the full name was shown twice already.
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
