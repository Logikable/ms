#include "src/frontend/stack_panel.h"

#include <string>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/frontend/panel_util.h"
#include "src/item.h"

namespace ms {

StackPanel::StackPanel(const CharacterInstance& character)
    : character_(character) {
}

ftxui::Element StackPanel::Render() const {
  const std::vector<StackableItem>& stacks = character_.stackables();
  if (stacks.empty()) {
    return ThemedWindow(" Items ", ftxui::text("(empty)"));
  }
  std::vector<ftxui::Element> rows;
  for (const StackableItem& stack : stacks) {
    rows.push_back(ftxui::text(PadRight(stack.name(), 24) + "x" +
                               std::to_string(stack.count())));
  }
  return ThemedWindow(" Items ", ftxui::vbox(std::move(rows)));
}

}  // namespace ms
