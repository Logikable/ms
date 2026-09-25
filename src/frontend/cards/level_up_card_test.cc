#include "src/frontend/cards/level_up_card.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/frontend/testing/screen_text.h"
#include "src/frontend/widgets/chrome.h"
#include "src/frontend/widgets/colors.h"

namespace ms {
namespace {

// The card rendered at its natural size, read cell by cell. Not
// Screen::ToString, which puts colour escapes in every row, so a row wouldn't
// match the line the player sees.
ftxui::Screen RenderCard(int from, int to, int ap, int sp, int hyper_sp = 0,
                         int64_t honor = 0,
                         const std::vector<std::string>& unlocks = {}) {
  ftxui::Element card = LevelUpCard(from, to, ap, sp, hyper_sp, honor, unlocks);
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fit(card));
  ftxui::Render(screen, card);
  return screen;
}

// The index of the first row containing `needle`, or -1 if none does.
int RowIndexOf(const ftxui::Screen& screen, const std::string& needle) {
  std::vector<std::string> rows = ScreenRows(screen);
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

// The colour of the first cell of `needle`, for checking whether a line is
// gold. Color::Default if it isn't on the card, which no colour equals.
ftxui::Color ColorOf(const ftxui::Screen& screen, const std::string& needle) {
  std::vector<std::string> rows = ScreenRows(screen);
  for (int y = 0; y < static_cast<int>(rows.size()); ++y) {
    size_t at = rows[y].find(needle);
    if (at != std::string::npos) {
      return screen.PixelAt(static_cast<int>(at), y).foreground_color;
    }
  }
  return ftxui::Color::Default;
}

TEST(LevelUpCardTest, ShowsTheLevelClimbedAsAnArrow) {
  ftxui::Screen screen = RenderCard(12, 13, 5, 3);
  EXPECT_TRUE(AnyRowHas(screen, "12  →  13"));
}

// A climb of several levels shows where it started, not just where it ended:
// after idling, that is the interesting part.
TEST(LevelUpCardTest, ReportsTheWholeClimbNotJustTheLastLevel) {
  ftxui::Screen screen = RenderCard(12, 15, 15, 9);
  EXPECT_TRUE(AnyRowHas(screen, "12  →  15"));
  EXPECT_TRUE(AnyRowHas(screen, "+15 AP"));
}

TEST(LevelUpCardTest, ShowsApAboveSpAboveHyperSp) {
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
  // Levels between the rungs pay no Hyper SP.
  EXPECT_FALSE(AnyRowHas(RenderCard(141, 142, 5, 0), "Hyper"));
}

// Honor goes under the points, and only once there is somewhere to spend it:
// the caller passes zero until Inner Ability is unlocked.
TEST(LevelUpCardTest, ShowsTheHonorBelowThePoints) {
  ftxui::Screen screen = RenderCard(160, 161, 5, 0, 0, 1800);
  int ap_row = RowIndexOf(screen, "+5 AP");
  int honor_row = RowIndexOf(screen, "+1,800 Honor");
  ASSERT_GE(ap_row, 0);
  ASSERT_GE(honor_row, 0);
  EXPECT_LT(ap_row, honor_row);
  EXPECT_FALSE(AnyRowHas(RenderCard(20, 21, 5, 3), "Honor"));
}

// What a Beginner sees: SP is earned by level but can't be spent until they
// advance, so the caller passes zero and the row is left out rather than
// showing "+0".
TEST(LevelUpCardTest, LeavesOutSpEntirelyWhenNoneWasEarned) {
  ftxui::Screen screen = RenderCard(4, 5, 5, 0);
  EXPECT_TRUE(AnyRowHas(screen, "+5 AP"));
  EXPECT_FALSE(AnyRowHas(screen, "SP"));
}

// The row is removed but its space isn't: the card is the same shape whatever
// the level paid, so the player recognises it rather than seeing a box that
// grows and shrinks.
TEST(LevelUpCardTest, StandsTheSameHeightWhateverItHasToReport) {
  // Border, the levels gained, the divider, three body rows, border.
  const int kRows = 7;
  EXPECT_EQ(RenderCard(12, 13, 5, 3).dimy(), kRows) << "AP and SP";
  EXPECT_EQ(RenderCard(4, 5, 5, 0).dimy(), kRows) << "a Beginner, AP only";
  EXPECT_EQ(RenderCard(4, 5, 0, 0).dimy(), kRows)
      << "a level that paid neither";
}

// A lone AP row sits in the middle of the three, not against the divider with
// two blank rows below.
TEST(LevelUpCardTest, CentresALoneGainInTheBody) {
  ftxui::Screen screen = RenderCard(4, 5, 5, 0);
  int rule_row = RowIndexOf(screen, "├");
  ASSERT_GE(rule_row, 0);
  EXPECT_EQ(RowIndexOf(screen, "+5 AP"), rule_row + 2);
}

// A pair fills the body from the top, leaving the odd row at the bottom. Split
// around the middle instead, they would sit unevenly and land somewhere
// different from a lone row.
TEST(LevelUpCardTest, StartsAPairOfGainsAtTheTopOfTheBody) {
  ftxui::Screen screen = RenderCard(12, 13, 5, 3);
  int rule_row = RowIndexOf(screen, "├");
  ASSERT_GE(rule_row, 0);
  EXPECT_EQ(RowIndexOf(screen, "+5 AP"), rule_row + 1);
  EXPECT_EQ(RowIndexOf(screen, "+3 SP"), rule_row + 2);
}

TEST(LevelUpCardTest, IsTitledAndBorderedInGold) {
  ftxui::Screen screen = RenderCard(12, 13, 5, 3);
  EXPECT_TRUE(AnyRowHas(screen, "Level Up"));
  // The top-left corner is border whatever the card holds. Gold is the point of
  // this panel: it is what makes it noticeable across a room.
  EXPECT_EQ(screen.PixelAt(0, 0).foreground_color, kYellow);
  EXPECT_EQ(screen.PixelAt(0, screen.dimy() - 1).foreground_color, kYellow);
}

// A steel-blue divider across a gold card looks like a seam between two joined
// things.
TEST(LevelUpCardTest, TheRuleInsideItIsGoldToo) {
  ftxui::Screen screen = RenderCard(12, 13, 5, 3);
  // Found by its left tee rather than a run of line characters: the title row
  // is padded with the same character now that the card is wider than its
  // title, so a run no longer identifies the divider alone.
  int rule_row = RowIndexOf(screen, "├");
  ASSERT_GE(rule_row, 1) << "a rule between the level and what it paid";
  EXPECT_EQ(screen.PixelAt(screen.dimx() / 2, rule_row).foreground_color,
            kYellow);
}

// --- what a level opened ---

// The only line on the card the player hasn't seen before, so it is the one
// line drawn gold against the white.
TEST(LevelUpCardTest, AnnouncesAnUnlockInGold) {
  ftxui::Screen screen = RenderCard(39, 40, 5, 3, 0, 0, {"Scrolling"});
  EXPECT_TRUE(AnyRowHas(screen, "Unlocked Scrolling!"));
  EXPECT_EQ(ColorOf(screen, "Unlocked"), kYellow);
  EXPECT_NE(ColorOf(screen, "+5 AP"), kYellow) << "the gains stay white";
}

// The announcement shares the body with the gains instead of being stacked
// under it, so a card that unlocked something is the same size as every other.
TEST(LevelUpCardTest, AnUnlockDoesNotGrowTheCard) {
  EXPECT_EQ(RenderCard(39, 40, 5, 3, 0, 0, {"Scrolling"}).dimy(),
            RenderCard(39, 40, 5, 3).dimy());
}

TEST(LevelUpCardTest, AnnouncesUnlocksBelowTheGains) {
  ftxui::Screen screen = RenderCard(39, 40, 5, 3, 0, 0, {"Scrolling"});
  EXPECT_GT(RowIndexOf(screen, "Unlocked"), RowIndexOf(screen, "+3 SP"));
}

// Most levels unlock nothing.
TEST(LevelUpCardTest, SaysNothingWhenALevelOpenedNothing) {
  EXPECT_FALSE(AnyRowHas(RenderCard(12, 13, 5, 3), "Unlocked"));
}

// --- the room around what it says ---

// Fitted to its content, the card would only be as wide as "12  →  13", too
// small to catch the eye of someone looking at another window. Tui::RenderFrame
// centres it, which shrinks it to its content, so the card must set its own
// minimum width.
TEST(LevelUpCardTest, IsWiderThanTheLineInsideItNeeds) {
  EXPECT_EQ(RenderCard(12, 13, 5, 3).dimx(), kCelebrationContentWidth + 2);
}

// One fixed width rather than fixed padding around the numbers, so the card
// doesn't change width between two levels gained back to back.
TEST(LevelUpCardTest, HoldsOneWidthAsALevelCountGrowsADigit) {
  EXPECT_EQ(RenderCard(9, 10, 5, 3).dimx(), RenderCard(99, 100, 5, 3).dimx());
}

}  // namespace
}  // namespace ms
