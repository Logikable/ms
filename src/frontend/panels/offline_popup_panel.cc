#include "src/frontend/panels/offline_popup_panel.h"

#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/combat/combat.h"
#include "src/combat/offline.h"
#include "src/frontend/widgets/chrome.h"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/format.h"

namespace ms {
namespace {

// One item and how many of it came back, with what the bag could not hold
// called out beside it. The count is left on even at one: what a stretch of
// farming yielded is a quantity, not a name.
ftxui::Element ItemRow(const RewardItem& item) {
  std::string got = item.name + " x" + FormatWithCommas(item.count);
  if (item.discarded <= 0) {
    return CenteredRow(got);
  }
  return CenteredRow(ftxui::hbox({
      ftxui::text(got),
      ftxui::text(" (" + FormatWithCommas(item.discarded) + " lost)") |
          ftxui::color(kRed),
  }));
}

}  // namespace

std::string FormatAbsence(double seconds) {
  int64_t whole = static_cast<int64_t>(seconds);
  int64_t days = whole / 86400;
  int64_t hours = whole % 86400 / 3600;
  int64_t minutes = whole % 3600 / 60;
  if (days > 0) {
    return std::to_string(days) + "d " + std::to_string(hours) + "h";
  }
  if (hours > 0) {
    return std::to_string(hours) + "h " + std::to_string(minutes) + "m";
  }
  if (minutes > 0) {
    return std::to_string(minutes) + "m";
  }
  return std::to_string(whole % 60) + "s";
}

ftxui::Element OfflinePopupPanel(const OfflineReport& report,
                                 ftxui::Element prompt) {
  std::vector<ftxui::Element> rows;
  rows.push_back(CenteredRow("Away for " + FormatAbsence(report.absence)));
  rows.push_back(AccentSeparator(kTheme));
  if (!report.farmed) {
    // Left standing in town, or with nothing to swing. Not a failure worth a
    // red line -- just a player who logged off somewhere quiet.
    rows.push_back(CenteredRow("You were not fighting anywhere."));
  } else {
    rows.push_back(CenteredRow(FormatWithCommas(report.kills) + " kills on " +
                               report.map_name));
    if (report.rewards.exp > 0) {
      rows.push_back(
          CenteredRow(FormatWithCommas(report.rewards.exp) + " EXP"));
    }
    if (report.end_level > report.start_level) {
      rows.push_back(CenteredRow("Level " + std::to_string(report.start_level) +
                                 " -> " + std::to_string(report.end_level)));
    }
    if (report.rewards.meso > 0) {
      rows.push_back(CenteredRow(FormatMeso(report.rewards.meso)));
    }
    for (const RewardItem& item : report.rewards.items) {
      rows.push_back(ItemRow(item));
    }
    if (report.died) {
      // The one line that is a reason rather than a reward: the farming
      // stopped here, and the player is somewhere else now.
      rows.push_back(AccentSeparator(kTheme));
      rows.push_back(CenteredRow(
          ftxui::text("Defeated after " + FormatAbsence(report.seconds) +
                      " and sent back to Maple Island.") |
          ftxui::color(kRed)));
    }
  }
  rows.push_back(AccentSeparator(kTheme));
  rows.push_back(CenteredRow(std::move(prompt)));
  // A floor rather than a fit, as the celebration cards are: centring shrinks
  // a window back to its content.
  return AccentWindow(" Welcome Back ",
                      ftxui::vbox(std::move(rows)) |
                          ftxui::size(ftxui::WIDTH, ftxui::GREATER_THAN,
                                      kCelebrationContentWidth),
                      kTheme);
}

}  // namespace ms
