#include "src/frontend/cards/death_card.h"

#include <gtest/gtest.h>

#include <string>

#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/frontend/cards/level_up_card.h"
#include "src/frontend/testing/screen_text.h"
#include "src/frontend/widgets/colors.h"

namespace ms {
namespace {

ftxui::Screen RenderCard() {
  ftxui::Element card = DeathCard();
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fit(card));
  ftxui::Render(screen, card);
  return screen;
}

TEST(DeathCardTest, SaysWhatHappened) {
  EXPECT_GE(RowIndexOf(RenderCard(), "You died!"), 0);
}

TEST(DeathCardTest, IsTitledAndBorderedInRed) {
  ftxui::Screen screen = RenderCard();
  EXPECT_GE(RowIndexOf(screen, "Death"), 0);
  EXPECT_EQ(screen.PixelAt(0, 0).foreground_color, kRed);
  EXPECT_EQ(screen.PixelAt(0, screen.dimy() - 1).foreground_color, kRed);
  EXPECT_NE(screen.PixelAt(0, 0).foreground_color, kYellow);
}

// It appears in the same place as the two good-news cards, so it must be the
// same size; a smaller box there would look like a lesser event than levelling
// up.
TEST(DeathCardTest, IsTheSameSizeAsTheLevelUpCard) {
  ftxui::Element level_up = LevelUpCard(9, 10, 5, 3);
  ftxui::Screen theirs = ftxui::Screen::Create(ftxui::Dimension::Fit(level_up));
  ftxui::Render(theirs, level_up);

  ftxui::Screen ours = RenderCard();
  EXPECT_EQ(ours.dimx(), theirs.dimx());
  EXPECT_EQ(ours.dimy(), theirs.dimy());
}

// Exactly centred in the five-row body: two blank rows above and two below.
TEST(DeathCardTest, HoldsItsOneLineInTheMiddleOfTheCard) {
  ftxui::Screen screen = RenderCard();
  EXPECT_EQ(RowIndexOf(screen, "You died!"), 3);
  EXPECT_EQ(screen.dimy(), 7);
}

}  // namespace
}  // namespace ms
