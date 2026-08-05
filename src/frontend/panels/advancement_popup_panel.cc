#include "src/frontend/panels/advancement_popup_panel.h"

#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/panel_util.h"
#include "src/protos/character.pb.h"

namespace ms {

ftxui::Element AdvancementPopupPanel(Job from_job, Job to_job) {
  // No width pinned here either, for the reason the level-up banner gives --
  // and taking the caller's width rather than naming one is also what keeps
  // the two the same size. They land in the same place, in the same gold,
  // seconds apart at level 10, and a pair that differed would read as two
  // unrelated things rather than one moment.
  return AccentWindow(" Advancement ",
                      ftxui::vbox({
                          CenteredRow(JobName(from_job)),
                          CenteredRow("↓"),
                          CenteredRow(JobName(to_job)),
                      }),
                      kYellow);
}

}  // namespace ms
