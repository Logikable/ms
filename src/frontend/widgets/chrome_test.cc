#include "src/frontend/widgets/chrome.h"

#include <gtest/gtest.h>

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/component/component_base.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/format.h"
#include "src/frontend/widgets/game_names.h"
#include "src/frontend/widgets/item_row.h"
#include "src/frontend/widgets/keys.h"
#include "src/protos/skill.pb.h"

namespace ms {
namespace {

// --- Rarity colours ---

// Every rank is written in its own colour.
TEST(RarityColorsTest, WritesEachRankInItsOwnColour) {
  EXPECT_EQ(RarityColor(ABILITY_RANK_RARE), kRare.ToColor());
  EXPECT_EQ(RarityColor(ABILITY_RANK_EPIC), kEpic.ToColor());
  EXPECT_EQ(RarityColor(ABILITY_RANK_UNIQUE), kUnique.ToColor());
  EXPECT_EQ(RarityColor(ABILITY_RANK_LEGENDARY), kLegendary.ToColor());
}

// --- ScrollBar ---

// The bar's glyphs, one per row, with the thumb's rows marked. A half-height
// cap counts as part of the thumb.
std::string RenderScrollBar(int total, int first_visible, int visible) {
  ftxui::Element bar = ScrollBar(total, first_visible, visible);
  ftxui::Screen screen = ftxui::Screen::Create(
      ftxui::Dimension::Fixed(1), ftxui::Dimension::Fixed(visible));
  ftxui::Render(screen, bar);
  std::string out;
  for (int y = 0; y < visible; ++y) {
    const std::string& cell = screen.PixelAt(0, y).character;
    out += cell == " " || cell.empty() ? '.' : '#';
  }
  return out;
}

TEST(ScrollBarTest, DrawsNothingWhenTheWholeListFits) {
  EXPECT_EQ(RenderScrollBar(10, 0, 10), "..........");
  EXPECT_EQ(RenderScrollBar(4, 0, 10), "..........");
}

// The thumb is as much of the bar as the window is of the list, plus whatever
// row its half-height caps land in.
TEST(ScrollBarTest, SizesTheThumbByTheShareOnScreen) {
  EXPECT_EQ(RenderScrollBar(20, 0, 10), "######....") << "half on screen";
  EXPECT_EQ(RenderScrollBar(40, 0, 10), "###.......") << "a quarter";
}

TEST(ScrollBarTest, SlidesTheThumbDownWithTheWindow) {
  EXPECT_EQ(RenderScrollBar(20, 5, 10), "..######..");
  EXPECT_EQ(RenderScrollBar(20, 10, 10), ".....#####") << "at the foot";
}

// However long the list, the thumb is still something to see.
TEST(ScrollBarTest, KeepsAThumbOnAVeryLongList) {
  EXPECT_NE(RenderScrollBar(10000, 0, 10).find('#'), std::string::npos);
}

// --- ProgressBar ---

// Renders a bar 10 cells wide onto its own screen so pixels can be inspected.
ftxui::Screen RenderBar(float frac, const std::string& label) {
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(10),
                                               ftxui::Dimension::Fixed(1));
  ftxui::Element bar = ProgressBar(frac, kGreen, label);
  ftxui::Render(screen, bar);
  return screen;
}

TEST(ProgressBarTest, FillsUpToTheFraction) {
  ftxui::Screen screen = RenderBar(0.5f, "");
  EXPECT_EQ(screen.PixelAt(4, 0).background_color, kGreen);
  EXPECT_EQ(screen.PixelAt(5, 0).background_color, kBarEmpty);
}

TEST(ProgressBarTest, ClampsOutOfRangeFractions) {
  EXPECT_EQ(RenderBar(5.0f, "").PixelAt(9, 0).background_color, kGreen);
  EXPECT_EQ(RenderBar(-1.0f, "").PixelAt(0, 0).background_color, kBarEmpty);
}

TEST(ProgressBarTest, CentersTheLabelOverTheBar) {
  ftxui::Screen screen = RenderBar(0.0f, "AB");
  EXPECT_EQ(screen.PixelAt(4, 0).character, "A");
  EXPECT_EQ(screen.PixelAt(5, 0).character, "B");
}

TEST(ProgressBarTest, LabelReadsDarkOnTheFillAndLightPastIt) {
  // Half full, so "ABCD" straddles the boundary: B sits on the fill, C past it.
  ftxui::Screen screen = RenderBar(0.5f, "ABCD");
  EXPECT_EQ(screen.PixelAt(4, 0).character, "B");
  EXPECT_EQ(screen.PixelAt(4, 0).background_color, kGreen);
  EXPECT_EQ(screen.PixelAt(4, 0).foreground_color, ftxui::Color::Black);
  EXPECT_EQ(screen.PixelAt(5, 0).character, "C");
  EXPECT_EQ(screen.PixelAt(5, 0).background_color, kBarEmpty);
  EXPECT_EQ(screen.PixelAt(5, 0).foreground_color, ftxui::Color::White);
}

TEST(ProgressBarTest, PinnedLabelColorHoldsAcrossTheWholeBar) {
  // Same straddle, but the label color is pinned: it survives the boundary.
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(10),
                                               ftxui::Dimension::Fixed(1));
  ftxui::Render(screen, ProgressBar(0.5f, kGreen, "ABCD", ftxui::Color::White));
  EXPECT_EQ(screen.PixelAt(4, 0).background_color, kGreen);
  EXPECT_EQ(screen.PixelAt(4, 0).foreground_color, ftxui::Color::White);
  EXPECT_EQ(screen.PixelAt(5, 0).background_color, kBarEmpty);
  EXPECT_EQ(screen.PixelAt(5, 0).foreground_color, ftxui::Color::White);
}

TEST(ProgressBarTest, EmptyLabelLeavesTheBarBlank) {
  ftxui::Screen screen = RenderBar(1.0f, "");
  EXPECT_EQ(screen.PixelAt(5, 0).character, " ");
}

// A label of several lines takes a row each, and every row is one bar: filled
// to the same point, with its own line centred over it.
TEST(ProgressBarTest, DrawsALineOfLabelPerRow) {
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(10),
                                               ftxui::Dimension::Fixed(2));
  ftxui::Render(
      screen, ProgressBar(0.5f, kGreen, std::vector<std::string>{"AB", "CD"}));
  EXPECT_EQ(screen.PixelAt(4, 0).character, "A");
  EXPECT_EQ(screen.PixelAt(4, 1).character, "C");
  EXPECT_EQ(screen.PixelAt(5, 1).character, "D");
  EXPECT_EQ(screen.PixelAt(0, 1).background_color, kGreen);
  EXPECT_EQ(screen.PixelAt(9, 1).background_color, kBarEmpty);
}

// --- ResultWindow ---

// Renders a result window wide enough not to wrap and returns its rows, so the
// order of subject / rule / body / rule / button can be asserted.
std::vector<std::string> ResultRows(std::vector<ftxui::Element> body) {
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(20),
                                               ftxui::Dimension::Fixed(7));
  ftxui::Render(screen, ResultWindow(" T ", "Sword", std::move(body)));
  std::vector<std::string> rows;
  for (int y = 0; y < 7; ++y) {
    std::string row;
    for (int x = 0; x < 20; ++x) {
      row += screen.PixelAt(x, y).character;
    }
    rows.push_back(row);
  }
  return rows;
}

TEST(ResultWindowTest, RulesOffTheSubjectAndTheButton) {
  std::vector<std::string> rows =
      ResultRows({ftxui::text("body") | ftxui::hcenter});
  EXPECT_NE(rows[1].find("Sword"), std::string::npos);
  EXPECT_NE(rows[2].find("─"), std::string::npos);
  EXPECT_NE(rows[3].find("body"), std::string::npos);
  // The rule below the body is the point: a blank row used to sit here, which
  // made these the only dialogs whose button floated.
  EXPECT_NE(rows[4].find("─"), std::string::npos);
  EXPECT_NE(rows[5].find("[Continue]"), std::string::npos);
  // And the subject is centered by the helper, so no caller can forget to.
  EXPECT_EQ(rows[1].find("Sword"), rows[1].rfind("Sword"));
  EXPECT_GT(rows[1].find("Sword"), 1u);
}

// --- CenteredRow ---

// Floats `content` in a window on a screen far larger than it needs, so the
// window sizes to its own content -- the only condition under which a row can
// be flush against the border. Returns the rows trimmed to the window's own
// columns. Cells no element painted read as spaces: ftxui leaves their
// character empty, and dropping them would slide the rest of the row left.
std::vector<std::string> WindowRows(std::vector<ftxui::Element> content) {
  constexpr int kWidth = 60;
  constexpr int kHeight = 10;
  ftxui::Screen screen = ftxui::Screen::Create(
      ftxui::Dimension::Fixed(kWidth), ftxui::Dimension::Fixed(kHeight));
  ftxui::Render(screen, ftxui::center(ThemedWindow(
                            " T ", ftxui::vbox(std::move(content)))));
  // Kept as cells rather than joined strings until the trim is done: the border
  // glyphs are multibyte, so a byte offset into a joined row is not a column.
  std::vector<std::vector<std::string>> painted;
  for (int y = 0; y < kHeight; ++y) {
    std::vector<std::string> cells;
    bool blank = true;
    for (int x = 0; x < kWidth; ++x) {
      const std::string& cell = screen.PixelAt(x, y).character;
      if (cell.empty() || cell == " ") {
        cells.push_back(" ");
      } else {
        cells.push_back(cell);
        blank = false;
      }
    }
    // Skip the blank rows the float leaves above and below the window.
    if (!blank) {
      painted.push_back(std::move(cells));
    }
  }
  // The top border spans exactly the window, so its extent gives the columns to
  // keep -- everything past them is the screen the window floats on.
  int first = 0;
  int last = kWidth - 1;
  while (painted[0][first] == " ") {
    ++first;
  }
  while (painted[0][last] == " ") {
    --last;
  }
  std::vector<std::string> rows;
  for (const std::vector<std::string>& cells : painted) {
    std::string row;
    for (int x = first; x <= last; ++x) {
      row += cells[x];
    }
    rows.push_back(row);
  }
  return rows;
}

// The whole point of the helper. The longest line on a screen is the one that
// sets the window's width, so bare hcenter leaves precisely that line -- the
// one the player is most likely reading -- jammed against both borders.
TEST(CenteredRowTest, TheLongestRowStillClearsBothBorders) {
  std::vector<std::string> rows =
      WindowRows({CenteredRow("This action is irreversible.")});
  EXPECT_EQ(rows[1], "│ This action is irreversible. │");
}

// The clearance is not a left indent: shorter rows still centre.
TEST(CenteredRowTest, ShorterRowsStayCentered) {
  std::vector<std::string> rows = WindowRows(
      {CenteredRow("wide enough to set the width"), CenteredRow("x")});
  EXPECT_EQ(rows[2], "│              x               │");
}

// Takes an element as readily as a string, so a bar or a button row centres
// with the same clearance as text.
TEST(CenteredRowTest, CentersAnElementToo) {
  std::vector<std::string> rows =
      WindowRows({CenteredRow(ftxui::text("0123456789"))});
  EXPECT_EQ(rows[1], "│ 0123456789 │");
}

// --- EmptyState ---

TEST(EmptyStateTest, WrapsTheReasonInParenthesesAfterOneSpace) {
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(8),
                                               ftxui::Dimension::Fixed(1));
  ftxui::Render(screen, EmptyState("empty"));
  EXPECT_EQ(screen.ToString(), " (empty)");
}

// The gutter exists so the row lines up with the "  " / "> " cursor column of
// the list it stands in for.
TEST(EmptyStateTest, IndentsByTheRequestedGutter) {
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(9),
                                               ftxui::Dimension::Fixed(1));
  ftxui::Render(screen, EmptyState("empty", /*gutter=*/2));
  EXPECT_EQ(screen.ToString(), "  (empty)");
}

// --- TabChip ---

// Renders one chip onto its own screen so its pixels can be inspected. The
// label is padded either side, so column 1 holds its first character.
ftxui::Screen RenderChip(bool active, bool row_focused) {
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(4),
                                               ftxui::Dimension::Fixed(1));
  ftxui::Render(screen, TabChip("AB", active, row_focused));
  return screen;
}

TEST(TabChipTest, PadsTheLabelWithASpaceEitherSide) {
  ftxui::Screen screen = RenderChip(/*active=*/false, /*row_focused=*/false);
  EXPECT_EQ(screen.PixelAt(0, 0).character, " ");
  EXPECT_EQ(screen.PixelAt(1, 0).character, "A");
  EXPECT_EQ(screen.PixelAt(2, 0).character, "B");
  EXPECT_EQ(screen.PixelAt(3, 0).character, " ");
}

TEST(TabChipTest, InactiveChipIsPlainTheme) {
  ftxui::Screen screen = RenderChip(/*active=*/false, /*row_focused=*/true);
  EXPECT_EQ(screen.PixelAt(1, 0).foreground_color, kTheme);
  EXPECT_FALSE(screen.PixelAt(1, 0).inverted);
}

// Off-focus the active chip stays theme-blue, so two visible bars don't both
// look like the one the keys are reaching.
TEST(TabChipTest, ActiveChipOffFocusKeepsTheThemeInvert) {
  ftxui::Screen screen = RenderChip(/*active=*/true, /*row_focused=*/false);
  EXPECT_EQ(screen.PixelAt(1, 0).foreground_color, kTheme);
  EXPECT_TRUE(screen.PixelAt(1, 0).inverted);
}

TEST(TabChipTest, ActiveChipOnTheFocusedRowGoesWhite) {
  ftxui::Screen screen = RenderChip(/*active=*/true, /*row_focused=*/true);
  EXPECT_EQ(screen.PixelAt(1, 0).background_color, ftxui::Color::White);
  EXPECT_EQ(screen.PixelAt(1, 0).foreground_color, ftxui::Color::Black);
}

// --- TabBar ---

// The bar as plain text, read cell by cell: Screen::ToString keeps the colour
// escapes, so a byte offset into a line is not the column it looks like.
std::string RenderTabBar(const std::vector<TabSpec>& tabs, int active,
                         int width) {
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(60),
                                               ftxui::Dimension::Fixed(1));
  ftxui::Render(screen,
                ftxui::hbox({TabBar(tabs, active, /*row_focused=*/false, width),
                             ftxui::filler()}));
  std::string out;
  for (int x = 0; x < screen.dimx(); ++x) {
    out += screen.PixelAt(x, 0).character;
  }
  while (!out.empty() && out.back() == ' ') {
    out.pop_back();
  }
  return out;
}

// Four chips of five columns each -- TabChip pads a space either side -- so
// 20 columns is exactly enough and 19 has to give something up.
const std::vector<TabSpec> kFour = {{"one"}, {"two"}, {"six"}, {"ten"}};
constexpr int kFits = 20;
constexpr int kTight = 19;

TEST(TabBarTest, ABarThatFitsDrawsEveryTabAndNoMarks) {
  EXPECT_EQ(RenderTabBar(kFour, 0, kFits), " one  two  six  ten");
}

TEST(TabBarTest, NoWidthMeansNoLimit) {
  EXPECT_EQ(RenderTabBar(kFour, 0, /*width=*/0), " one  two  six  ten");
}

// The right mark costs a column of its own, so a bar one column short of
// fitting still has to give a chip up.
TEST(TabBarTest, ABarThatOverflowsHoldsTheRestBehindAMark) {
  EXPECT_EQ(RenderTabBar(kFour, 0, kTight), " one  two  six ›");
}

// Left mark at the start, right mark at the end, each shown only while there
// is something that way. The right one still holds its column when absent, so
// the chips do not shuffle as the bar scrolls under them; the left one stands
// in the leading chip's pad, so the bar begins in the same column either way.
TEST(TabBarTest, EachMarkShowsOnlyWhileThereIsMoreThatWay) {
  EXPECT_EQ(RenderTabBar(kFour, 0, kTight).substr(0, 1), " ");
  EXPECT_EQ(RenderTabBar(kFour, 3, kTight), "‹two  six  ten");
}

// A bar that scrolls puts its first label where a bar that fits puts it, so
// two bars stacked in one panel line up.
TEST(TabBarTest, AScrolledBarStartsInTheSameColumnAsOneThatFits) {
  EXPECT_EQ(RenderTabBar(kFour, 0, kFits).find("one"),
            RenderTabBar(kFour, 0, kTight).find("one"));
}

// The window follows the selection rather than sitting still, so the chip the
// player is on is drawn whichever it is.
TEST(TabBarTest, TheWindowFollowsTheActiveTab) {
  for (int active = 0; active < 4; ++active) {
    EXPECT_NE(RenderTabBar(kFour, active, kTight).find(kFour[active].label),
              std::string::npos)
        << "tab " << active << " fell off the bar";
  }
}

// It moves by the least it can, and comes back to where it was: the window is
// a plain function of the selection, not a thing with a memory of its own.
TEST(TabBarTest, SteppingBackReturnsTheWindowToWhereItWas) {
  std::string at_the_start = RenderTabBar(kFour, 0, kTight);
  EXPECT_EQ(RenderTabBar(kFour, 2, kTight), at_the_start)
      << "the window moved before it had to";
  EXPECT_NE(RenderTabBar(kFour, 3, kTight), at_the_start);
  EXPECT_EQ(RenderTabBar(kFour, 2, kTight), at_the_start);
}

// A width too small for even one chip draws the chip the player is on anyway.
// A bar showing nothing says less than one that overflows.
TEST(TabBarTest, TooNarrowForAnyChipStillDrawsTheActiveOne) {
  EXPECT_EQ(RenderTabBar(kFour, 2, /*width=*/3), "‹six ›");
}

TEST(TabBarTest, AnUnseenTabIsStillGoldThroughTheBar) {
  std::vector<TabSpec> tabs = {{"one"}, {"two", /*unseen=*/true}};
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(12),
                                               ftxui::Dimension::Fixed(1));
  ftxui::Render(screen, TabBar(tabs, 0, /*row_focused=*/false, /*width=*/0));
  EXPECT_EQ(screen.PixelAt(6, 0).character, "t");
  EXPECT_EQ(screen.PixelAt(6, 0).foreground_color, kYellow);
}

// --- Floating ---

// A three-row bordered box, standing in for the panel a float is drawn over.
ftxui::Element Base() {
  return ftxui::border(ftxui::text("base"));
}

// "MENU" `row` rows down and `col` columns across, in an element sized to end
// exactly on it. The trailing fillers a real overlay carries are left off so
// the element's own extent is the marker's position, which is what the screen
// fit and the edge slides are measured against.
ftxui::Element Marker(int row, int col) {
  return ftxui::vbox({
      ftxui::filler() | ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, row),
      ftxui::hbox({
          ftxui::filler() | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, col),
          ftxui::text("MENU"),
      }),
  });
}

// Holds `element` to its own size with its corner at (col, row), leaving the
// rest of the terminal empty for a float to spill onto. Without the trailing
// fillers the boxes would stretch it to the whole screen and there would be
// nothing to spill on.
//
// The offset is what makes an edge slide observable: a float already against
// the top or left of the screen has nowhere to come back to, so it clips
// there instead.
ftxui::Element At(int col, int row, ftxui::Element element) {
  std::vector<ftxui::Element> rows;
  for (int i = 0; i < row; ++i) {
    rows.push_back(ftxui::text(""));
  }
  rows.push_back(ftxui::hbox({
      ftxui::text(std::string(col, ' ')),
      std::move(element),
      ftxui::filler(),
  }));
  rows.push_back(ftxui::filler());
  return ftxui::vbox(std::move(rows));
}

ftxui::Screen RenderSized(ftxui::Element element, int width, int height) {
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(width),
                                               ftxui::Dimension::Fixed(height));
  ftxui::Render(screen, std::move(element));
  return screen;
}

// Row `y` of the screen, with the cells no element painted read as spaces.
std::string ScreenRow(const ftxui::Screen& screen, int y) {
  std::string row;
  for (int x = 0; x < screen.dimx(); ++x) {
    const std::string& cell = screen.PixelAt(x, y).character;
    if (cell.empty()) {
      row += " ";
    } else {
      row += cell;
    }
  }
  return row;
}

// `count` cells of row `y` from column `x`. Border glyphs are multibyte, so an
// offset into the joined row is not a column -- read the cells instead.
std::string ScreenCells(const ftxui::Screen& screen, int y, int x, int count) {
  std::string cells;
  for (int i = x; i < x + count; ++i) {
    const std::string& cell = screen.PixelAt(i, y).character;
    if (cell.empty()) {
      cells += " ";
    } else {
      cells += cell;
    }
  }
  return cells;
}

// The height an element takes when the screen is fitted to it.
int FitHeight(ftxui::Element element) {
  return ftxui::Screen::Create(ftxui::Dimension::Fit(element),
                               ftxui::Dimension::Fit(element))
      .dimy();
}

// The behaviour Floating exists to avoid, pinned here so the test above it is
// known to be testing something: a bare overlay drags the dbox taller.
TEST(FloatingTest, AnUnfloatedOverlayGrowsWhatItIsDrawnOver) {
  EXPECT_GT(FitHeight(ftxui::dbox({Base(), Marker(5, 0)})), FitHeight(Base()));
}

TEST(FloatingTest, AsksItsParentForNoRoom) {
  EXPECT_EQ(FitHeight(ftxui::dbox({Base(), Floating(Marker(5, 0))})),
            FitHeight(Base()));
}

TEST(FloatingTest, DrawsPastTheElementItIsOver) {
  ftxui::Screen screen = RenderSized(
      At(0, 0, ftxui::dbox({Base(), Floating(Marker(5, 0))})), 20, 10);
  // Row 2 is the box's bottom border, so row 5 is outside it.
  EXPECT_NE(ScreenRow(screen, 2).find("╰"), std::string::npos);
  EXPECT_EQ(ScreenCells(screen, 5, 0, 4), "MENU");
}

TEST(FloatingTest, LeavesTheBordersBelowItAlone) {
  ftxui::Screen plain = RenderSized(At(0, 0, Base()), 20, 10);
  ftxui::Screen floated = RenderSized(
      At(0, 0, ftxui::dbox({Base(), Floating(Marker(5, 0))})), 20, 10);
  for (int y = 0; y <= 2; ++y) {
    EXPECT_EQ(ScreenRow(floated, y), ScreenRow(plain, y)) << "row " << y;
  }
}

// Anchored on row 3, Marker(8, 0) wants rows 3..11 of a ten-row screen, so it
// comes back up the two that hang off and ends on the last one.
TEST(FloatingTest, SlidesUpOffTheBottomEdgeOfTheScreen) {
  ftxui::Screen screen = RenderSized(
      At(0, 3, ftxui::dbox({Base(), Floating(Marker(8, 0))})), 20, 10);
  EXPECT_EQ(ScreenCells(screen, 9, 0, 4), "MENU");
}

// Anchored on column 6, Marker(0, 16) wants columns 6..25 of a twenty-column
// screen, so it comes back the six that hang off and ends on the last one.
TEST(FloatingTest, SlidesLeftOffTheRightEdgeOfTheScreen) {
  ftxui::Screen screen = RenderSized(
      At(6, 0, ftxui::dbox({Base(), Floating(Marker(0, 16))})), 20, 10);
  EXPECT_EQ(ScreenCells(screen, 0, 16, 4), "MENU");
}

// A float too tall for the screen has to lose rows somewhere, and it gives up
// the top ones: an overlay is put where it belongs by empty space above it, so
// that is the end that can be spared. Twenty-one rows on an eight-row screen,
// and the last of them still lands on the last row.
TEST(FloatingTest, GivesUpItsTopWhenItCannotFit) {
  ftxui::Element tall = ftxui::vbox({
      ftxui::text("TOP"),
      ftxui::filler() | ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, 19),
      ftxui::text("BOT"),
  });
  ftxui::Screen screen = RenderSized(
      At(0, 1, ftxui::dbox({Base(), Floating(std::move(tall))})), 20, 8);
  EXPECT_EQ(ScreenCells(screen, 7, 0, 3), "BOT");
}

// --- ScrollWindowStart ---

TEST(ScrollWindowStartTest, CentresTheSelectionInTheWindow) {
  // Five on screen of twenty: the cursor sits with two rows above it.
  EXPECT_EQ(ScrollWindowStart(20, 10, 5), 8);
  EXPECT_EQ(ScrollWindowStart(20, 11, 5), 9) << "and moves a row at a time";
  // An even window puts it a row above the middle, as yframe does.
  EXPECT_EQ(ScrollWindowStart(20, 10, 6), 8);
}

// Both ends of the list are shown whole rather than half a window of blanks:
// there is nothing above the first row to centre it against.
TEST(ScrollWindowStartTest, ClampsAtBothEndsOfTheList) {
  EXPECT_EQ(ScrollWindowStart(20, 0, 5), 0);
  EXPECT_EQ(ScrollWindowStart(20, 1, 5), 0);
  EXPECT_EQ(ScrollWindowStart(20, 19, 5), 15);
  EXPECT_EQ(ScrollWindowStart(20, 18, 5), 15);
}

// A list that fits never scrolls, and neither does one with no window at all.
TEST(ScrollWindowStartTest, AListThatFitsStartsAtItsHead) {
  EXPECT_EQ(ScrollWindowStart(5, 4, 5), 0);
  EXPECT_EQ(ScrollWindowStart(5, 4, 9), 0);
  EXPECT_EQ(ScrollWindowStart(0, 0, 0), 0);
}

TEST(AdvanceTabKeyTest, EveryStageHasItsOwnKey) {
  EXPECT_NE(AdvanceTabKey(1), AdvanceTabKey(2));
  EXPECT_NE(AdvanceTabKey(1), kShopTabKey);
  EXPECT_FALSE(AdvanceTabKey(1).empty());
}

// Every slot names itself. The armour rows arrived after the two the player
// already reads, so a blank here would be a worn item with no slot.
// --- DialogWindow ---

// The point of the helper: whatever the body is, a rule stands between it and
// the buttons, so no dialog can be written without one.
TEST(DialogWindowTest, RulesBetweenTheBodyAndTheButtons) {
  ftxui::Screen screen =
      RenderSized(DialogWindow(" Ask ", {CenteredRow("Are you sure?")},
                               ActionButton("OK", true)),
                  20, 5);
  EXPECT_EQ(ScreenRow(screen, 1), "│  Are you sure?   │");
  EXPECT_EQ(ScreenRow(screen, 2), "├──────────────────┤");
  EXPECT_EQ(ScreenRow(screen, 3), "│       [OK]       │");
}

TEST(DialogWindowTest, TheAccentColoursTheRuleAndTheBorder) {
  ftxui::Screen screen = RenderSized(
      DialogWindow("", {CenteredRow("Gone")}, ActionButton("OK", false), kRed),
      20, 5);
  EXPECT_EQ(screen.PixelAt(0, 0).foreground_color, kRed);
  EXPECT_EQ(screen.PixelAt(5, 2).foreground_color, kRed);
}

// The Hyper tab's worth column: flat for a flat stat, a percent sign for a
// percentage, and EXP's half-point step written out.
TEST(HyperStatTextTest, WritesWhatALevelIsWorth) {
  EXPECT_EQ(HyperStatBonusText(HYPER_STAT_FIELD_STR, 3), "+90");
  EXPECT_EQ(HyperStatBonusText(HYPER_STAT_FIELD_ATTACK, 4), "+12");
  EXPECT_EQ(HyperStatBonusText(HYPER_STAT_FIELD_CRIT_DAMAGE, 7), "+7%");
  EXPECT_EQ(HyperStatBonusText(HYPER_STAT_FIELD_EXP, 1), "+0.5%");
  EXPECT_EQ(HyperStatBonusText(HYPER_STAT_FIELD_EXP, 4), "+2%");
  // An untouched row still says which kind of stat it is.
  EXPECT_EQ(HyperStatBonusText(HYPER_STAT_FIELD_BOSS_DAMAGE, 0), "+0%");
  EXPECT_EQ(HyperStatBonusText(HYPER_STAT_FIELD_LUK, 0), "+0");
}

// Every stat in the order is named, and the order holds all of them.
TEST(HyperStatTextTest, NamesEveryStatInTheOrder) {
  EXPECT_EQ(kNumHyperStats, 14);
  for (int i = 0; i < kNumHyperStats; ++i) {
    EXPECT_FALSE(HyperStatName(kHyperStatOrder[i]).empty()) << i;
  }
  EXPECT_EQ(HyperStatName(HYPER_STAT_FIELD_UNSPECIFIED), "");
}

}  // namespace
}  // namespace ms
