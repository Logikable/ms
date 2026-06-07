#include "src/frontend/panel_util.h"

#include <gtest/gtest.h>

#include <string>

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

}  // namespace
}  // namespace ms
