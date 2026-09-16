#include "src/frontend/screens/dailies_panel.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/chrome.h"
#include "src/frontend/widgets/format.h"
#include "src/frontend/widgets/text_columns.h"

namespace ms {
namespace {

// Columns between the longest name and the count beside it.
constexpr int kGap = 2;

}  // namespace

void DailiesPanel::Reset(std::vector<Reward> rewards) {
  rewards_ = std::move(rewards);
  confirm_.Open(/*cancel_selected=*/false);
}

ftxui::Element DailiesPanel::Render() const {
  int widest = 0;
  for (const Reward& reward : rewards_) {
    widest = std::max(widest, TextColumns(reward.name));
  }
  ftxui::Elements rows;
  rows.push_back(CenteredRow("Claim today's dailies?"));
  rows.push_back(ThemedSeparator());
  for (const Reward& reward : rewards_) {
    // A column of clearance at both ends, so neither the name nor the count
    // sits against the border.
    rows.push_back(ftxui::text(" " + PadRight(reward.name, widest) +
                               std::string(kGap, ' ') + "x" +
                               std::to_string(reward.count) + " "));
  }
  return DialogWindow(" Dailies ", std::move(rows),
                      ConfirmButtons(confirm_.focus()));
}

ConfirmChoice DailiesPanel::OnEvent(ftxui::Event event) {
  return confirm_.OnEvent(std::move(event));
}

}  // namespace ms
