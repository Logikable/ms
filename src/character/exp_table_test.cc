#include "src/character/exp_table.h"

#include <gtest/gtest.h>

namespace ms {
namespace {

TEST(ExpToNextLevelTest, ReturnsZeroBelowLevelOne) {
  EXPECT_EQ(ExpToNextLevel(0), 0);
  EXPECT_EQ(ExpToNextLevel(-1), 0);
}

TEST(ExpToNextLevelTest, ReturnsZeroAtAndAboveMaxLevel) {
  EXPECT_EQ(ExpToNextLevel(kMaxLevel), 0);
  EXPECT_EQ(ExpToNextLevel(kMaxLevel + 1), 0);
}

// The table's ends, and the two plateaus where the cost stops climbing for
// five levels at an advancement.
TEST(ExpToNextLevelTest, MatchesTheTableAtItsLandmarks) {
  EXPECT_EQ(ExpToNextLevel(1), 15);
  EXPECT_EQ(ExpToNextLevel(299), 1737759854037637LL);
  for (int level = 10; level <= 14; ++level) {
    EXPECT_EQ(ExpToNextLevel(level), 1242) << "at level " << level;
  }
  for (int level = 30; level <= 34; ++level) {
    EXPECT_EQ(ExpToNextLevel(level), 19112) << "at level " << level;
  }
}

// 5th job starts at 200 with a large jump from level 199's cost.
TEST(ExpToNextLevelTest, FifthJobBoundaryJump) {
  EXPECT_EQ(ExpToNextLevel(199), 571115568);
  EXPECT_EQ(ExpToNextLevel(200), 2207026470LL);
}

}  // namespace
}  // namespace ms
