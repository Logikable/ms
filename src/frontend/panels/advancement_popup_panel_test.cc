#include "src/frontend/panels/advancement_popup_panel.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/frontend/panels/level_up_popup_panel.h"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/screen_text.h"
#include "src/protos/character.pb.h"

namespace ms {
namespace {

ftxui::Screen RenderCard(Job from, Job to, int to_stage = 1) {
  ftxui::Element card = AdvancementPopupPanel(from, to, to_stage);
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fit(card));
  ftxui::Render(screen, card);
  return screen;
}

// The card is read top to bottom, so the order of the three rows IS the
// meaning: the job left behind, the arrow, then the job taken. Reversed, it
// would say the player had just become a Beginner.
TEST(AdvancementPopupPanelTest, ReadsOldJobThenArrowThenNewJob) {
  ftxui::Screen screen = RenderCard(JOB_BEGINNER, JOB_SWORDMAN);
  int from_row = RowIndexOf(screen, "Beginner");
  int arrow_row = RowIndexOf(screen, "↓");
  int to_row = RowIndexOf(screen, "Swordman");

  ASSERT_GE(from_row, 0);
  ASSERT_GE(arrow_row, 0);
  ASSERT_GE(to_row, 0);
  EXPECT_LT(from_row, arrow_row);
  EXPECT_LT(arrow_row, to_row);
}

TEST(AdvancementPopupPanelTest, NamesWhicheverJobsItIsGiven) {
  ftxui::Screen screen = RenderCard(JOB_BEGINNER, JOB_MAGICIAN);
  EXPECT_GE(RowIndexOf(screen, "Magician"), 0);
  EXPECT_LT(RowIndexOf(screen, "Swordman"), 0);
}

// The 5th advancement leaves the job where it was, so a card naming both
// halves the same way would say nothing happened.
TEST(AdvancementPopupPanelTest, TheFifthAdvancementTakesAV) {
  ftxui::Screen screen = RenderCard(JOB_NIGHT_LORD, JOB_NIGHT_LORD, 5);
  EXPECT_NE(ScreenRow(screen, 2).find("Night Lord"), std::string::npos);
  EXPECT_EQ(ScreenRow(screen, 2).find("Night Lord V"), std::string::npos);
  EXPECT_NE(ScreenRow(screen, 4).find("Night Lord V"), std::string::npos);
}

TEST(AdvancementPopupPanelTest, IsTitledAndBorderedInGold) {
  ftxui::Screen screen = RenderCard(JOB_BEGINNER, JOB_SWORDMAN);
  EXPECT_GE(RowIndexOf(screen, "Advancement"), 0);
  EXPECT_EQ(screen.PixelAt(0, 0).foreground_color, kYellow);
  EXPECT_EQ(screen.PixelAt(0, screen.dimy() - 1).foreground_color, kYellow);
}

// The two land in the same place, in the same gold, seconds apart at level 10.
// A pair that differed in size would read as two unrelated things rather than
// one moment, so this asks the level-up card directly rather than repeating a
// number that could drift away from it.
TEST(AdvancementPopupPanelTest, IsTheSameSizeAsTheLevelUpCard) {
  ftxui::Element level_up = LevelUpPopupPanel(9, 10, 5, 3);
  ftxui::Screen theirs = ftxui::Screen::Create(ftxui::Dimension::Fit(level_up));
  ftxui::Render(theirs, level_up);

  ftxui::Screen ours = RenderCard(JOB_BEGINNER, JOB_SWORDMAN);
  EXPECT_EQ(ours.dimx(), theirs.dimx());
  EXPECT_EQ(ours.dimy(), theirs.dimy());
}

// Three rows of content in a body of five, so the names are not up against the
// border. Room around what it says is most of what makes a card carry to
// somebody looking at a different window.
TEST(AdvancementPopupPanelTest, KeepsABlankRowAboveAndBelowTheNames) {
  ftxui::Screen screen = RenderCard(JOB_BEGINNER, JOB_SWORDMAN);
  EXPECT_EQ(RowIndexOf(screen, "Beginner"), 2);
  EXPECT_EQ(RowIndexOf(screen, "Swordman"), 4);
}

}  // namespace
}  // namespace ms
