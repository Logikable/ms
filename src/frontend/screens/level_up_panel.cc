#include "src/frontend/screens/level_up_panel.h"

#include <string>
#include <utility>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/chrome.h"
#include "src/frontend/widgets/confirm_prompt.h"

namespace ms {

void LevelUpPanel::Reset(const std::string& name, int level,
                         const std::string& price, bool affordable) {
  name_ = name;
  level_ = level;
  price_ = price;
  affordable_ = affordable;
  // On [Confirm]: a symbol's duplicates are already spent, so the player is
  // finishing something they started rather than giving up anything new.
  confirm_.Open(/*cancel_selected=*/false);
}

ftxui::Element LevelUpPanel::Render() const {
  // Red on a price that can't be covered: the reason is on the cell that
  // causes it, and the grey button below is what it blocks.
  ftxui::Element cost = RedUnless(ftxui::text("Cost " + price_), affordable_);
  return DialogWindow(" Level Up ",
                      {
                          CenteredRow(name_),
                          ThemedSeparator(),
                          CenteredRow("Level " + std::to_string(level_) +
                                      " → " + std::to_string(level_ + 1)),
                          CenteredRow(std::move(cost)),
                      },
                      ConfirmButtons(confirm_.focus(), affordable_));
}

ConfirmChoice LevelUpPanel::OnEvent(ftxui::Event event) {
  return confirm_.OnEvent(std::move(event), affordable_);
}

}  // namespace ms
