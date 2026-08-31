#include "src/frontend/screens/symbol_combine_panel.h"

#include <algorithm>
#include <string>
#include <utility>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/chrome.h"

namespace ms {

void SymbolCombinePanel::Reset(const std::string& symbol_name, int level,
                               int exp, int needed, int spares) {
  symbol_name_ = symbol_name;
  level_ = level;
  exp_ = exp;
  needed_ = needed;
  selector_.Reset(spares);
}

ftxui::Element SymbolCombinePanel::Render() const {
  // Where the EXP lands if the player confirms, held to the rung: the row
  // reads as a bar filling rather than as a total that can overshoot it. What
  // spills over is not lost -- it carries into the next level.
  int after = std::min(needed_, exp_ + selector_.value());
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
