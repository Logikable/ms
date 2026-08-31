#include "src/frontend/screens/symbol_level_panel.h"

#include <cstdint>
#include <string>
#include <utility>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/chrome.h"
#include "src/frontend/widgets/confirm_prompt.h"
#include "src/frontend/widgets/format.h"

namespace ms {

void SymbolLevelPanel::Reset(const std::string& symbol_name, int level,
                             int64_t cost, int64_t meso) {
  symbol_name_ = symbol_name;
  level_ = level;
  cost_ = cost;
  meso_ = meso;
  // On [Confirm]: the duplicates are already spent, so this is the player
  // finishing something they started rather than parting with anything new.
  confirm_.Open(/*cancel_selected=*/false);
}

ftxui::Element SymbolLevelPanel::Render() const {
  // Red on a price the purse cannot cover: the reason sits on the cell that
  // carries it, and the greyed button below is the door it closes.
  ftxui::Element cost =
      RedUnless(ftxui::text("Cost " + FormatMeso(cost_)), affordable());
  return DialogWindow(" Level Up ",
                      {
                          CenteredRow(symbol_name_),
                          ThemedSeparator(),
                          CenteredRow("Level " + std::to_string(level_) +
                                      " → " + std::to_string(level_ + 1)),
                          CenteredRow(std::move(cost)),
                      },
                      ConfirmButtons(confirm_.focus(), affordable()));
}

ConfirmChoice SymbolLevelPanel::OnEvent(ftxui::Event event) {
  return confirm_.OnEvent(std::move(event), affordable());
}

}  // namespace ms
