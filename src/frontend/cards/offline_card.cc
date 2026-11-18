#include "src/frontend/cards/offline_card.h"

#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/combat/combat.h"
#include "src/combat/offline.h"
#include "src/frontend/widgets/chrome.h"
#include "src/frontend/widgets/text/colors.h"
#include "src/frontend/widgets/text/format.h"

namespace ms {
namespace {

// One item and how many came back, with what didn't fit in the bag shown beside
// it. The count is shown even at one, since farming results are quantities.
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

namespace {

// Levels first: gaining one is the news, and everything below is what the climb
// was made of.
std::vector<ftxui::Element> GrowthRows(const OfflineReport& report,
                                       bool show_familiars) {
  std::vector<ftxui::Element> growth;
  if (report.end_level > report.start_level) {
    growth.push_back(CenteredRow("Level " + std::to_string(report.start_level) +
                                 " -> " + std::to_string(report.end_level)));
  }
  if (report.rewards.exp > 0) {
    growth.push_back(
        CenteredRow(FormatWithCommas(report.rewards.exp) + " EXP"));
  }
  if (report.rewards.familiar_exp > 0 && show_familiars) {
    growth.push_back(CenteredRow(FormatWithCommas(report.rewards.familiar_exp) +
                                 " Familiar EXP"));
  }
  return growth;
}

std::vector<ftxui::Element> PurseRows(const OfflineReport& report,
                                      bool show_honor) {
  std::vector<ftxui::Element> purse;
  // Meso is shown before potions are paid for, with what they took beside it,
  // so the drop rate's work isn't hidden in a net figure.
  if (report.rewards.meso > 0 || report.rewards.consumable_cost > 0) {
    std::string meso = FormatMeso(report.rewards.meso);
    if (report.rewards.consumable_cost > 0) {
      meso += " (-" + FormatWithCommas(report.rewards.consumable_cost) +
              " potions)";
    }
    purse.push_back(CenteredRow(meso));
  }
  if (report.rewards.honor > 0 && show_honor) {
    purse.push_back(
        CenteredRow(FormatWithCommas(report.rewards.honor) + " Honor"));
  }
  // No explanation for this one: only 5th jobs get V Points, so anyone who has
  // some already knows what they are.
  if (report.rewards.v_points > 0) {
    purse.push_back(
        CenteredRow(FormatWithCommas(report.rewards.v_points) + " V Points"));
  }
  return purse;
}

// The fight, the character's growth, the currencies, the loot, then why
// farming stopped: each group under its own divider, and an empty one draws
// none.
void AppendFarmed(const OfflineReport& report, bool show_honor,
                  bool show_familiars, std::vector<ftxui::Element>& rows) {
  std::vector<std::vector<ftxui::Element>> groups;
  groups.push_back({CenteredRow(FormatWithCommas(report.kills) + " kills on " +
                                report.map_name)});
  groups.push_back(GrowthRows(report, show_familiars));
  groups.push_back(PurseRows(report, show_honor));
  std::vector<ftxui::Element>& loot = groups.emplace_back();
  for (const RewardItem& item : report.rewards.items) {
    loot.push_back(ItemRow(item));
  }
  if (report.died) {
    // The only line that is a reason rather than a reward: farming stopped
    // here, and the player is now somewhere else.
    groups.push_back({CenteredRow(
        ftxui::text("Defeated after " + FormatAbsence(report.seconds) +
                    " and sent back to Maple Island.") |
        ftxui::color(kRed))});
  }
  bool first = true;
  for (std::vector<ftxui::Element>& group : groups) {
    if (group.empty()) {
      continue;
    }
    if (!first) {
      rows.push_back(AccentSeparator(kTheme));
    }
    first = false;
    for (ftxui::Element& row : group) {
      rows.push_back(std::move(row));
    }
  }
}

}  // namespace

ftxui::Element OfflineCard(const OfflineReport& report, ftxui::Element prompt,
                           bool show_honor, bool show_familiars) {
  std::vector<ftxui::Element> rows;
  rows.push_back(CenteredRow("Away for " + FormatAbsence(report.absence)));
  rows.push_back(AccentSeparator(kTheme));
  if (!report.farmed) {
    // Left in town, or with no weapon. Not a failure worth a red line, just a
    // player who logged off somewhere quiet.
    rows.push_back(CenteredRow("You were not fighting anywhere."));
  } else {
    AppendFarmed(report, show_honor, show_familiars, rows);
  }
  rows.push_back(AccentSeparator(kTheme));
  rows.push_back(CenteredRow(std::move(prompt)));
  // A minimum width rather than fitting the content, as with the celebration
  // cards: centring shrinks a window to its content.
  return AccentWindow(" Welcome Back ",
                      ftxui::vbox(std::move(rows)) |
                          ftxui::size(ftxui::WIDTH, ftxui::GREATER_THAN,
                                      kCelebrationContentWidth),
                      kTheme);
}

}  // namespace ms
