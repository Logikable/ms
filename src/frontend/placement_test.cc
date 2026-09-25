#include "src/frontend/placement.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/frontend/testing/screen_text.h"
#include "src/frontend/widgets/chrome.h"

namespace ms {
namespace {

constexpr int kRows = 11;
constexpr int kColumns = 21;

std::vector<std::string> Draw(ftxui::Element element) {
  ftxui::Screen screen = ftxui::Screen::Create(
      ftxui::Dimension::Fixed(kColumns), ftxui::Dimension::Fixed(kRows));
  ftxui::Render(screen, element);
  return ScreenRows(screen);
}

// A bordered box `rows` tall and `columns` wide, standing in for a window.
ftxui::Element Box(int rows, int columns, const std::string& label) {
  return ftxui::border(ftxui::text(label) |
                       ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, rows) |
                       ftxui::size(ftxui::WIDTH, ftxui::EQUAL, columns));
}

TEST(PlacementTest, CentredSitsInTheMiddleOfBothAxes) {
  std::vector<std::string> rows = Draw(Centred(Box(3, 5, "x")));
  // Five rows of box in eleven leave three blank above and below; seven columns
  // in twenty-one leave seven on each side.
  EXPECT_EQ(rows[3], "       ╭─────╮       ");
  EXPECT_EQ(rows[7], "       ╰─────╯       ");
}

TEST(PlacementTest, BottomRightTucksTheBoxIntoTheCorner) {
  std::vector<std::string> rows =
      Draw(BottomRight(ftxui::filler(), Box(1, 3, "x")));
  // Three rows of box at the bottom of eleven, five columns at the right of
  // twenty-one.
  EXPECT_EQ(rows[8], "                ╭───╮");
  EXPECT_EQ(rows[10], "                ╰───╯");
  EXPECT_EQ(rows[7], "                     ");
}

TEST(PlacementTest, CardRowDrawsEveryCardTheHeightOfTheTallest) {
  std::vector<std::string> rows =
      Draw(SideBySide({Box(1, 3, "a"), Box(5, 3, "b")}));
  // Both borders start and end on the same rows: a short card is stretched to
  // the tall one's height, not centred beside it.
  EXPECT_EQ(rows[2], "     ╭───╮╭───╮      ");
  EXPECT_EQ(rows[8], "     ╰───╯╰───╯      ");
}

TEST(PlacementTest, OverlayCentresTheDialogOverTheScreen) {
  ftxui::Element screen =
      ftxui::border(ftxui::text("background") | ftxui::flex);
  std::vector<std::string> rows = Draw(Overlay(screen, Box(1, 3, "d")));
  // The screen behind keeps its corners, and the dialog sits in the middle with
  // the background cleared under it.
  EXPECT_EQ(rows[0].substr(0, 3), "╭");
  EXPECT_EQ(rows[4], "│       ╭───╮       │");
  EXPECT_EQ(rows[6], "│       ╰───╯       │");
}

}  // namespace
}  // namespace ms
