#include "src/frontend/screens/all_stats_panel.h"

#include <string>
#include <utility>
#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/character/character_stats.h"
#include "src/character/job_name.h"
#include "src/character/progression.h"
#include "src/frontend/widgets/chrome.h"
#include "src/frontend/widgets/format.h"
#include "src/frontend/widgets/stat_rows.h"

namespace ms {
namespace {

// One stat in one column. A blank line is blank space, which evens out a row
// with an odd number of stats. The value's right edge is fixed and the gap
// before it shrinks. Attack is shown as "(base+bonus) total", so padding the
// label would leave it no room and push it into the gutter.
std::string ColumnText(const StatLine& line) {
  if (line.label.empty()) {
    return std::string(AllStatsPanel::kColumnWidth, ' ');
  }
  int gap =
      AllStatsPanel::kColumnWidth - 2 - static_cast<int>(line.value.size());
  // PadRight truncates, so a value long enough to reach the label cuts into it
  // instead of breaking the column layout the whole screen depends on.
  return " " + PadRight(line.label, std::max(0, gap)) + line.value + " ";
}

}  // namespace

AllStatsPanel::AllStatsPanel(const CharacterInstance& character,
                             const AccountInstance* account,
                             const std::map<std::string, Skill>& skills)
    : character_(character), account_(account), skills_(skills) {
}

bool AllStatsPanel::ShowsPresetBar() const {
  // Nothing to pick between while the character uses one allocation for
  // everything, so the screen shows the one in use. This is the character's own
  // setting, not the reader's, since a party member's sheet includes it.
  if (!character_.autoswap_presets()) {
    return false;
  }
  // The player's own screen checks the account, which knows about their other
  // characters. A party member's sheet is all we have of them, so their own
  // level decides.
  if (account_ == nullptr) {
    return character_.proto().level() >= kHyperStatUnlockLevel;
  }
  return Unlocked(Feature::kHyperStats, character_, *account_);
}

bool AllStatsPanel::OnEvent(const ftxui::Event& event) {
  if (!ShowsPresetBar()) {
    return false;
  }
  // Stops at the ends, like every tab bar in the game.
  if (event == ftxui::Event::ArrowLeft) {
    preset_ = Activity::kFarming;
    return true;
  }
  if (event == ftxui::Event::ArrowRight) {
    preset_ = Activity::kBossing;
    return true;
  }
  return false;
}

ftxui::Element AllStatsPanel::Pairs(const std::vector<StatLine>& lines) {
  std::vector<ftxui::Element> rows;
  // Each group between rules fills its left column top to bottom before
  // starting the right one, so the list reads down a column instead of
  // zigzagging across the screen. A group with an odd count leaves the gap at
  // the bottom right, where nothing follows.
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
  // The same heading as the Character panel, name row included, so this screen
  // reads as the same character rather than a table of numbers.
  std::string lvl = PadLeft(std::to_string(p.level()), 3);
  std::vector<ftxui::Element> rows = {
      CenteredRow(character_.username()),
      CenteredRow("Lv" + lvl + " " + ShortJobName(p.job())),
      CenteredRow(
          CombatPowerText(CharacterCombatPower(character_, skills_, preset_))),
      ThemedSeparator(),
  };
  // Between the heading and the stats, so it reads as a heading of its own:
  // whose numbers these are. The row has the screen's only cursor, so it is
  // drawn focused.
  if (ShowsPresetBar()) {
    std::vector<TabSpec> specs = {{"Farm"}, {"Boss"}};
    int active = preset_ == Activity::kBossing ? 1 : 0;
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
