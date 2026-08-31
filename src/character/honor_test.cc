#include "src/character/honor.h"

#include <cstdint>
#include <random>

#include "gtest/gtest.h"

namespace ms {
namespace {

TEST(HonorForLevelUpTest, SevenHundredUntilTheSixties) {
  EXPECT_EQ(HonorForLevelUp(2), 700);
  EXPECT_EQ(HonorForLevelUp(59), 700);
  EXPECT_EQ(HonorForLevelUp(60), 800);
  EXPECT_EQ(HonorForLevelUp(69), 800);
  EXPECT_EQ(HonorForLevelUp(70), 900);
}

TEST(HonorForLevelUpTest, AHundredMoreEveryBand) {
  EXPECT_EQ(HonorForLevelUp(160), 1800);
  EXPECT_EQ(HonorForLevelUp(200), 2200);
}

TEST(HonorForLevelUpTest, NothingClimbsToLevelOne) {
  EXPECT_EQ(HonorForLevelUp(1), 0);
  EXPECT_EQ(HonorForLevelUp(0), 0);
}

TEST(HonorForLevelsTest, TheWholeSpan) {
  EXPECT_EQ(HonorForLevels(58, 61), 700 + 800 + 800);
  EXPECT_EQ(HonorForLevels(5, 5), 0);
  EXPECT_EQ(HonorForLevels(10, 5), 0);
}

// The projection the sources were tuned against: what the climb alone pays by
// the level Inner Ability opens at, and by the cap.
TEST(HonorForLevelsTest, TheClimbToTheCap) {
  EXPECT_EQ(HonorForLevels(1, 160), 167400);
  EXPECT_EQ(HonorForLevels(1, 200), 245800);
}

TEST(RollMobHonorTest, PaysTheRateOverManyKills) {
  std::mt19937 rng(7);
  int64_t honor = RollMobHonor(200000, rng);
  EXPECT_NEAR(static_cast<double>(honor) / 200000.0, kMobHonorPerKill, 0.02);
}

TEST(RollMobHonorTest, PaysInWholeDrops) {
  std::mt19937 rng(11);
  EXPECT_EQ(RollMobHonor(100, rng) % kMobHonorPerDrop, 0);
}

TEST(RollMobHonorTest, NothingComesOfNoKills) {
  std::mt19937 rng(1);
  EXPECT_EQ(RollMobHonor(0, rng), 0);
  EXPECT_EQ(RollMobHonor(-5, rng), 0);
}

}  // namespace
}  // namespace ms
