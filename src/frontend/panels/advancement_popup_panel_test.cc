#include "src/frontend/panels/advancement_popup_panel.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/frontend/widgets/colors.h"
#include "src/protos/character.pb.h"

namespace ms {
namespace {

ftxui::Screen RenderBanner(Job from, Job to) {
  ftxui::Element banner = AdvancementPopupPanel(from, to);
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fit(banner));
  ftxui::Render(screen, banner);
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

// The banner is read top to bottom, so the order of the three rows IS the
// meaning: the job left behind, the arrow, then the job taken. Reversed, it
// would say the player had just become a Beginner.
TEST(AdvancementPopupPanelTest, ReadsOldJobThenArrowThenNewJob) {
  ftxui::Screen screen = RenderBanner(JOB_BEGINNER, JOB_SWORDMAN);
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
  ftxui::Screen screen = RenderBanner(JOB_BEGINNER, JOB_MAGICIAN);
  EXPECT_GE(RowIndexOf(screen, "Magician"), 0);
  EXPECT_LT(RowIndexOf(screen, "Swordman"), 0);
}

TEST(AdvancementPopupPanelTest, IsTitledAndBorderedInGold) {
  ftxui::Screen screen = RenderBanner(JOB_BEGINNER, JOB_SWORDMAN);
  EXPECT_GE(RowIndexOf(screen, "Advancement"), 0);
  EXPECT_EQ(screen.PixelAt(0, 0).foreground_color, kYellow);
  EXPECT_EQ(screen.PixelAt(0, screen.dimy() - 1).foreground_color, kYellow);
}

// A banner like the level-up one, and guarding the same thing: that nothing
// here pins or shrinks the width. Taking the caller's width rather than naming
// one is also what keeps the two the same size, and they land in the same
// place, in the same gold, seconds apart at level 10.
TEST(AdvancementPopupPanelTest, StretchesToTheWidthItIsGiven) {
  const int kWidth = 60;
  ftxui::Element banner = AdvancementPopupPanel(JOB_BEGINNER, JOB_SWORDMAN);
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(kWidth),
                                               ftxui::Dimension::Fit(banner));
  ftxui::Render(screen, banner);
  EXPECT_EQ(screen.PixelAt(0, screen.dimy() - 1).character, "╰");
  EXPECT_EQ(screen.PixelAt(kWidth - 1, screen.dimy() - 1).character, "╯");
}

}  // namespace
}  // namespace ms
