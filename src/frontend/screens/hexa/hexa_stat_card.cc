#include "src/frontend/screens/hexa/hexa_stat_card.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/character/matrix/hexa_stat.h"
#include "src/frontend/widgets/chrome.h"
#include "src/frontend/widgets/text/colors.h"
#include "src/frontend/widgets/text/game_names.h"
#include "src/protos/character.pb.h"

namespace ms {
namespace {

// The widest name, so the levels line up in one column on every card.
int NameWidth(StatField main_stat) {
  int widest = 0;
  for (int i = HEXA_STAT_UNSPECIFIED + 1; i <= HexaStat_MAX; ++i) {
    widest = std::max(
        widest, static_cast<int>(
                    HexaStatName(static_cast<HexaStat>(i), main_stat).size()));
  }
  return widest;
}

ftxui::Element StatRow(bool primary, HexaStat stat, int level,
                       StatField main_stat) {
  return ftxui::hbox({
      ftxui::text(" "),
      ftxui::text(primary ? "M: " : "A: ") |
          ftxui::color(primary ? kGold : kGreen),
      ftxui::text(HexaStatName(stat, main_stat)),
      ftxui::filler(),
      ftxui::text("  " + std::to_string(level) + "/" +
                  std::to_string(kHexaStatMaxLevel) + " "),
  });
}

}  // namespace

ftxui::Element HexaStatCard(int index, const HexaStatNode& node,
                            StatField main_stat) {
  // " M: ", the widest name, and the widest level, "  10/10 ".
  const int width = 4 + NameWidth(main_stat) + 8;
  std::vector<ftxui::Element> rows = {
      CenteredRow(HexaStatNodeNumeral(index)),
      ThemedSeparator(),
      StatRow(true, node.primary(), node.primary_level(), main_stat),
      StatRow(false, node.first(), node.first_level(), main_stat),
      StatRow(false, node.second(), node.second_level(), main_stat),
      ThemedSeparator(),
      CenteredRow(ContinueButton("Close")),
  };
  return ThemedWindow(" HEXA Stat ",
                      ftxui::vbox(std::move(rows)) |
                          ftxui::size(ftxui::WIDTH, ftxui::EQUAL, width));
}

}  // namespace ms
