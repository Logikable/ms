#include "src/frontend/screens/boss_clear_panel.h"

#include <algorithm>
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

// One drop and how many came out. The count is left off a single item, since
// "Zakum's Soul Shard x1" reads like a quantity someone chose.
std::string DropLine(const BossRewardItem& item) {
  if (item.count <= 1) {
    return item.name;
  }
  return item.name + " x" + std::to_string(item.count);
}

// Puts the least likely of `items` first, keeping items with the same drop rate
// in table order.
void RarestFirst(std::vector<const BossRewardItem*>& items) {
  std::stable_sort(items.begin(), items.end(),
                   [](const BossRewardItem* a, const BossRewardItem* b) {
                     return a->chance < b->chance;
                   });
}

}  // namespace

ftxui::Element BossClearPanel(const std::string& title, double seconds,
                              const BossReward& reward, ftxui::Element prompt,
                              bool show_honor) {
  std::vector<ftxui::Element> rows;
  rows.push_back(CenteredRow(title + " in " + FormatClock(seconds)));
  rows.push_back(AccentSeparator(kYellow));
  // Meso first, since every clear pays it, so the drops below read as what this
  // run happened to give.
  if (reward.meso > 0) {
    rows.push_back(CenteredRow(FormatWithCommas(reward.meso) + " meso"));
  }
  if (reward.exp > 0) {
    rows.push_back(CenteredRow(FormatWithCommas(reward.exp) + " EXP"));
  }
  if (reward.honor > 0 && show_honor) {
    rows.push_back(CenteredRow(FormatWithCommas(reward.honor) + " Honor"));
  }
  // The prizes go under their own rule, below what every clear pays, so a drop
  // the player was waiting for doesn't get lost among the numbers. Each group
  // starts with its rarest item, the reverse of the Fight panel's list.
  std::vector<const BossRewardItem*> paid;
  std::vector<const BossRewardItem*> prizes;
  for (const BossRewardItem& item : reward.items) {
    (item.prize ? prizes : paid).push_back(&item);
  }
  RarestFirst(paid);
  RarestFirst(prizes);
  for (const BossRewardItem* item : paid) {
    rows.push_back(CenteredRow(DropLine(*item)));
  }
  if (!prizes.empty() && rows.size() > 2) {
    rows.push_back(AccentSeparator(kYellow));
  }
  for (const BossRewardItem* item : prizes) {
    rows.push_back(CenteredRow(DropLine(*item)));
  }
  if (rows.size() == 2) {
    rows.push_back(EmptyState("no rewards"));
  }
  rows.push_back(AccentSeparator(kYellow));
  rows.push_back(CenteredRow(std::move(prompt)));
  // A minimum width rather than a fit, like the level-up card: centring shrinks
  // a window to its content, and a card only as wide as "Cleared!" looks wrong.
  return AccentWindow(" Cleared ",
                      ftxui::vbox(std::move(rows)) |
                          ftxui::size(ftxui::WIDTH, ftxui::GREATER_THAN,
                                      kCelebrationContentWidth),
                      kYellow);
}

}  // namespace ms
