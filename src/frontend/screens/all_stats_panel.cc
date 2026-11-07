#include "src/frontend/screens/all_stats_panel.h"

#include <algorithm>
#include <optional>
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
ftxui::Element ColumnText(const StatLine& line, bool selected) {
  if (line.label.empty()) {
    return ftxui::text(std::string(AllStatsPanel::kColumnWidth, ' '));
  }
  int gap =
      AllStatsPanel::kColumnWidth - 2 - static_cast<int>(line.value.size());
  // PadRight truncates, so a value long enough to reach the label cuts into it
  // instead of breaking the column layout the whole screen depends on.
  std::string padded = PadRight(line.label, std::max(0, gap));
  if (!selected || padded.size() < line.label.size()) {
    return ftxui::text(" " + padded + line.value + " ");
  }
  // Only the name inverts, as on the Stats tab.
  return ftxui::hbox({
      ftxui::text(" "),
      ftxui::text(line.label) | ftxui::inverted,
      ftxui::text(padded.substr(line.label.size()) + line.value + " "),
  });
}

// Where each stat is drawn: MainStatLines fills the left column before the
// right, and Attack and Magic Attack head the extras' left column.
struct GridCell {
  int row;
  int col;
};
GridCell CellOf(InspectStat stat) {
  switch (stat) {
    case InspectStat::kStr:
      return {0, 0};
    case InspectStat::kDex:
      return {1, 0};
    case InspectStat::kInt:
      return {0, 1};
    case InspectStat::kLuk:
      return {1, 1};
    case InspectStat::kAttack:
      return {2, 0};
    case InspectStat::kMagicAttack:
      return {3, 0};
  }
  return {0, 0};
}
constexpr int kGridRows = 4;

// The stat at `row`, in `col` or the nearest column left of it that row has.
InspectStat StatAt(int row, int col) {
  for (int c = col; c >= 0; --c) {
    for (int i = 0; i < kNumInspectStats; ++i) {
      const InspectStat stat = static_cast<InspectStat>(i);
      if (CellOf(stat).row == row && CellOf(stat).col == c) {
        return stat;
      }
    }
  }
  return InspectStat::kStr;
}

}  // namespace

AllStatsPanel::AllStatsPanel(const CharacterInstance& character,
                             const AccountInstance* account,
                             const AccountInstance& reader,
                             const std::map<std::string, Skill>& skills)
    : character_(character),
      account_(account),
      reader_(reader),
      skills_(skills) {
}

bool AllStatsPanel::ShowsFarmBoss() const {
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

bool AllStatsPanel::ShowsDrop() const {
  return account_ != nullptr &&
         Unlocked(Feature::kEquipPresets, character_, *account_);
}

std::vector<AllStatsPanel::View> AllStatsPanel::Views() const {
  std::vector<View> views;
  if (ShowsFarmBoss()) {
    views.push_back({"Farm", Activity::kFarming, std::nullopt});
    views.push_back({"Boss", Activity::kBossing, std::nullopt});
  } else {
    // One allocation for everything: what the character wears.
    views.push_back({"Worn", preset_, std::nullopt});
  }
  // Bossing with the Drop preset's gear, as the boss drop roll reads it.
  if (ShowsDrop()) {
    views.push_back({"Drop", Activity::kBossing, kDropPreset});
  }
  return views;
}

AllStatsPanel::View AllStatsPanel::Current() const {
  std::vector<View> views = Views();
  return views[std::clamp(tab_, 0, static_cast<int>(views.size()) - 1)];
}

bool AllStatsPanel::ShowsPresetBar() const {
  return Views().size() > 1;
}

void AllStatsPanel::SetPreset(Activity preset) {
  preset_ = preset;
  tab_ = ShowsFarmBoss() && preset == Activity::kBossing ? 1 : 0;
  // Called as the screen opens, which starts the cursor at the top.
  on_tabs_ = true;
  stat_ = InspectStat::kStr;
}

std::optional<StatPreset> AllStatsPanel::gear() const {
  return Current().gear;
}

bool AllStatsPanel::OnTabs() const {
  return on_tabs_ && ShowsPresetBar();
}

std::optional<InspectStat> AllStatsPanel::selected_stat() const {
  if (OnTabs()) {
    return std::nullopt;
  }
  return stat_;
}

Activity AllStatsPanel::preset() const {
  return Current().activity;
}

bool AllStatsPanel::showing_drop() const {
  return Current().gear.has_value();
}

bool AllStatsPanel::OnEvent(const ftxui::Event& event) {
  const bool left = event == ftxui::Event::ArrowLeft;
  const bool right = event == ftxui::Event::ArrowRight;
  if (OnTabs() && (left || right)) {
    // Stops at the ends, like every tab bar in the game.
    const int last = static_cast<int>(Views().size()) - 1;
    tab_ =
        left ? std::max(0, std::min(tab_, last) - 1) : std::min(last, tab_ + 1);
    return true;
  }
  const GridCell at = CellOf(stat_);
  if (left || right) {
    // Stops at the ends too; a row with one stat keeps it.
    stat_ = StatAt(at.row, std::clamp(at.col + (right ? 1 : -1), 0, 1));
    return true;
  }
  const bool up = event == ftxui::Event::ArrowUp;
  if (!up && event != ftxui::Event::ArrowDown) {
    return false;
  }
  // One ring: the preset row, if drawn, then the grid's rows. The column is
  // kept where the next row has it.
  const bool bar = ShowsPresetBar();
  const bool was_on_tabs = OnTabs();
  const int stops = kGridRows + (bar ? 1 : 0);
  int stop = was_on_tabs ? 0 : at.row + (bar ? 1 : 0);
  stop = (stop + (up ? stops - 1 : 1)) % stops;
  on_tabs_ = bar && stop == 0;
  if (!on_tabs_) {
    stat_ = StatAt(stop - (bar ? 1 : 0), was_on_tabs ? 0 : at.col);
  }
  return true;
}

ftxui::Element AllStatsPanel::Pairs(const std::vector<StatLine>& lines,
                                    int highlight) {
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
      const size_t r = start + left + i;
      if (r < end) {
        right = lines[r];
      }
      const int l = static_cast<int>(start + i);
      rows.push_back(ftxui::hbox({
          ColumnText(lines[l], l == highlight),
          ColumnText(right, r < end && static_cast<int>(r) == highlight),
      }));
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
  const View view = Current();
  std::vector<ftxui::Element> rows = {
      CenteredRow(character_.username()),
      CenteredRow("Lv" + lvl + " " + ShortJobName(p.job())),
      CenteredRow(CombatPowerText(ShownCombatPower(reader_, character_, skills_,
                                                   view.activity, view.gear))),
      ThemedSeparator(),
  };
  // Between the heading and the stats, so it reads as a heading of its own:
  // whose numbers these are. The row has the screen's only cursor, so it is
  // drawn focused.
  if (ShowsPresetBar()) {
    std::vector<TabSpec> specs;
    for (const View& tab : Views()) {
      specs.push_back({tab.label});
    }
    const int active = std::clamp(tab_, 0, static_cast<int>(specs.size()) - 1);
    rows.push_back(ftxui::hbox({
        TabBar(specs, active, /*row_focused=*/OnTabs(), kContentWidth),
        ftxui::filler(),
    }));
    rows.push_back(ThemedSeparator());
  }
  // MainStatLines lists STR to LUK in InspectStat's order, and the extras
  // start with Attack and Magic Attack.
  const std::optional<InspectStat> lit = selected_stat();
  const int main_lit =
      lit && *lit <= InspectStat::kLuk ? static_cast<int>(*lit) : -1;
  const int extra_lit =
      lit && *lit >= InspectStat::kAttack
          ? static_cast<int>(*lit) - static_cast<int>(InspectStat::kAttack)
          : -1;
  rows.push_back(Pairs(
      MainStatLines(character_, skills_, view.activity, view.gear), main_lit));
  rows.push_back(ThemedSeparator());
  rows.push_back(
      Pairs(ExtraStatLines(character_, skills_, view.activity, view.gear),
            extra_lit));
  return ftxui::vbox(std::move(rows));
}

ftxui::Element AllStatsPanel::Render() const {
  return ThemedWindow(" Character ", RenderBody());
}

}  // namespace ms
