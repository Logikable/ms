#include "src/frontend/screens/all_stats_panel.h"

#include <string>
#include <utility>
#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/character/job_name.h"
#include "src/character/progression.h"
#include "src/frontend/widgets/chrome.h"
#include "src/frontend/widgets/format.h"
#include "src/frontend/widgets/stat_rows.h"

namespace ms {
namespace {

// One stat in one column. A blank line renders as blank space, which is what
// squares off a row with an odd number of stats in it.
//
// The value's RIGHT edge is what is fixed, and the gap before it gives way --
// not the label's width. Attack is written "(base+bonus) total", so it can
// outgrow a value column, and padding the label to a fixed 16 left it nothing
// to give: the value ran into the gutter and out past every other value on the
// screen.
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
                             const AccountInstance* account,
                             const std::map<std::string, Skill>& skills)
    : character_(character), account_(account), skills_(skills) {
}

bool AllStatsPanel::ShowsPresetBar() const {
  return account_ != nullptr &&
         Unlocked(Feature::kHyperStats, character_, *account_);
}

bool AllStatsPanel::OnEvent(const ftxui::Event& event) {
  if (!ShowsPresetBar()) {
    return false;
  }
  // Clamped at the ends, as every tab bar in the game is.
  if (event == ftxui::Event::ArrowLeft) {
    preset_ = StatPreset::kFarming;
    return true;
  }
  if (event == ftxui::Event::ArrowRight) {
    preset_ = StatPreset::kBossing;
    return true;
  }
  return false;
}

ftxui::Element AllStatsPanel::Pairs(const std::vector<StatLine>& lines) {
  std::vector<ftxui::Element> rows;
  // Each group between rules fills its left column top to bottom before it
  // starts the right one, so the list is read down a column rather than
  // zigzagged across the screen. A group with an odd count leaves the gap at
  // the bottom right, where nothing follows it.
  for (size_t start = 0; start < lines.size();) {
    if (lines[start].rule) {
      rows.push_back(ThemedSeparator());
      ++start;
      continue;
    }
    size_t end = start;
    while (end < lines.size() && !lines[end].rule) {
      ++end;
    }
    size_t left = (end - start + 1) / 2;
    for (size_t i = 0; i < left; ++i) {
      StatLine right;
      if (start + left + i < end) {
        right = lines[start + left + i];
      }
      rows.push_back(
          ftxui::text(ColumnText(lines[start + i]) + ColumnText(right)));
    }
    start = end;
  }
  return ftxui::vbox(std::move(rows));
}

ftxui::Element AllStatsPanel::RenderBody() const {
  const Character& p = character_.proto();
  // The same heading the Character panel carries, name row included, so the
  // screen behind it reads as the same character rather than as a table of
  // numbers.
  std::string lvl = PadLeft(std::to_string(p.level()), 3);
  std::vector<ftxui::Element> rows = {
      CenteredRow(character_.username()),
      CenteredRow("Lv" + lvl + " " + ShortJobName(p.job())),
      CenteredRow(
          CombatPowerText(CharacterCombatPower(character_, skills_, preset_))),
      ThemedSeparator(),
  };
  // Between the heading and the stats, so it reads as a heading of its own:
  // whose numbers these are. The row holds the screen's only cursor, so it is
  // drawn focused.
  if (ShowsPresetBar()) {
    std::vector<TabSpec> specs = {{"Farm"}, {"Boss"}};
    int active = preset_ == StatPreset::kBossing ? 1 : 0;
    rows.push_back(ftxui::hbox({
        TabBar(specs, active, /*row_focused=*/true, kContentWidth),
        ftxui::filler(),
    }));
    rows.push_back(ThemedSeparator());
  }
  rows.push_back(Pairs(MainStatLines(character_, skills_, preset_)));
  rows.push_back(ThemedSeparator());
  rows.push_back(Pairs(ExtraStatLines(character_, skills_, preset_)));
  return ftxui::vbox(std::move(rows));
}

ftxui::Element AllStatsPanel::Render() const {
  return ThemedWindow(" Character ", RenderBody());
}

}  // namespace ms
