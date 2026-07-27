#include "src/frontend/panel_util.h"

#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/frontend/colors.h"

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
TEST(DisplayStatForTest, FindsAFieldWhoseAccessorIsNamedDifferently) {
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

}  // namespace
}  // namespace ms
