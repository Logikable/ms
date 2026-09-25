#include "src/frontend/screens/symbol_combine_panel.h"

#include <algorithm>
#include <cstdint>
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
  // Where the EXP ends up if the player confirms. It can go past the level's
  // requirement, since the overflow isn't lost: it goes toward the next level.
  int after = exp_;
  int taken = static_cast<int>(std::min<int64_t>(
      selector_.value(), static_cast<int64_t>(spare_worths_.size())));
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
