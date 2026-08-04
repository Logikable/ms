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
  int rule_row = RowIndexOf(screen, "─────");
  ASSERT_GE(rule_row, 1) << "a rule between the level and what it paid";
  EXPECT_EQ(screen.PixelAt(screen.dimx() / 2, rule_row).foreground_color,
            kYellow);
}

}  // namespace
}  // namespace ms
