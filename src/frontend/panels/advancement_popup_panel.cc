#include "src/frontend/panels/advancement_popup_panel.h"

#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/panel_util.h"
#include "src/protos/character.pb.h"

namespace ms {

ftxui::Element AdvancementPopupPanel(Job from_job, Job to_job) {
  return AccentWindow(" Advancement ",
                      ftxui::vbox({
                          CenteredRow(JobName(from_job)),
                          CenteredRow("↓"),
                          CenteredRow(JobName(to_job)),
                      }),
                      kYellow);
}

}  // namespace ms
