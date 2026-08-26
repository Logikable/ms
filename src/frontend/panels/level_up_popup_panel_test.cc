#include "src/frontend/panels/level_up_popup_panel.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/panel_util.h"

namespace ms {
namespace {

// The card rendered at its natural size, read off cell by cell. Not
// Screen::ToString -- that threads colour escapes through every row, so a row
// does not read as the line the player sees.
ftxui::Screen RenderCard(int from, int to, int ap, int sp, int hyper_sp = 0,
                         const std::vector<std::string>& unlocks = {}) {
  ftxui::Element card = LevelUpPopupPanel(from, to, ap, sp, hyper_sp, unlocks);
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

// The colour of the first cell of `needle`, for asking whether a line came out
// gold. Color::Default when it is not on the card, which no colour equals.
ftxui::Color ColorOf(const ftxui::Screen& screen, const std::string& needle) {
  std::vector<std::string> rows = Rows(screen);
  for (int y = 0; y < static_cast<int>(rows.size()); ++y) {
    size_t at = rows[y].find(needle);
    if (at != std::string::npos) {
      return screen.PixelAt(static_cast<int>(at), y).foreground_color;
    }
  }
  return ftxui::Color::Default;
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

TEST(LevelUpPopupPanelTest, ShowsApAboveSpAboveHyperSp) {
  ftxui::Screen screen = RenderCard(139, 140, 5, 5, 1);
  int ap_row = RowIndexOf(screen, "+5 AP");
  int sp_row = RowIndexOf(screen, "+5 SP");
  int hyper_row = RowIndexOf(screen, "+1 Hyper SP");
  ASSERT_GE(ap_row, 0);
  ASSERT_GE(sp_row, 0);
  ASSERT_GE(hyper_row, 0);
  EXPECT_LT(ap_row, sp_row) << "AP is spent first, so it is listed first";
  EXPECT_LT(sp_row, hyper_row) << "the pool the SP is not a stage of";
  // Three gains fill the body exactly, so the card is its usual height.
  EXPECT_EQ(screen.dimy(), RenderCard(12, 13, 5, 3).dimy());
  // And the levels between the rungs pay none of it.
  EXPECT_FALSE(AnyRowHas(RenderCard(141, 142, 5, 0), "Hyper"));
}

// What a Beginner sees: SP is granted by level but unreachable until they
// advance, so the caller passes zero and the row goes away rather than
// standing there at "+0".
TEST(LevelUpPopupPanelTest, LeavesOutSpEntirelyWhenNoneWasEarned) {
  ftxui::Screen screen = RenderCard(4, 5, 5, 0);
  EXPECT_TRUE(AnyRowHas(screen, "+5 AP"));
  EXPECT_FALSE(AnyRowHas(screen, "SP"));
}

// The row goes away, but the space it stood in does not: the card is one shape
// whatever the level paid, so it is something the player recognises rather than
// a box that grows and shrinks with the news.
TEST(LevelUpPopupPanelTest, StandsTheSameHeightWhateverItHasToReport) {
  // Border, the level climbed, the rule, three rows of body, border.
  const int kRows = 7;
  EXPECT_EQ(RenderCard(12, 13, 5, 3).dimy(), kRows) << "AP and SP";
  EXPECT_EQ(RenderCard(4, 5, 5, 0).dimy(), kRows) << "a Beginner, AP only";
  EXPECT_EQ(RenderCard(4, 5, 0, 0).dimy(), kRows)
      << "a level that paid neither";
}

// A lone AP row sits in the middle of the three, rather than up against the
// rule with two blank rows under it.
TEST(LevelUpPopupPanelTest, CentresALoneGainInTheBody) {
  ftxui::Screen screen = RenderCard(4, 5, 5, 0);
  int rule_row = RowIndexOf(screen, "├");
  ASSERT_GE(rule_row, 0);
  EXPECT_EQ(RowIndexOf(screen, "+5 AP"), rule_row + 2);
}

// A pair fills the body from the top, leaving the odd row over at the bottom.
// Split around the middle instead, they would straddle it unevenly and land
// somewhere different from where a lone row lands.
TEST(LevelUpPopupPanelTest, StartsAPairOfGainsAtTheTopOfTheBody) {
  ftxui::Screen screen = RenderCard(12, 13, 5, 3);
  int rule_row = RowIndexOf(screen, "├");
  ASSERT_GE(rule_row, 0);
  EXPECT_EQ(RowIndexOf(screen, "+5 AP"), rule_row + 1);
  EXPECT_EQ(RowIndexOf(screen, "+3 SP"), rule_row + 2);
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

// --- what a level opened ---

// The one line on the card the player has never seen before, so it is the one
// line drawn gold against the white.
TEST(LevelUpPopupPanelTest, AnnouncesAnUnlockInGold) {
  ftxui::Screen screen = RenderCard(39, 40, 5, 3, 0, {"Scrolling"});
  EXPECT_TRUE(AnyRowHas(screen, "Unlocked Scrolling!"));
  EXPECT_EQ(ColorOf(screen, "Unlocked"), kYellow);
  EXPECT_NE(ColorOf(screen, "+5 AP"), kYellow) << "the gains stay white";
}

// The announcement shares the body with the gains rather than being stacked
// under it, so the card that opened something is the same size as every other.
TEST(LevelUpPopupPanelTest, AnUnlockDoesNotGrowTheCard) {
  EXPECT_EQ(RenderCard(39, 40, 5, 3, 0, {"Scrolling"}).dimy(),
            RenderCard(39, 40, 5, 3).dimy());
}

TEST(LevelUpPopupPanelTest, AnnouncesUnlocksBelowTheGains) {
  ftxui::Screen screen = RenderCard(39, 40, 5, 3, 0, {"Scrolling"});
  EXPECT_GT(RowIndexOf(screen, "Unlocked"), RowIndexOf(screen, "+3 SP"));
}

// Every other level, which is nearly all of them.
TEST(LevelUpPopupPanelTest, SaysNothingWhenALevelOpenedNothing) {
  EXPECT_FALSE(AnyRowHas(RenderCard(12, 13, 5, 3), "Unlocked"));
}

// --- the room around what it says ---

// Fitted to its content the card would be the width of "12  →  13" and no
// more, which is not enough of a card to catch someone looking at a different
// window. Tui::RenderFrame centres it, and centring shrinks to content, so the
// floor has to come from the card itself.
TEST(LevelUpPopupPanelTest, IsWiderThanTheLineInsideItNeeds) {
  EXPECT_EQ(RenderCard(12, 13, 5, 3).dimx(), kCelebrationContentWidth + 2);
}

// One width rather than a fixed padding either side of the numbers, so the card
// does not breathe in and out between two levels that land back to back.
TEST(LevelUpPopupPanelTest, HoldsOneWidthAsALevelCountGrowsADigit) {
  EXPECT_EQ(RenderCard(9, 10, 5, 3).dimx(), RenderCard(99, 100, 5, 3).dimx());
}

}  // namespace
}  // namespace ms
