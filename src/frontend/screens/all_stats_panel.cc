#include "src/frontend/screens/all_stats_panel.h"

#include <string>
#include <utility>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/panel_util.h"
#include "src/frontend/widgets/stat_rows.h"

namespace ms {
namespace {

// One stat in one column. A blank line renders as blank space, which is what
// squares off a row with an odd number of stats in it.
//
// The value's RIGHT edge is what is fixed, and the gap before it gives way --
// not the label's width. Defense is the one stat written "(base+bonus) total",
// so it is the only one that outgrows a value column, and padding the label to
// a fixed 16 left it nothing to give: the value ran into the gutter and out
// past every other value on the screen.
std::string ColumnText(const StatLine& line) {
  if (line.label.empty()) {
    return std::string(AllStatsPanel::kColumnWidth, ' ');
  }
  int gap =
      AllStatsPanel::kColumnWidth - 2 - static_cast<int>(line.value.size());
  // PadRight truncates, so a value long enough to reach the label cuts into it
  // rather than breaking the column the whole screen is built on.
  return " " + PadRight(line.label, std::max(0, gap)) + line.value + " ";
}

}  // namespace

AllStatsPanel::AllStatsPanel(const CharacterInstance& character,
                             const std::map<std::string, Skill>& skills)
    : character_(character), skills_(skills) {
}

ftxui::Element AllStatsPanel::Pairs(const std::vector<StatLine>& lines) {
  std::vector<ftxui::Element> rows;
  for (size_t i = 0; i < lines.size(); i += 2) {
    StatLine right = i + 1 < lines.size() ? lines[i + 1] : StatLine();
    rows.push_back(ftxui::text(ColumnText(lines[i]) + ColumnText(right)));
  }
  return ftxui::vbox(std::move(rows));
}

ftxui::Element AllStatsPanel::Render() const {
  const Character& p = character_.proto();
  // The same heading the Character panel carries, name row included, so the
  // screen behind it reads as the same character rather than as a table of
  // numbers.
  std::string lvl = PadLeft(std::to_string(p.level()), 3);
  return ThemedWindow(" Character ",
                      ftxui::vbox({
                          CenteredRow(character_.username()),
                          CenteredRow("Lv" + lvl + " " + ShortJobName(p.job())),
                          CenteredRow(CombatPowerText(
                              CharacterCombatPower(character_, skills_))),
                          ThemedSeparator(),
                          Pairs(MainStatLines(character_, skills_)),
                          ThemedSeparator(),
                          Pairs(ExtraStatLines(character_, skills_)),
                      }));
}

}  // namespace ms
