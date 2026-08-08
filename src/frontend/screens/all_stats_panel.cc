#include "src/frontend/screens/all_stats_panel.h"

#include <string>
#include <utility>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/panel_util.h"
#include "src/frontend/widgets/stat_rows.h"

namespace ms {
namespace {

// Seats "Critical Damage", the longest label here.
constexpr int kLabelWidth = 16;
// What is left of a column once the gutter either side and the label are paid.
constexpr int kValueWidth = AllStatsPanel::kColumnWidth - 2 - kLabelWidth;

// One stat in one column. A blank line renders as blank space, which is what
// squares off a row with an odd number of stats in it.
std::string ColumnText(const StatLine& line) {
  if (line.label.empty()) {
    return std::string(AllStatsPanel::kColumnWidth, ' ');
  }
  return " " + PadRight(line.label, kLabelWidth) +
         PadLeft(line.value, kValueWidth) + " ";
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
  // The same heading the Character panel carries, so the screen behind it
  // reads as the same character rather than as a table of numbers.
  std::string lvl = PadLeft(std::to_string(p.level()), 3);
  return ThemedWindow(" Character ",
                      ftxui::vbox({
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
