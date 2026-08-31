#include "src/frontend/widgets/screen_text.h"

#include <gtest/gtest.h>

#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/screen.hpp"

namespace ms {
namespace {

ftxui::Screen Render(ftxui::Element element, int width, int height) {
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(width),
                                               ftxui::Dimension::Fixed(height));
  ftxui::Render(screen, std::move(element));
  return screen;
}

TEST(ScreenTextTest, ReadsRowsWholeAndInPart) {
  ftxui::Screen screen = Render(ftxui::text("abcdef"), 8, 2);
  EXPECT_EQ(ScreenRow(screen, 0), "abcdef  ");
  EXPECT_EQ(ScreenRow(screen, 0, 2, 4), "cd");
  EXPECT_EQ(ScreenRow(screen, 1), "        ") << "an unpainted cell is a space";
  EXPECT_EQ(ScreenRows(screen).size(), 2u);
  EXPECT_EQ(ScreenText(screen), "abcdef  \n        \n");
}

// Asking past either end is clamped rather than read off the screen.
TEST(ScreenTextTest, AColumnRangeIsClampedToTheScreen) {
  ftxui::Screen screen = Render(ftxui::text("ab"), 4, 1);
  EXPECT_EQ(ScreenRow(screen, 0, -5, 99), "ab  ");
}

// The whole reason this is not a find() into Screen::ToString: a border cell
// is three bytes wide and one column wide, so the two part company the moment
// one is on the row.
TEST(ScreenTextTest, FindsTheColumnPastMultiByteCells) {
  ftxui::Screen screen = Render(ftxui::border(ftxui::text("hi")), 6, 3);
  EXPECT_EQ(FindOnScreen(screen, "hi").x, 1);
  EXPECT_EQ(FindOnScreen(screen, "hi").y, 1);
  EXPECT_EQ(RowIndexOf(screen, "hi"), 1);
  // The byte offset a naive search would have returned instead.
  EXPECT_EQ(ScreenRow(screen, 1).find("hi"), 3u);
}

TEST(ScreenTextTest, SaysSoWhenNothingHoldsTheNeedle) {
  ftxui::Screen screen = Render(ftxui::text("abc"), 4, 1);
  EXPECT_EQ(RowIndexOf(screen, "zz"), -1);
  EXPECT_EQ(FindOnScreen(screen, "zz").x, -1);
  EXPECT_EQ(ColorOf(screen, "zz"), ftxui::Color::Default);
  EXPECT_EQ(PixelOf(screen, "zz").character, "");
}

// The colour is read off the cell the needle starts in, which is what a byte
// offset would get wrong once a border shares the row.
TEST(ScreenTextTest, ReadsTheColourAndPixelWhereTheNeedleStarts) {
  ftxui::Screen screen =
      Render(ftxui::border(ftxui::text("hi") | ftxui::color(ftxui::Color::Red) |
                           ftxui::dim),
             6, 3);
  EXPECT_EQ(ColorOf(screen, "hi"), ftxui::Color::Red);
  EXPECT_TRUE(PixelOf(screen, "hi").dim);
}

}  // namespace
}  // namespace ms
