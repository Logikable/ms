#include "src/frontend/screens/hyper_stat_level_panel.h"

#include <string>
#include <utility>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/confirm_prompt.h"
#include "src/frontend/widgets/panel_util.h"

namespace ms {

void HyperStatLevelPanel::Reset(const std::string& stat_name, int level,
                                int cost, int points) {
  stat_name_ = stat_name;
  level_ = level;
  cost_ = cost;
  points_ = points;
  // On [Confirm]: the points are the player's to spend and the reset that
  // takes them back is free, so there is nothing here to be careful of.
  confirm_.Open(/*cancel_selected=*/false);
}

ftxui::Element HyperStatLevelPanel::Render() const {
  // Red on the count rather than on the price: what the player is short of is
  // points, and the greyed button below is the door that closes.
  ftxui::Element current = RedUnless(
      ftxui::text("Current Points: " + std::to_string(points_)), affordable());
  return DialogWindow(
      " Hyper Stat Level Up ",
      {
          CenteredRow(stat_name_),
          ThemedSeparator(),
          CenteredRow("Lv " + std::to_string(level_) + " → Lv " +
                      std::to_string(level_ + 1)),
          CenteredRow(std::move(current)),
          CenteredRow("Required Points: " + std::to_string(cost_)),
      },
      ConfirmButtons(confirm_.focus(), affordable()));
}

ConfirmChoice HyperStatLevelPanel::OnEvent(ftxui::Event event) {
  return confirm_.OnEvent(std::move(event), affordable());
}

}  // namespace ms
