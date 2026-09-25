#include "src/frontend/screens/boss_analysis_panel.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/combat/damage_breakdown.h"
#include "src/frontend/placement.h"
#include "src/frontend/widgets/chrome.h"
#include "src/frontend/widgets/format.h"
#include "src/frontend/widgets/keys.h"

namespace ms {
namespace {

// Each column as wide as its widest value: a skill name, 9,999 trillion
// damage, 100.00%, 99,999 casts (Hurricane), 999 trillion a line, 99,999 lines.
// Players share the first three, so a name sits over a skill.
constexpr int kNameWidth = 28;
constexpr int kDamageWidth = 21;
constexpr int kShareWidth = 7;
constexpr int kCastsWidth = 6;
constexpr int kPerLineWidth = 15;
constexpr int kLinesWidth = 6;
constexpr char kGap[] = "  ";
constexpr char kCursorHere[] = "> ";
constexpr char kCursorAway[] = "  ";
constexpr int kRowWidth = 2 + kNameWidth + 2 + kDamageWidth + 2 + kShareWidth +
                          2 + kCastsWidth + 2 + kPerLineWidth + 2 + kLinesWidth;
// The row and the blank column inside the right border, where the table's
// scroll bar goes.
constexpr int kContentWidth = kRowWidth + 1;

// Rows a window spends on itself: two borders, a header and its divider.
constexpr int kTableChrome = 4;
constexpr int kTotalsHeight = 3;
// The Players panel spends the same, and the All row on top of that.
constexpr int kPlayersChrome = kTableChrome + 1;

std::string Damage(double damage) {
  return FormatWithCommas(std::llround(damage));
}

std::string Share(double share) {
  char text[16];
  std::snprintf(text, sizeof(text), "%.2f%%", 100.0 * share);
  return text;
}

// The first three columns, which the Players panel and the table share.
std::string LeadColumns(const std::string& name, const std::string& damage,
                        const std::string& share) {
  return PadRight(name, kNameWidth) + kGap + PadLeft(damage, kDamageWidth) +
         kGap + PadLeft(share, kShareWidth);
}

// `columns` behind the cursor's mark, padded out to the row: the caret only
// where the arrows are, the band wherever `banded`.
ftxui::Element Row(const std::string& columns, bool caret, bool banded) {
  std::string row = (caret ? kCursorHere : kCursorAway) + columns;
  return HighlightRow(ftxui::text(PadRight(row, kRowWidth)), banded);
}

ftxui::Element Header(const std::string& columns) {
  return ftxui::text(PadRight(kCursorAway + columns, kRowWidth));
}

ftxui::Element Fixed(ftxui::Element content) {
  return std::move(content) |
         ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kContentWidth);
}

}  // namespace

void BossAnalysisPanel::Open(std::vector<PlayerBreakdown> players,
                             double seconds) {
  for (PlayerBreakdown& player : players) {
    SortHeaviestFirst(player.rows);
  }
  std::stable_sort(players.begin(), players.end(),
                   [](const PlayerBreakdown& a, const PlayerBreakdown& b) {
                     return TotalDamage(a.rows) > TotalDamage(b.rows);
                   });
  players_ = std::move(players);
  player_totals_.clear();
  for (const PlayerBreakdown& player : players_) {
    player_totals_.push_back(TotalDamage(player.rows));
  }
  merged_ = MergeBreakdowns(players_);
  party_total_ = TotalDamage(merged_);
  seconds_ = seconds;
  table_focused_ = !party();
  player_cursor_ = 0;
  skill_cursor_ = 0;
}

const std::vector<BreakdownRow>& BossAnalysisPanel::table() const {
  if (player_cursor_ == 0) {
    return merged_;
  }
  return players_[player_cursor_ - 1].rows;
}

bool BossAnalysisPanel::OnEvent(const ftxui::Event& event) {
  if (IsSwitchPanel(event)) {
    if (party()) {
      table_focused_ = !table_focused_;
    }
    return true;
  }
  int delta = 0;
  if (event == ftxui::Event::ArrowUp) {
    delta = -1;
  } else if (event == ftxui::Event::ArrowDown) {
    delta = 1;
  } else {
    return false;
  }
  if (table_focused_) {
    int rows = static_cast<int>(table().size());
    if (rows > 0) {
      skill_cursor_ = StepCursor(skill_cursor_, delta, rows);
    }
    return true;
  }
  // All, then each player.
  int stops = static_cast<int>(players_.size()) + 1;
  player_cursor_ = StepCursor(player_cursor_, delta, stops);
  skill_cursor_ = 0;
  return true;
}

int BossAnalysisPanel::VisibleSkillRows() const {
  int height = kMinTerminalRows - kTotalsHeight;
  if (party()) {
    height -= kPlayersChrome + static_cast<int>(players_.size());
  }
  return height - kTableChrome;
}

ftxui::Element BossAnalysisPanel::Render() const {
  std::vector<ftxui::Element> windows;
  windows.push_back(RenderTotals());
  if (party()) {
    windows.push_back(RenderPlayers());
  }
  windows.push_back(RenderTable());
  return ftxui::vbox(std::move(windows));
}

// The party's numbers, each straight after its label and padded to its widest
// so nothing moves between fights.
ftxui::Element BossAnalysisPanel::RenderTotals() const {
  double per_minute = 0.0;
  if (seconds_ > 0.0) {
    per_minute = party_total_ / seconds_ * 60.0;
  }
  ftxui::Element row = ftxui::hbox({
      ftxui::text(kCursorAway + std::string("Duration: ") +
                  PadRight(FormatClock(seconds_), 5)),
      ftxui::filler(),
      ftxui::text("Total Damage: " +
                  PadRight(Damage(party_total_), kDamageWidth)),
      ftxui::filler(),
      ftxui::text("Damage/min: " + PadRight(Damage(per_minute), kDamageWidth)),
      ftxui::text(" "),
  });
  return ThemedWindow(" Totals ", Fixed(std::move(row)));
}

ftxui::Element BossAnalysisPanel::RenderPlayers() const {
  bool focused = !table_focused_;
  std::vector<ftxui::Element> rows;
  rows.push_back(Header(LeadColumns("Name", "Damage", "Damage%")));
  rows.push_back(ThemedSeparator());
  rows.push_back(Row(LeadColumns("All", Damage(party_total_),
                                 Share(party_total_ > 0.0 ? 1.0 : 0.0)),
                     focused && player_cursor_ == 0, player_cursor_ == 0));
  for (int i = 0; i < static_cast<int>(players_.size()); ++i) {
    bool here = player_cursor_ == i + 1;
    double total = player_totals_[i];
    rows.push_back(
        Row(LeadColumns(players_[i].name, Damage(total),
                        Share(party_total_ > 0.0 ? total / party_total_ : 0.0)),
            focused && here, here));
  }
  return ThemedWindow(" Players ", Fixed(ftxui::vbox(std::move(rows))),
                      focused);
}

ftxui::Element BossAnalysisPanel::RenderTable() const {
  const std::vector<BreakdownRow>& rows = table();
  double total = TotalDamage(rows);
  int visible = VisibleSkillRows();
  int count = static_cast<int>(rows.size());
  int first = ScrollWindowStart(count, skill_cursor_, visible);
  std::vector<ftxui::Element> lines;
  if (rows.empty()) {
    lines.push_back(EmptyState("empty", 2));
  }
  for (int i = first; i < std::min(count, first + visible); ++i) {
    const BreakdownRow& row = rows[i];
    std::string columns =
        LeadColumns(row.skill, Damage(row.damage),
                    Share(DamageShare(row, total))) +
        kGap + PadLeft(FormatWithCommas(row.casts), kCastsWidth) + kGap +
        PadLeft(Damage(row.per_line()), kPerLineWidth) + kGap +
        PadLeft(FormatWithCommas(row.lines), kLinesWidth);
    bool here = table_focused_ && i == skill_cursor_;
    lines.push_back(Row(columns, here, here));
  }
  std::string header = LeadColumns("Skill", "Damage", "Damage%") + kGap +
                       PadLeft("Casts", kCastsWidth) + kGap +
                       PadLeft("Damage/line", kPerLineWidth) + kGap +
                       PadLeft("Lines", kLinesWidth);
  ftxui::Element body = ftxui::hbox({
      ftxui::vbox(std::move(lines)) |
          ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kRowWidth),
      ScrollBar(count, first, visible),
  });
  return ThemedWindow(
      " Battle Analysis ",
      Fixed(ftxui::vbox({
          Header(header),
          ThemedSeparator(),
          std::move(body) | ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, visible),
      })),
      table_focused_ && party());
}

}  // namespace ms
