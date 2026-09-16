#include "src/frontend/screens/symbol_combine_panel.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/chrome.h"

namespace ms {

void SymbolCombinePanel::Reset(const std::string& symbol_name, int level,
                               int exp, int needed,
                               std::vector<int> spare_worths) {
  symbol_name_ = symbol_name;
  level_ = level;
  exp_ = exp;
  needed_ = needed;
  spare_worths_ = std::move(spare_worths);
  selector_.Reset(static_cast<int>(spare_worths_.size()));
}

ftxui::Element SymbolCombinePanel::Render() const {
  // Where the EXP lands if the player confirms. Allowed past the rung, since
  // what spills over is not lost: it is what the level after that is paid in.
  int after = exp_;
  int taken =
      std::min(selector_.value(), static_cast<int>(spare_worths_.size()));
  for (int i = 0; i < taken; ++i) {
    after += spare_worths_[i];
  }
  ftxui::Element content = ftxui::vbox({
      CenteredRow(symbol_name_),
      ThemedSeparator(),
      CenteredRow("Level " + std::to_string(level_)),
      CenteredRow("EXP " + std::to_string(after) + " / " +
                  std::to_string(needed_)),
      ThemedSeparator(),
      selector_.Render(),
  });
  return ThemedWindow(" Combine ", std::move(content));
}

ConfirmChoice SymbolCombinePanel::OnEvent(ftxui::Event event) {
  return selector_.OnEvent(std::move(event));
}

}  // namespace ms
