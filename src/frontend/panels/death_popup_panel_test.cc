#include "src/frontend/panels/death_popup_panel.h"

#include <gtest/gtest.h>

#include <string>

#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/frontend/panels/level_up_popup_panel.h"
#include "src/frontend/widgets/colors.h"

namespace ms {
namespace {

ftxui::Screen RenderCard() {
  ftxui::Element card = DeathPopupPanel();
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fit(card));
  ftxui::Render(screen, card);
  return screen;
}

// The index of the first row holding `needle`, or -1 if none does. Read cell
// by cell rather than from Screen::ToString, which threads colour escapes
// through the rows.
int RowIndexOf(const ftxui::Screen& screen, const std::string& needle) {
  for (int y = 0; y < screen.dimy(); ++y) {
    std::string row;
    for (int x = 0; x < screen.dimx(); ++x) {
      const std::string& cell = screen.PixelAt(x, y).character;
      row += cell.empty() ? " " : cell;
    }
    if (row.find(needle) != std::string::npos) {
      return y;
    }
  }
  return -1;
}

TEST(DeathPopupPanelTest, SaysWhatHappened) {
  EXPECT_GE(RowIndexOf(RenderCard(), "You died!"), 0);
}

TEST(DeathPopupPanelTest, IsTitledAndBorderedInRed) {
  ftxui::Screen screen = RenderCard();
  EXPECT_GE(RowIndexOf(screen, "Death"), 0);
  EXPECT_EQ(screen.PixelAt(0, 0).foreground_color, kRed);
  EXPECT_EQ(screen.PixelAt(0, screen.dimy() - 1).foreground_color, kRed);
  EXPECT_NE(screen.PixelAt(0, 0).foreground_color, kYellow);
}

// It lands in the same place as the two cards that mean good news, so it has
// to carry the same weight there -- a smaller box in the same spot would read
// as a lesser event than levelling up.
TEST(DeathPopupPanelTest, IsTheSameSizeAsTheLevelUpCard) {
  ftxui::Element level_up = LevelUpPopupPanel(9, 10, 5, 3);
  ftxui::Screen theirs = ftxui::Screen::Create(ftxui::Dimension::Fit(level_up));
  ftxui::Render(theirs, level_up);

  ftxui::Screen ours = RenderCard();
  EXPECT_EQ(ours.dimx(), theirs.dimx());
  EXPECT_EQ(ours.dimy(), theirs.dimy());
}

// Dead centre of the five-row body: two blank rows above it and two below.
TEST(DeathPopupPanelTest, HoldsItsOneLineInTheMiddleOfTheCard) {
  ftxui::Screen screen = RenderCard();
  EXPECT_EQ(RowIndexOf(screen, "You died!"), 3);
  EXPECT_EQ(screen.dimy(), 7);
}

}  // namespace
}  // namespace ms
