#include "src/frontend/screens/boss_clear_panel.h"

#include <string>
#include <utility>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/combat/boss_run.h"
#include "src/frontend/widgets/chrome.h"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/format.h"

namespace ms {
namespace {

// One drop, and how many of it came out. The count is left off a lone one:
// "Zakum's Soul Shard x1" reads as a quantity somebody chose.
std::string DropLine(const BossRewardItem& item) {
  if (item.count <= 1) {
    return item.name;
  }
  return item.name + " x" + std::to_string(item.count);
}

}  // namespace

ftxui::Element BossClearPanel(const std::string& title,
                              const BossReward& reward, ftxui::Element prompt,
                              bool show_honor) {
  std::vector<ftxui::Element> rows;
  rows.push_back(CenteredRow(title));
  rows.push_back(AccentSeparator(kYellow));
  // The meso first: it is the one line every clear pays, so the drops under it
  // read as what this run happened to give.
  if (reward.meso > 0) {
    rows.push_back(CenteredRow(FormatWithCommas(reward.meso) + " meso"));
  }
  if (reward.exp > 0) {
    rows.push_back(CenteredRow(FormatWithCommas(reward.exp) + " EXP"));
  }
  if (reward.honor > 0 && show_honor) {
    rows.push_back(CenteredRow(FormatWithCommas(reward.honor) + " Honor"));
  }
  for (const BossRewardItem& item : reward.items) {
    rows.push_back(CenteredRow(DropLine(item)));
  }
  if (rows.size() == 2) {
    rows.push_back(EmptyState("no rewards"));
  }
  rows.push_back(AccentSeparator(kYellow));
  rows.push_back(CenteredRow(std::move(prompt)));
  // A floor rather than a fit, as the level-up card is: centring shrinks a
  // window back to its content, and a card the width of "Cleared!" is not one.
  return AccentWindow(" Cleared ",
                      ftxui::vbox(std::move(rows)) |
                          ftxui::size(ftxui::WIDTH, ftxui::GREATER_THAN,
                                      kCelebrationContentWidth),
                      kYellow);
}

}  // namespace ms
