#include "src/frontend/screens/boss_analysis_panel.h"

#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/combat/damage_breakdown.h"
#include "src/frontend/placement.h"
#include "src/frontend/testing/screen_text.h"

namespace ms {
namespace {

PlayerBreakdown Player(const std::string& name,
                       std::vector<BreakdownRow> rows) {
  return {name, name, std::move(rows)};
}

// Two players, with the lower one listed first so the panel has to sort them.
std::vector<PlayerBreakdown> Party() {
  return {
      Player("Light", {{"Blast", 100.0, 10, 20}, {"Raging Blow", 50.0, 5, 5}}),
      Player("Heavy",
             {{"Raging Blow", 800.0, 8, 40}, {"Puncture", 50.0, 9, 9}}),
  };
}

// Thirty skills, more than the table shows at once.
std::vector<PlayerBreakdown> Solo() {
  std::vector<BreakdownRow> rows;
  for (int i = 0; i < 30; ++i) {
    rows.push_back({"Skill " + std::to_string(i), 100.0 - i, 1, 1});
  }
  return {Player("Me", std::move(rows))};
}

ftxui::Screen Draw(const BossAnalysisPanel& panel) {
  ftxui::Screen screen(kMinTerminalColumns, kMinTerminalRows);
  ftxui::Render(screen, Centred(panel.Render()));
  return screen;
}

// The column just past the end of `needle` on the first row containing it.
int EndOf(const ftxui::Screen& screen, const std::string& needle) {
  ScreenPos pos = FindOnScreen(screen, needle);
  return pos.x + static_cast<int>(needle.size());
}

// The column after the last character inside row `y`'s right border.
int TextEnd(const ftxui::Screen& screen, int y) {
  for (int x = screen.dimx() - 1; x >= 0; --x) {
    std::string cell = ScreenRow(screen, y, x, x + 1);
    if (cell != " " && cell != "│" && !cell.empty()) {
      return x + 1;
    }
  }
  return 0;
}

bool IsRule(const std::string& row) {
  return row.find("───") != std::string::npos;
}

TEST(BossAnalysisPanelTest, PartyColumnsLineUpUnderTheirHeaders) {
  BossAnalysisPanel panel;
  panel.Open(Party(), 180.0);
  ftxui::Screen screen = Draw(panel);
  std::vector<std::string> rows = ScreenRows(screen);

  EXPECT_NE(RowIndexOf(screen, "Duration: 3:00"), -1);
  EXPECT_NE(RowIndexOf(screen, "Total Damage: 1,000 "), -1);
  EXPECT_NE(RowIndexOf(screen, "Damage/min: 333 "), -1);

  // Highest first, All on top, and the caret one space before the name.
  int all = RowIndexOf(screen, "> All");
  ASSERT_NE(all, -1);
  EXPECT_LT(all, RowIndexOf(screen, "Heavy"));
  EXPECT_LT(RowIndexOf(screen, "Heavy"), RowIndexOf(screen, "Light"));
  EXPECT_NE(rows[all].find("100.00%"), std::string::npos);
  EXPECT_NE(rows[RowIndexOf(screen, "Heavy")].find("85.00%"),
            std::string::npos);

  // A rule under both headers, and the shared columns in the same place.
  int name = RowIndexOf(screen, "Name");
  int skill = RowIndexOf(screen, "Skill");
  EXPECT_TRUE(IsRule(rows[name + 1]));
  EXPECT_TRUE(IsRule(rows[skill + 1]));
  EXPECT_EQ(FindOnScreen(screen, "Name").x, FindOnScreen(screen, "Skill").x);
  EXPECT_EQ(FindOnScreen(screen, "All").x, FindOnScreen(screen, "Name").x);
  EXPECT_EQ(EndOf(screen, "Damage%"), EndOf(screen, "100.00%"));
  EXPECT_EQ(TextEnd(screen, skill),
            TextEnd(screen, RowIndexOf(screen, "Blast")));

  // All's table merges Raging Blow and lists it first.
  EXPECT_LT(RowIndexOf(screen, "Raging Blow"), RowIndexOf(screen, "Blast"));
  EXPECT_NE(rows[RowIndexOf(screen, "Raging Blow")].find("850"),
            std::string::npos);

  EXPECT_EQ(static_cast<int>(rows.size()), kMinTerminalRows);
  EXPECT_TRUE(rows.back().find("╯") != std::string::npos)
      << "the table's foot is the screen's last row";
  EXPECT_TRUE(RowsTouchingTheRightBorder(panel.Render()).empty());
}

TEST(BossAnalysisPanelTest, PlayersPickTheTableAndTabHandsTheArrowsOver) {
  BossAnalysisPanel panel;
  panel.Open(Party(), 60.0);
  EXPECT_FALSE(panel.table_focused());

  panel.OnEvent(ftxui::Event::ArrowDown);
  EXPECT_EQ(panel.player_cursor(), 1);
  ftxui::Screen screen = Draw(panel);
  EXPECT_NE(RowIndexOf(screen, "> Heavy"), -1);
  EXPECT_EQ(RowIndexOf(screen, "Blast"), -1) << "Light's skill, not Heavy's";
  ASSERT_EQ(panel.table().size(), 2u);
  EXPECT_EQ(panel.table()[1].skill, "Puncture");

  panel.OnEvent(ftxui::Event::Tab);
  EXPECT_TRUE(panel.table_focused());
  panel.OnEvent(ftxui::Event::ArrowDown);
  EXPECT_EQ(panel.skill_cursor(), 1);
  EXPECT_EQ(panel.player_cursor(), 1);
  screen = Draw(panel);
  EXPECT_NE(RowIndexOf(screen, "> Puncture"), -1);
  EXPECT_EQ(RowIndexOf(screen, "> Heavy"), -1) << "one caret on screen";

  // A newly selected player's table starts from the top.
  panel.OnEvent(ftxui::Event::TabReverse);
  panel.OnEvent(ftxui::Event::ArrowDown);
  EXPECT_EQ(panel.player_cursor(), 2);
  EXPECT_EQ(panel.skill_cursor(), 0);
  EXPECT_FALSE(panel.OnEvent(ftxui::Event::Escape));
}

TEST(BossAnalysisPanelTest, SoloIsTheTableAloneAndItScrolls) {
  BossAnalysisPanel panel;
  panel.Open(Solo(), 30.0);
  EXPECT_FALSE(panel.party());
  EXPECT_TRUE(panel.table_focused());
  panel.OnEvent(ftxui::Event::Tab);
  EXPECT_TRUE(panel.table_focused());

  ftxui::Screen screen = Draw(panel);
  EXPECT_EQ(RowIndexOf(screen, "Name"), -1);
  EXPECT_NE(RowIndexOf(screen, "> Skill 0"), -1);
  EXPECT_EQ(RowIndexOf(screen, "Skill 29"), -1);
  EXPECT_EQ(static_cast<int>(ScreenRows(screen).size()), kMinTerminalRows);

  for (int i = 0; i < 29; ++i) {
    panel.OnEvent(ftxui::Event::ArrowDown);
  }
  screen = Draw(panel);
  EXPECT_NE(RowIndexOf(screen, "> Skill 29"), -1);
  EXPECT_EQ(RowIndexOf(screen, "Skill 0 "), -1);
}

// The widest values each column was sized for still leave the gutter.
TEST(BossAnalysisPanelTest, WidestValuesFit) {
  BossAnalysisPanel panel;
  panel.Open({Player(std::string(20, 'N'),
                     {{"Repeating Crossbow Cartridge", 9.99e15, 99999, 99999}}),
              Player("Other", {})},
             5999.0);
  ftxui::Screen screen = Draw(panel);
  EXPECT_NE(RowIndexOf(screen, "9,990,000,000,000,000"), -1);
  EXPECT_NE(RowIndexOf(screen, "99,900,999,010"), -1);
  EXPECT_NE(RowIndexOf(screen, "99:59"), -1);
  // Hurricane casts that many, and the column still ends under its header.
  ScreenPos casts = FindOnScreen(screen, "  99,999  ");
  EXPECT_EQ(casts.x + 8, EndOf(screen, "Casts"));
  EXPECT_TRUE(RowsTouchingTheRightBorder(panel.Render()).empty());
  ftxui::Element element = panel.Render();
  element->ComputeRequirement();
  EXPECT_LE(element->requirement().min_x, kMinTerminalColumns);
}

}  // namespace
}  // namespace ms
