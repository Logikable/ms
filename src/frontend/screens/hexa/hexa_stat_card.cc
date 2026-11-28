#include "src/frontend/screens/hexa/hexa_stat_card.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/character/matrix/hexa_stat.h"
#include "src/frontend/widgets/chrome.h"
#include "src/frontend/widgets/text/colors.h"
#include "src/frontend/widgets/text/format.h"
#include "src/frontend/widgets/text/game_names.h"
#include "src/frontend/widgets/text/text_columns.h"
#include "src/protos/character.pb.h"

namespace ms {
namespace {

constexpr char kChanceHeading[] = "Chance";
constexpr char kLevelHeading[] = "Level";
// "35.0%" and "10/10" both fit under their headings.
constexpr int kChanceWidth = 6;
constexpr int kLevelWidth = 5;
constexpr int kGap = 2;

std::string StatText(HexaStat stat, int level, bool primary,
                     StatField main_stat) {
  const std::string bonus = HexaStatBonusText(stat, level, primary);
  return HexaStatName(stat, main_stat) + (bonus.empty() ? "" : " " + bonus);
}

// Every stat at every level in both places, so the columns sit in the same
// place on every card.
int StatWidth(StatField main_stat) {
  int widest = 0;
  for (int i = HEXA_STAT_UNSPECIFIED + 1; i <= HexaStat_MAX; ++i) {
    for (int level = 1; level <= kHexaStatMaxLevel; ++level) {
      for (bool primary : {true, false}) {
        widest =
            std::max(widest, TextColumns(StatText(static_cast<HexaStat>(i),
                                                  level, primary, main_stat)));
      }
    }
  }
  return widest;
}

// A full node rolls nothing, so it shows a dash rather than three 0.0%s; a
// single stat at 10 in a node still rolling reads 0.0%.
std::string ChanceText(double chance, bool node_full) {
  if (node_full) {
    return "—";
  }
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "%.1f%%", chance * 100.0);
  return buffer;
}

ftxui::Element Columns(ftxui::Element stat, int stat_width,
                       const std::string& chance, const std::string& level) {
  return ftxui::hbox({
      std::move(stat) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, stat_width),
      ftxui::text(std::string(kGap, ' ') + PadLeft(chance, kChanceWidth) +
                  std::string(kGap, ' ') + PadLeft(level, kLevelWidth) + " "),
  });
}

ftxui::Element StatRow(bool primary, HexaStat stat, int level, double chance,
                       bool node_full, StatField main_stat, int stat_width) {
  ftxui::Element name = ftxui::hbox({
      ftxui::text(" "),
      ftxui::text(primary ? "M: " : "A: ") |
          ftxui::color(primary ? kGold : kGreen),
      ftxui::text(StatText(stat, level, primary, main_stat)),
  });
  return Columns(
      std::move(name), stat_width, ChanceText(chance, node_full),
      std::to_string(level) + "/" + std::to_string(kHexaStatMaxLevel));
}

}  // namespace

ftxui::Element HexaStatCard(int index, const HexaStatNode& node,
                            StatField main_stat) {
  // " M: " before the widest stat.
  const int stat_width = 4 + StatWidth(main_stat);
  const bool full = HexaStatNodeLevels(node) >= kHexaStatNodeLevels;
  const HexaStatChances chances = HexaStatLevelChances(node);
  std::vector<ftxui::Element> rows = {
      Columns(ftxui::text("    Stat"), stat_width, kChanceHeading,
              kLevelHeading) |
          ftxui::color(kTheme),
      ThemedSeparator(),
      StatRow(true, node.primary(), node.primary_level(), chances.primary, full,
              main_stat, stat_width),
      StatRow(false, node.first(), node.first_level(), chances.first, full,
              main_stat, stat_width),
      StatRow(false, node.second(), node.second_level(), chances.second, full,
              main_stat, stat_width),
      ThemedSeparator(),
      CenteredRow(ContinueButton("Close")),
  };
  return ThemedWindow(" HEXA Stat " + HexaStatNodeNumeral(index) + " ",
                      ftxui::vbox(std::move(rows)));
}

}  // namespace ms
