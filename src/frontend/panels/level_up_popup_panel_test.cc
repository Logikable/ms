#include "src/frontend/panels/level_up_popup_panel.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/frontend/widgets/colors.h"

namespace ms {
namespace {

// The card rendered at its natural size, read off cell by cell. Not
// Screen::ToString -- that threads colour escapes through every row, so a row
// does not read as the line the player sees.
ftxui::Screen RenderCard(int from, int to, int ap, int sp) {
  ftxui::Element card = LevelUpPopupPanel(from, to, ap, sp);
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fit(card));
  ftxui::Render(screen, card);
  return screen;
}

std::vector<std::string> Rows(const ftxui::Screen& screen) {
  std::vector<std::string> rows;
  for (int y = 0; y < screen.dimy(); ++y) {
    std::string row;
    for (int x = 0; x < screen.dimx(); ++x) {
      const std::string& cell = screen.PixelAt(x, y).character;
      row += cell.empty() ? " " : cell;
    }
    rows.push_back(row);
  }
  return rows;
}

// The index of the first row holding `needle`, or -1 if none does.
int RowIndexOf(const ftxui::Screen& screen, const std::string& needle) {
  std::vector<std::string> rows = Rows(screen);
  for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
    if (rows[i].find(needle) != std::string::npos) {
      return i;
    }
  }
  return -1;
}

bool AnyRowHas(const ftxui::Screen& screen, const std::string& needle) {
  return RowIndexOf(screen, needle) >= 0;
}

TEST(LevelUpPopupPanelTest, ShowsTheLevelClimbedAsAnArrow) {
  ftxui::Screen screen = RenderCard(12, 13, 5, 3);
  EXPECT_TRUE(AnyRowHas(screen, "12  →  13"));
}

// A climb of several levels reports where it started, not just where it
// stopped: after an idle stretch that is the interesting half.
TEST(LevelUpPopupPanelTest, ReportsTheWholeClimbNotJustTheLastLevel) {
  ftxui::Screen screen = RenderCard(12, 15, 15, 9);
  EXPECT_TRUE(AnyRowHas(screen, "12  →  15"));
  EXPECT_TRUE(AnyRowHas(screen, "+15 AP"));
}

TEST(LevelUpPopupPanelTest, ShowsApAboveSp) {
  ftxui::Screen screen = RenderCard(12, 13, 5, 3);
  int ap_row = RowIndexOf(screen, "+5 AP");
  int sp_row = RowIndexOf(screen, "+3 SP");
  ASSERT_GE(ap_row, 0);
  ASSERT_GE(sp_row, 0);
  EXPECT_LT(ap_row, sp_row) << "AP is spent first, so it is listed first";
}

// What a Beginner sees: SP is granted by level but unreachable until they
// advance, so the caller passes zero and the row goes away rather than
// standing there at "+0".
TEST(LevelUpPopupPanelTest, LeavesOutSpEntirelyWhenNoneWasEarned) {
  ftxui::Screen screen = RenderCard(4, 5, 5, 0);
  EXPECT_TRUE(AnyRowHas(screen, "+5 AP"));
  EXPECT_FALSE(AnyRowHas(screen, "SP"));
}

// Not merely blanked: the card is a row shorter, so nothing is left holding
// space for something that did not happen.
TEST(LevelUpPopupPanelTest, TheCardIsShorterWithoutTheSpRow) {
  EXPECT_EQ(RenderCard(4, 5, 5, 0).dimy(), RenderCard(12, 13, 5, 3).dimy() - 1);
}

TEST(LevelUpPopupPanelTest, IsTitledAndBorderedInGold) {
  ftxui::Screen screen = RenderCard(12, 13, 5, 3);
  EXPECT_TRUE(AnyRowHas(screen, "Level Up"));
  // The top-left corner is border whatever the card holds. Gold is the whole
  // point of this panel: it is what makes it carry across a room.
  EXPECT_EQ(screen.PixelAt(0, 0).foreground_color, kYellow);
  EXPECT_EQ(screen.PixelAt(0, screen.dimy() - 1).foreground_color, kYellow);
}

// A steel-blue rule across a gold card reads as a seam where two things were
// joined, which is exactly what it would be.
TEST(LevelUpPopupPanelTest, TheRuleInsideItIsGoldToo) {
  ftxui::Screen screen = RenderCard(12, 13, 5, 3);
  // Found by its left tee rather than by a run of line: the title row is
  // padded out with the same line character now that the card is wider than
  // its title, so a run of it no longer picks out the rule alone.
  int rule_row = RowIndexOf(screen, "├");
  ASSERT_GE(rule_row, 1) << "a rule between the level and what it paid";
  EXPECT_EQ(screen.PixelAt(screen.dimx() / 2, rule_row).foreground_color,
            kYellow);
}

// --- the room around it ---

// This card is the one thing on screen asking to be noticed from across a
// room, and space around what it says is most of what makes it carry.
TEST(LevelUpPopupPanelTest, LeavesABlankRowAtEachEndOfItsContent) {
  ftxui::Screen screen = RenderCard(12, 13, 5, 3);
  ASSERT_GE(screen.dimy(), 4);
  // Cell by cell rather than off a joined row: the borders are multi-byte, so
  // a byte offset into the row is not the column it looks like.
  for (int y : {1, screen.dimy() - 2}) {
    for (int x = 1; x + 1 < screen.dimx(); ++x) {
      const std::string& cell = screen.PixelAt(x, y).character;
      EXPECT_TRUE(cell.empty() || cell == " ")
          << "row " << y << " column " << x << " holds " << cell;
    }
  }
}

// A terminal cell is about twice as tall as it is wide, so a card that reads
// as square on screen is about twice as wide as it is high.
TEST(LevelUpPopupPanelTest, IsRoughlySquareOnScreen) {
  ftxui::Screen screen = RenderCard(12, 13, 5, 3);
  EXPECT_GE(screen.dimx(), screen.dimy() * 2 - 2);
  EXPECT_LE(screen.dimx(), screen.dimy() * 2 + 2);
}

// The width comes from a minimum rather than from padding either side of the
// numbers, so the card does not breathe in and out as a level count grows a
// digit. Two of these land back to back on a good run.
TEST(LevelUpPopupPanelTest, KeepsOneWidthAsTheNumbersGrow) {
  EXPECT_EQ(RenderCard(4, 5, 5, 3).dimx(), RenderCard(129, 135, 30, 18).dimx());
}

}  // namespace
}  // namespace ms
