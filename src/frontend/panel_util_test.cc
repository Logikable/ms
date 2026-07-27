#include "src/frontend/panel_util.h"

#include <gtest/gtest.h>

#include <string>

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
