#include "src/frontend/widgets/format.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace ms {
namespace {

// --- PadRight ---

TEST(PadRightTest, PadsOrTruncatesToTheWidth) {
  EXPECT_EQ(PadRight("hi", 5), "hi   ");
  EXPECT_EQ(PadRight("hello", 5), "hello");
  EXPECT_EQ(PadRight("toolong", 4), "tool");
  EXPECT_EQ(PadRight("", 3), "   ");
}

// Both pads count columns, so a row with a multibyte cell in it still lines up
// with the rows around it. The cell above is the same width on screen.
TEST(PadRightTest, PadsInColumnsNotBytes) {
  EXPECT_EQ(PadRight("📜", 4), "📜  ");
  EXPECT_EQ(PadRight("Émeraude", 9), "Émeraude ");
  EXPECT_EQ(PadLeft("📜", 4), "  📜");
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

// --- FormatCompact ---

TEST(FormatCompactTest, WritesOutAnythingUnderTwoMillion) {
  EXPECT_EQ(FormatCompact(0), "0");
  EXPECT_EQ(FormatCompact(700000), "700,000");
  EXPECT_EQ(FormatCompact(1999999), "1,999,999");
}

TEST(FormatCompactTest, ThreeDigitsAndNoTrailingZeros) {
  EXPECT_EQ(FormatCompact(5600000), "5.6M");
  EXPECT_EQ(FormatCompact(7000000), "7M");
  EXPECT_EQ(FormatCompact(12300000), "12.3M");
  EXPECT_EQ(FormatCompact(500000000), "500M");
  EXPECT_EQ(FormatCompact(-5600000), "-5.6M");
}

// A unit is only taken up at two thousand of the one below, so the number
// keeps the unit a reader can weigh it in.
TEST(FormatCompactTest, ClimbsAUnitAtTwoThousandOfTheLast) {
  EXPECT_EQ(FormatCompact(1570000000LL), "1570M");
  EXPECT_EQ(FormatCompact(2340000000LL), "2.34B");
  EXPECT_EQ(FormatCompact(1570000000000LL), "1570B");
  EXPECT_EQ(FormatCompact(2340000000000LL), "2.34T");
  EXPECT_EQ(FormatCompact(2340000000000000LL), "2.34Q");
}

// --- DropChance ---

TEST(DropChanceTest, TrimsToTheDecimalsTheRateNeeds) {
  EXPECT_EQ(DropChance(1.0), "100%");
  EXPECT_EQ(DropChance(0.4), "40%");
  EXPECT_EQ(DropChance(0.1), "10%");
  EXPECT_EQ(DropChance(0.00025), "0.025%");
  EXPECT_EQ(DropChance(0.0001), "0.01%");
}

TEST(DropChanceTest, KeepsATinyRateAChance) {
  EXPECT_EQ(DropChance(0.0000001), "<0.001%");
  EXPECT_EQ(DropChance(0.0), "0%");
}

// --- FormatClock ---

TEST(FormatClockTest, CountsInMinutesAndSeconds) {
  EXPECT_EQ(FormatClock(300.0), "5:00");
  EXPECT_EQ(FormatClock(299.5), "5:00");
  EXPECT_EQ(FormatClock(65.0), "1:05");
  EXPECT_EQ(FormatClock(9.2), "0:10");
  // Only actually being out of time reads 0:00.
  EXPECT_EQ(FormatClock(0.1), "0:01");
  EXPECT_EQ(FormatClock(0.0), "0:00");
  EXPECT_EQ(FormatClock(-5.0), "0:00");
}

// --- FormatMeso ---

TEST(FormatMesoTest, PrefixesIndicatorAndFormatsValue) {
  EXPECT_NE(FormatMeso(1234567).find("1,234,567"), std::string::npos);
}

// --- AppendStat ---

TEST(AppendStatTest, WritesAStatAndSeparatesTheNext) {
  std::string out;
  AppendStat(out, 5, "ATT");
  EXPECT_EQ(out, "+5 ATT");
  AppendStat(out, 7, "DEX");
  EXPECT_EQ(out, "+5 ATT  +7 DEX");
}

TEST(AppendStatTest, NothingPositiveWritesNothing) {
  std::string out;
  AppendStat(out, 0, "ATT");
  AppendStat(out, -1, "DEX");
  EXPECT_TRUE(out.empty());
}

TEST(AppendStatTest, SkipsZeroInMiddle) {
  std::string out;
  AppendStat(out, 3, "STR");
  AppendStat(out, 0, "DEX");
  AppendStat(out, 2, "LUK");
  EXPECT_EQ(out, "+3 STR  +2 LUK");
}

TEST(WrapBalancedTest, BreaksNearTheMiddle) {
  EXPECT_EQ(WrapBalanced("Aquatic Letter Eye Accessory", 26, 4),
            (std::vector<std::string>{"Aquatic Letter", "Eye Accessory"}));
  EXPECT_EQ(WrapBalanced("Condensed Power Crystal", 26, 4),
            (std::vector<std::string>{"Condensed", "Power Crystal"}));
}

// Fewest lines first, and the tail is what the last line has to leave free.
// "Zakum's Soul Shard" fits on one line; ask for room beside it and it stops.
TEST(WrapBalancedTest, TheTailIsOnlyChargedToTheLastLine) {
  EXPECT_EQ(WrapBalanced("Zakum's Soul Shard", 26, 5),
            (std::vector<std::string>{"Zakum's Soul Shard"}));
  EXPECT_EQ(WrapBalanced("Zakum's Soul Shard", 26, 12),
            (std::vector<std::string>{"Zakum's", "Soul Shard"}));
}

// A word with nowhere to fit runs over rather than being cut, and nothing at
// all is one empty line rather than none.
// The margin is what makes two rows read as one name. It is charged to the
// lines that carry it, so the balance is of what ends up on screen.
TEST(WrapBalancedTest, EveryLineButTheFirstCarriesTheMargin) {
  EXPECT_EQ(WrapBalanced("Aquatic Letter Eye Accessory", 26, 4, 2),
            (std::vector<std::string>{"Aquatic Letter", "  Eye Accessory"}));
  EXPECT_EQ(WrapBalanced("Zakum's Soul Shard", 26, 5, 2),
            (std::vector<std::string>{"Zakum's Soul Shard"}));
  // Room for "Beginner Sword" and the margin, but not for both at once.
  EXPECT_EQ(WrapBalanced("A Beginner Sword", 14, 0, 2),
            (std::vector<std::string>{"A Beginner", "  Sword"}));
}

TEST(WrapBalancedTest, AWordTooLongKeepsItsOwnLine) {
  EXPECT_EQ(WrapBalanced("Supercalifragilistic sword", 10, 0),
            (std::vector<std::string>{"Supercalifragilistic", "sword"}));
  EXPECT_EQ(WrapBalanced("", 10, 0), (std::vector<std::string>{""}));
}

}  // namespace
}  // namespace ms
