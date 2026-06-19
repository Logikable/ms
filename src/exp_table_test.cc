#include "src/exp_table.h"

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

TEST(ExpToNextLevelTest, LevelOneCostsCorrectly) {
  EXPECT_EQ(ExpToNextLevel(1), 15);
}

TEST(ExpToNextLevelTest, FirstJobPlateauIsFlat) {
  // Levels 10-14 all cost 1242 before growth resumes at 15.
  for (int level = 10; level <= 14; ++level) {
    EXPECT_EQ(ExpToNextLevel(level), 1242) << "at level " << level;
  }
}

TEST(ExpToNextLevelTest, SecondJobPlateauIsFlat) {
  // Levels 30-34 all cost 19112 before growth resumes at 35.
  for (int level = 30; level <= 34; ++level) {
    EXPECT_EQ(ExpToNextLevel(level), 19112) << "at level " << level;
  }
}

TEST(ExpToNextLevelTest, FifthJobBoundaryJump) {
  // 5th job starts at 200 with a large jump from level 199's cost.
  EXPECT_EQ(ExpToNextLevel(199), 571115568);
  EXPECT_EQ(ExpToNextLevel(200), 2207026470LL);
}

TEST(ExpToNextLevelTest, LastEntryIsCorrect) {
  EXPECT_EQ(ExpToNextLevel(299), 1737759854037637LL);
}

}  // namespace
}  // namespace ms
