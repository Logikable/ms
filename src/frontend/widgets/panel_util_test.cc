#include "src/frontend/widgets/panel_util.h"

#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/component/component_base.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/frontend/widgets/colors.h"

namespace ms {
namespace {

// --- PadRight ---

TEST(PadRightTest, PadsShortStringWithSpaces) {
  EXPECT_EQ(PadRight("hi", 5), "hi   ");
}

TEST(PadRightTest, ExactWidthUnchanged) {
  EXPECT_EQ(PadRight("hello", 5), "hello");
}

TEST(PadRightTest, TruncatesLongString) {
  EXPECT_EQ(PadRight("toolong", 4), "tool");
}

TEST(PadRightTest, EmptyStringProducesAllSpaces) {
  EXPECT_EQ(PadRight("", 3), "   ");
}

// --- DisplayStatFor ---

TEST(DisplayStatForTest, FindsTheEntryTheFieldNames) {
  EquipStats stats;
  stats.set_luk(7);
  const DisplayStat* stat = DisplayStatFor(STAT_FIELD_LUK);
  ASSERT_NE(stat, nullptr);
  EXPECT_STREQ(stat->label, "LUK");
  EXPECT_EQ(stat->GetFrom(stats), 7);
}

// HP is spelled max_hp on EquipStats; the join is by label, so it still lands.
TEST(DisplayStatForTest, FindsAFieldWithARenamedAccessor) {
  EquipStats stats;
  stats.set_max_hp(150);
  const DisplayStat* stat = DisplayStatFor(STAT_FIELD_HP);
  ASSERT_NE(stat, nullptr);
  EXPECT_EQ(stat->GetFrom(stats), 150);
}

TEST(DisplayStatForTest, UnspecifiedFieldHasNoEntry) {
  EXPECT_EQ(DisplayStatFor(STAT_FIELD_UNSPECIFIED), nullptr);
}

// --- PadLeft ---

TEST(PadLeftTest, RightAlignsWithinTheWidth) {
  EXPECT_EQ(PadLeft("7", 3), "  7");
  EXPECT_EQ(PadLeft("123", 3), "123");
}

// The one place it differs from PadRight, and the reason it exists: a level
// that outgrew its column must read 1000, not 100.
TEST(PadLeftTest, NeverTruncates) {
  EXPECT_EQ(PadLeft("1000", 3), "1000");
}

// --- FormatWithCommas ---

TEST(FormatWithCommasTest, NoCommasBelowThousand) {
  EXPECT_EQ(FormatWithCommas(0), "0");
  EXPECT_EQ(FormatWithCommas(999), "999");
}

TEST(FormatWithCommasTest, InsertsThousandsSeparators) {
  EXPECT_EQ(FormatWithCommas(1000), "1,000");
  EXPECT_EQ(FormatWithCommas(1234567), "1,234,567");
}

TEST(FormatWithCommasTest, HandlesNegative) {
  EXPECT_EQ(FormatWithCommas(-12345), "-12,345");
}

// --- FormatMeso ---

TEST(FormatMesoTest, PrefixesIndicatorAndFormatsValue) {
  EXPECT_NE(FormatMeso(1234567).find("1,234,567"), std::string::npos);
}

// --- AppendStat ---

TEST(AppendStatTest, ZeroValueIsNoOp) {
  std::string out;
  AppendStat(out, 0, "ATT");
  EXPECT_TRUE(out.empty());
}

TEST(AppendStatTest, NegativeValueIsNoOp) {
  std::string out;
  AppendStat(out, -1, "ATT");
  EXPECT_TRUE(out.empty());
}

TEST(AppendStatTest, AppendsLabelAndValue) {
  std::string out;
  AppendStat(out, 5, "ATT");
  EXPECT_EQ(out, "+5 ATT");
}

TEST(AppendStatTest, AddsSeparatorBetweenStats) {
  std::string out;
  AppendStat(out, 3, "STR");
  AppendStat(out, 7, "DEX");
  EXPECT_EQ(out, "+3 STR  +7 DEX");
}

TEST(AppendStatTest, SkipsZeroInMiddle) {
  std::string out;
  AppendStat(out, 3, "STR");
  AppendStat(out, 0, "DEX");
  AppendStat(out, 2, "LUK");
  EXPECT_EQ(out, "+3 STR  +2 LUK");
}

// --- FormatItemEntry ---

TEST(FormatItemEntryTest, ContainsNameSlotInfoAndScrollCounts) {
  std::string entry =
      FormatItemEntry("Sword", EQUIP_SLOT_PRIMARY_WEAPON, "+7 ATT", 3, 2, 4);
  EXPECT_NE(entry.find("Sword"), std::string::npos);
  EXPECT_NE(entry.find("Weapon"), std::string::npos);
  EXPECT_NE(entry.find("+7 ATT"), std::string::npos);
  EXPECT_NE(entry.find("3/2/4"), std::string::npos);
}

TEST(FormatItemEntryTest, InfoColumnPaddedForAlignment) {
  // Short and long info strings should position scroll counts at the same
  // offset.
  std::string short_entry =
      FormatItemEntry("Sword", EQUIP_SLOT_PRIMARY_WEAPON, "A", 3, 2, 4);
  std::string long_entry = FormatItemEntry("Sword", EQUIP_SLOT_PRIMARY_WEAPON,
                                           "A longer info", 3, 2, 4);
  EXPECT_EQ(short_entry.find("3/2/4"), long_entry.find("3/2/4"));
}

// An item name wider than its column is cut, and slides under the column once
// its row has been selected long enough -- the same treatment a skill name
// gets, through the same ScrollingWindow. Nothing shipped is this long yet;
// the wiring is what is being pinned.
TEST(FormatItemEntryTest, ALongNameIsCutAndThenSlides) {
  const char* kWordy = "Fafnir Mistilteinn Trace Of Old";  // 31 columns
  std::string still =
      FormatItemEntry(kWordy, EQUIP_SLOT_PRIMARY_WEAPON, "+7 ATT", 3, 2, 4);
  EXPECT_EQ(still.substr(0, kItemNameWidth), "Fafnir Mistilteinn Trace O");

  std::string slid =
      FormatItemEntry(kWordy, EQUIP_SLOT_PRIMARY_WEAPON, "+7 ATT", 3, 2, 4,
                      kMarqueePause + kMarqueeStep);
  EXPECT_EQ(slid.substr(0, kItemNameWidth), "fnir Mistilteinn Trace Of ");
  // The columns after the name do not move with it.
  EXPECT_EQ(still.substr(kItemNameWidth), slid.substr(kItemNameWidth));
}

// A name that fits is padded to the column and never moves, so a list of them
// stays a list rather than shuffling under the cursor.
TEST(FormatItemEntryTest, AShortNameNeverMoves) {
  std::string still =
      FormatItemEntry("Sword", EQUIP_SLOT_PRIMARY_WEAPON, "+7 ATT", 3, 2, 4);
  std::string later = FormatItemEntry("Sword", EQUIP_SLOT_PRIMARY_WEAPON,
                                      "+7 ATT", 3, 2, 4, kMarqueePause * 10);
  EXPECT_EQ(still, later);
}

TEST(FormatItemEntryTest, NonUpgradeableItemShowsDash) {
  std::string entry =
      FormatItemEntry("Sword", EQUIP_SLOT_PRIMARY_WEAPON, "info", -1, -1, -1);
  EXPECT_NE(entry.find("-"), std::string::npos);
  EXPECT_EQ(entry.find("/"), std::string::npos);
}

// --- FormatJobCategories ---

TEST(FormatJobCategoriesTest, EmptyCategoriesReturnsAll) {
  EquipPrototype proto;
  EXPECT_EQ(FormatJobCategories(proto), "All");
}

TEST(FormatJobCategoriesTest, UniversalReturnsAll) {
  EquipPrototype proto;
  proto.add_equip_job_categories(EQUIP_JOB_CATEGORY_UNIVERSAL);
  EXPECT_EQ(FormatJobCategories(proto), "All");
}

TEST(FormatJobCategoriesTest, SingleCategory) {
  EquipPrototype proto;
  proto.add_equip_job_categories(EQUIP_JOB_CATEGORY_WARRIOR);
  EXPECT_EQ(FormatJobCategories(proto), "Warrior");
}

TEST(FormatJobCategoriesTest, MultipleCategories) {
  EquipPrototype proto;
  proto.add_equip_job_categories(EQUIP_JOB_CATEGORY_WARRIOR);
  proto.add_equip_job_categories(EQUIP_JOB_CATEGORY_THIEF);
  EXPECT_EQ(FormatJobCategories(proto), "Warrior/Thief");
}

TEST(AttackSpeedNameTest, EveryStageHasAName) {
  for (int stage = ATTACK_SPEED_SLOWER; stage <= ATTACK_SPEED_FASTEST_3;
       ++stage) {
    EXPECT_FALSE(AttackSpeedName(static_cast<AttackSpeed>(stage)).empty())
        << "stage " << stage;
  }
  EXPECT_EQ(AttackSpeedName(ATTACK_SPEED_FAST_2), "Fast 2");
  EXPECT_EQ(AttackSpeedName(ATTACK_SPEED_UNSPECIFIED), "");
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
}

// The subject is centered by the helper, so no caller can forget to.
TEST(ResultWindowTest, CentersTheSubject) {
  std::vector<std::string> rows =
      ResultRows({ftxui::text("body") | ftxui::hcenter});
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

// --- StepCursor ---

TEST(StepCursorTest, WalksTheRingOneStopAtATime) {
  EXPECT_EQ(StepCursor(0, 1, 4), 1);
  EXPECT_EQ(StepCursor(2, 1, 4), 3);
  EXPECT_EQ(StepCursor(3, -1, 4), 2);
}

// The whole point of it. Down off the last stop lands on the first, and Up off
// the first lands on the last.
TEST(StepCursorTest, ComesOutTheOtherEndAtEitherEdge) {
  EXPECT_EQ(StepCursor(3, 1, 4), 0);
  EXPECT_EQ(StepCursor(0, -1, 4), 3);
}

// A ring with one place to stand is every step a no-op -- a panel whose list is
// empty, where the tab bar is the only stop there is.
TEST(StepCursorTest, AOneStopRingGoesNowhere) {
  EXPECT_EQ(StepCursor(0, 1, 1), 0);
  EXPECT_EQ(StepCursor(0, -1, 1), 0);
}

// Nowhere to stand at all: answered rather than left to the caller, because
// every list here can be empty and none of them wants its own check.
TEST(StepCursorTest, AnEmptyRingAnswersZero) {
  EXPECT_EQ(StepCursor(0, -1, 0), 0);
  EXPECT_EQ(StepCursor(3, 1, -1), 0);
}

// C++ hands a negative dividend a negative remainder, so a single modulo would
// answer -1 here and put the cursor off the list.
TEST(StepCursorTest, NeverAnswersBelowZero) {
  for (int stops = 1; stops <= 8; ++stops) {
    for (int current = 0; current < stops; ++current) {
      EXPECT_GE(StepCursor(current, -1, stops), 0)
          << "stops=" << stops << " current=" << current;
      EXPECT_LT(StepCursor(current, -1, stops), stops)
          << "stops=" << stops << " current=" << current;
    }
  }
}

// A cursor left pointing past the end of a list that shrank under it -- a tab
// switched, an item sold -- is folded back in rather than walked further off.
TEST(StepCursorTest, FoldsACurrentFromOutsideTheRingBackIn) {
  EXPECT_EQ(StepCursor(9, 1, 4), 2);
  EXPECT_EQ(StepCursor(-3, 0, 4), 1);
}

// Any delta, not just the one step every caller passes today.
TEST(StepCursorTest, TakesMoreThanOneStopAtATime) {
  EXPECT_EQ(StepCursor(0, 3, 4), 3);
  EXPECT_EQ(StepCursor(0, 5, 4), 1);
  EXPECT_EQ(StepCursor(0, -5, 4), 3);
}

// --- WrappingList ---

namespace {

// A three-row ftxui::Menu wrapped for cycling, sharing `selected` and
// `entries` with the caller so a test can move one and read the other.
ftxui::Component WrappedMenu(std::vector<std::string>& entries, int& selected) {
  return WrappingList(ftxui::Menu(&entries, &selected), selected, [&entries]() {
    return static_cast<int>(entries.size());
  });
}

}  // namespace

TEST(WrappingListTest, LeavesTheStepsThroughTheMiddleToTheMenu) {
  std::vector<std::string> entries = {"a", "b", "c"};
  int selected = 0;
  ftxui::Component list = WrappedMenu(entries, selected);
  EXPECT_TRUE(list->OnEvent(ftxui::Event::ArrowDown));
  EXPECT_EQ(selected, 1);
  EXPECT_TRUE(list->OnEvent(ftxui::Event::ArrowUp));
  EXPECT_EQ(selected, 0);
}

TEST(WrappingListTest, UpOffTheFirstRowLandsOnTheLast) {
  std::vector<std::string> entries = {"a", "b", "c"};
  int selected = 0;
  ftxui::Component list = WrappedMenu(entries, selected);
  EXPECT_TRUE(list->OnEvent(ftxui::Event::ArrowUp));
  EXPECT_EQ(selected, 2);
}

TEST(WrappingListTest, DownOffTheLastRowLandsOnTheFirst) {
  std::vector<std::string> entries = {"a", "b", "c"};
  int selected = 2;
  ftxui::Component list = WrappedMenu(entries, selected);
  EXPECT_TRUE(list->OnEvent(ftxui::Event::ArrowDown));
  EXPECT_EQ(selected, 0);
}

// The count is asked at the keypress, not taken once. These lists lose rows
// under the cursor -- an item sold, a filter narrowed -- and a wrap that
// remembered the old length would send the cursor off the end of the new one.
TEST(WrappingListTest, AsksHowLongTheListIsEveryTime) {
  std::vector<std::string> entries = {"a", "b", "c", "d", "e"};
  int selected = 0;
  ftxui::Component list = WrappedMenu(entries, selected);
  entries = {"a", "b"};
  EXPECT_TRUE(list->OnEvent(ftxui::Event::ArrowUp));
  EXPECT_EQ(selected, 1) << "the last row of the list as it is now";
}

// Nothing to be at either end of, so the key is swallowed. Handing it down
// instead is not harmless: an ftxui::Menu with no entries still moves its
// index, and the cursor ends up at row -1 of a list that has no rows.
TEST(WrappingListTest, SwallowsTheKeyOnAnEmptyList) {
  std::vector<std::string> entries;
  int selected = 0;
  ftxui::Component list = WrappedMenu(entries, selected);
  EXPECT_TRUE(list->OnEvent(ftxui::Event::ArrowUp));
  EXPECT_EQ(selected, 0);
  EXPECT_TRUE(list->OnEvent(ftxui::Event::ArrowDown));
  EXPECT_EQ(selected, 0);
}

// A list of one is a ring of one: the cursor is at both ends at once, and
// either key leaves it where it is rather than appearing to move.
TEST(WrappingListTest, ASingleRowGoesNowhere) {
  std::vector<std::string> entries = {"only"};
  int selected = 0;
  ftxui::Component list = WrappedMenu(entries, selected);
  list->OnEvent(ftxui::Event::ArrowUp);
  EXPECT_EQ(selected, 0);
  list->OnEvent(ftxui::Event::ArrowDown);
  EXPECT_EQ(selected, 0);
}

// Everything that is not an edge step passes through untouched, so wrapping a
// list does not cost it any other key.
TEST(WrappingListTest, PassesEveryOtherKeyThrough) {
  std::vector<std::string> entries = {"a", "b", "c"};
  int selected = 0;
  bool seen = false;
  ftxui::Component list = WrappingList(
      ftxui::CatchEvent(ftxui::Menu(&entries, &selected),
                        [&seen](ftxui::Event) {
                          seen = true;
                          return false;
                        }),
      selected, [&entries]() { return static_cast<int>(entries.size()); });
  list->OnEvent(ftxui::Event::Character('x'));
  EXPECT_TRUE(seen);
}

// --- AlwaysFocusable ---

namespace {

// A component that reports itself unfocusable, as ftxui::Menu does when it has
// no entries. Renderer() without a child is the shortest one to hand.
ftxui::Component UnfocusableComponent() {
  return ftxui::Renderer([]() { return ftxui::text("PANEL"); });
}

}  // namespace

TEST(AlwaysFocusableTest, ReportsFocusableWhereTheChildDoesNot) {
  ftxui::Component child = UnfocusableComponent();
  ASSERT_FALSE(child->Focusable());
  EXPECT_TRUE(AlwaysFocusable(child)->Focusable());
}

TEST(AlwaysFocusableTest, DrawsWhatTheChildDraws) {
  ftxui::Component wrapped = AlwaysFocusable(UnfocusableComponent());
  EXPECT_EQ(ScreenCells(RenderSized(wrapped->Render(), 10, 1), 0, 0, 5),
            "PANEL");
}

TEST(AlwaysFocusableTest, PassesEventsToTheChild) {
  bool seen = false;
  ftxui::Component wrapped = AlwaysFocusable(
      ftxui::CatchEvent(UnfocusableComponent(), [&seen](ftxui::Event) {
        seen = true;
        return true;
      }));
  EXPECT_TRUE(wrapped->OnEvent(ftxui::Event::ArrowRight));
  EXPECT_TRUE(seen);
}

// The reason the wrapper exists. Container::Tab asks only its active child
// whether it is focusable and drops every key when the answer is no, so an
// unwrapped panel never sees the event at all.
TEST(AlwaysFocusableTest, ATabContainerReachesTheChild) {
  bool seen_bare = false;
  int selector = 0;
  ftxui::Component bare =
      ftxui::Container::Tab({ftxui::CatchEvent(UnfocusableComponent(),
                                               [&seen_bare](ftxui::Event) {
                                                 seen_bare = true;
                                                 return true;
                                               })},
                            &selector);
  ASSERT_FALSE(bare->OnEvent(ftxui::Event::ArrowRight));
  ASSERT_FALSE(seen_bare) << "the container is expected to drop this one";

  bool seen_wrapped = false;
  ftxui::Component wrapped = ftxui::Container::Tab(
      {AlwaysFocusable(ftxui::CatchEvent(UnfocusableComponent(),
                                         [&seen_wrapped](ftxui::Event) {
                                           seen_wrapped = true;
                                           return true;
                                         }))},
      &selector);
  EXPECT_TRUE(wrapped->OnEvent(ftxui::Event::ArrowRight));
  EXPECT_TRUE(seen_wrapped);
}

// --- The name tables ---
//
// Nothing else asserts these: they are read through a panel's rendered text,
// where a missing name reads as a blank column rather than a failure.

TEST(JobNameTest, EveryJobHasAName) {
  for (int i = Job_MIN; i <= Job_MAX; ++i) {
    if (!Job_IsValid(i) || i == JOB_UNSPECIFIED) {
      continue;
    }
    Job job = static_cast<Job>(i);
    EXPECT_NE(JobName(job), "Unknown") << Job_Name(job) << " is not named";
  }
}

// The short name is the default everywhere a job is shown, so every job has to
// answer it -- and the ones that need no shortening answer their own name.
TEST(JobNameTest, EveryJobHasAShortName) {
  for (int i = Job_MIN; i <= Job_MAX; ++i) {
    if (!Job_IsValid(i) || i == JOB_UNSPECIFIED) {
      continue;
    }
    Job job = static_cast<Job>(i);
    EXPECT_NE(ShortJobName(job), "Unknown") << Job_Name(job) << " is not named";
    EXPECT_LE(static_cast<int>(ShortJobName(job).size()),
              static_cast<int>(JobName(job).size()))
        << Job_Name(job) << " is longer short than long";
  }
}

TEST(JobNameTest, TheWizardIsSpelledOutInFullAndAbbreviated) {
  EXPECT_EQ(JobName(JOB_ICE_LIGHTNING_WIZARD), "Ice/Lightning Wizard");
  EXPECT_EQ(ShortJobName(JOB_ICE_LIGHTNING_WIZARD), "I/L Wizard");
  EXPECT_EQ(ShortJobName(JOB_SPEARMAN), "Spearman");
}

TEST(StatFieldNameTest, NamesTheFourAllocatableStats) {
  EXPECT_EQ(StatFieldName(STAT_FIELD_STR), "STR");
  EXPECT_EQ(StatFieldName(STAT_FIELD_DEX), "DEX");
  EXPECT_EQ(StatFieldName(STAT_FIELD_INT), "INT");
  EXPECT_EQ(StatFieldName(STAT_FIELD_LUK), "LUK");
  EXPECT_EQ(StatFieldName(STAT_FIELD_UNSPECIFIED), "");
}

// A skill kind added without a look at this reads as a passive, which is what
// happened to the first auto-attack: it inspected as " Passive " and showed no
// effects at any level.
TEST(IsActiveTest, EverythingButAPassiveIsActive) {
  Skill skill;
  skill.set_kind(SKILL_KIND_ATTACK);
  EXPECT_TRUE(IsActive(skill));
  skill.set_kind(SKILL_KIND_ACTIVE);
  EXPECT_TRUE(IsActive(skill));
  skill.set_kind(SKILL_KIND_AUTO_ATTACK);
  EXPECT_TRUE(IsActive(skill));
  skill.set_kind(SKILL_KIND_PASSIVE);
  EXPECT_FALSE(IsActive(skill));
}

// One key per stage: the tab arrives again at every advancement, and having
// seen the first is not having seen the second.
TEST(AdvanceTabKeyTest, EveryStageHasItsOwnKey) {
  EXPECT_NE(AdvanceTabKey(1), AdvanceTabKey(2));
  EXPECT_NE(AdvanceTabKey(1), kShopTabKey);
  EXPECT_FALSE(AdvanceTabKey(1).empty());
}

}  // namespace
}  // namespace ms
