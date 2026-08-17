#include "src/combat/loot.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <random>

#include "src/protos/mob.pb.h"

namespace ms {
namespace {

// A rate below one is a chance per kill, so a big enough sample lands near
// the rate and no single kill is owed anything.
TEST(RollDropsTest, PaysTheRateOverManyKills) {
  std::mt19937 rng(1234);
  int64_t dropped = RollDrops(0.4, 100000, rng);
  EXPECT_NEAR(dropped, 40000, 1500);
}

// The old accumulator paid the 5,000th kill, every time. A roll does not.
TEST(RollDropsTest, DoesNotPayOnASchedule) {
  std::mt19937 rng(7);
  bool differed = false;
  for (int trial = 0; trial < 20 && !differed; ++trial) {
    differed = RollDrops(0.5, 10, rng) != 5;
  }
  EXPECT_TRUE(differed) << "twenty batches all paid exactly the mean";
}

// A rate above one owes a drop outright and rolls only what is left over.
TEST(RollDropsTest, ARateAboveOnePaysItsWholePartEveryTime) {
  std::mt19937 rng(99);
  for (int trial = 0; trial < 10; ++trial) {
    int64_t dropped = RollDrops(2.5, 100, rng);
    EXPECT_GE(dropped, 200);
    EXPECT_LE(dropped, 300);
  }
}

TEST(RollDropsTest, NothingComesOfNothing) {
  std::mt19937 rng(3);
  EXPECT_EQ(RollDrops(0.0, 1000, rng), 0);
  EXPECT_EQ(RollDrops(-1.0, 1000, rng), 0);
  EXPECT_EQ(RollDrops(std::numeric_limits<double>::quiet_NaN(), 1000, rng), 0);
  EXPECT_EQ(RollDrops(0.5, 0, rng), 0);
}

// The roll has to average what the curve says the economy pays, or the sims
// measure one game and the player plays another.
TEST(RollMesoTest, AveragesTheExpectedAmount) {
  Mob mob;
  mob.set_level(70);
  std::mt19937 rng(2024);
  int64_t total = RollMeso(mob, 70, 200000, rng);
  double expected = ExpectedMesoPerKill(mob, 70) * 200000;
  EXPECT_NEAR(total / expected, 1.0, 0.01);
}

// Each paying kill lands inside the band's range -- a fifth either side of the
// mean -- rather than on the mean itself.
TEST(RollMesoTest, OneKillPaysInsideTheBandOrNothing) {
  Mob mob;
  mob.set_level(70);  // band mean 6.0, so 4.8 to 7.2 times the level
  std::mt19937 rng(5);
  bool paid_off_the_mean = false;
  for (int trial = 0; trial < 200; ++trial) {
    int64_t meso = RollMeso(mob, 70, 1, rng);
    if (meso == 0) {
      continue;  // the 40% of kills that pay nothing
    }
    EXPECT_GE(meso, 6.0 * 70 * 4.8);
    EXPECT_LE(meso, 6.0 * 70 * 7.2);
    if (meso != 6 * 70 * 6) {
      paid_off_the_mean = true;
    }
  }
  EXPECT_TRUE(paid_off_the_mean) << "every drop paid the band mean exactly";
}

TEST(RollMesoTest, ALevelOneMobPaysAFlatMeso) {
  Mob mob;
  mob.set_level(1);
  std::mt19937 rng(11);
  int64_t total = RollMeso(mob, 1, 10000, rng);
  // 60% of kills, one meso each, at the Heroic world's 6x.
  EXPECT_NEAR(total, 36000, 1000);
}

// Out-levelled far enough and the mob pays nothing at all, so there is
// nothing to roll.
TEST(RollMesoTest, PaysNothingPastTheLevelPenalty) {
  Mob mob;
  mob.set_level(10);
  std::mt19937 rng(13);
  EXPECT_EQ(RollMeso(mob, 40, 10000, rng), 0);
}

TEST(MesoLevelPenaltyTest, NoPenaltyWithinTenLevels) {
  EXPECT_DOUBLE_EQ(MesoLevelPenalty(0), 1.0);
  EXPECT_DOUBLE_EQ(MesoLevelPenalty(10), 1.0);
  EXPECT_DOUBLE_EQ(MesoLevelPenalty(-10), 1.0);
}

TEST(MesoLevelPenaltyTest, OverLevelLinearBand) {
  // 11..20: 2% reduction per level past 10.
  EXPECT_DOUBLE_EQ(MesoLevelPenalty(11), 0.98);
  EXPECT_DOUBLE_EQ(MesoLevelPenalty(20), 0.80);
}

TEST(MesoLevelPenaltyTest, OverLevelIrregularBand) {
  // 21..29 follow the fixed reduction table.
  EXPECT_NEAR(MesoLevelPenalty(21), 0.75, 1e-9);  // -25%
  EXPECT_NEAR(MesoLevelPenalty(29), 0.03, 1e-9);  // -97%
}

TEST(MesoLevelPenaltyTest, OverLevelThirtyPlusYieldsNothing) {
  EXPECT_DOUBLE_EQ(MesoLevelPenalty(30), 0.0);
  EXPECT_DOUBLE_EQ(MesoLevelPenalty(50), 0.0);
}

TEST(MesoLevelPenaltyTest, UnderLevelGentlerBand) {
  // 11..20 below: 3% reduction per level.
  EXPECT_DOUBLE_EQ(MesoLevelPenalty(-11), 0.97);
  EXPECT_DOUBLE_EQ(MesoLevelPenalty(-20), 0.70);
}

TEST(MesoLevelPenaltyTest, UnderLevelSteeperBand) {
  // 21..33 below: 5% reduction per level.
  EXPECT_NEAR(MesoLevelPenalty(-21), 0.65, 1e-9);
  EXPECT_NEAR(MesoLevelPenalty(-33), 0.05, 1e-9);
}

TEST(MesoLevelPenaltyTest, UnderLevelThirtyFourPlusYieldsNothing) {
  EXPECT_DOUBLE_EQ(MesoLevelPenalty(-34), 0.0);
  EXPECT_DOUBLE_EQ(MesoLevelPenalty(-60), 0.0);
}

// The 6.0 in each of these is the Heroic world rate, written out rather than
// folded into the number so a change to it reads as itself.
TEST(ExpectedMesoPerKillTest, LevelOneMobDropsFlatBase) {
  Mob mob;
  mob.set_level(1);
  // 0.60 drop chance * 1 flat meso * no penalty.
  EXPECT_DOUBLE_EQ(ExpectedMesoPerKill(mob, 1), 6.0 * 0.60);
}

TEST(ExpectedMesoPerKillTest, ScalesByLevelBandMean) {
  Mob mob;
  mob.set_level(10);
  EXPECT_DOUBLE_EQ(ExpectedMesoPerKill(mob, 10), 6.0 * 0.60 * 10 * 2.0);
  mob.set_level(21);  // the next band up
  EXPECT_DOUBLE_EQ(ExpectedMesoPerKill(mob, 21), 6.0 * 0.60 * 21 * 2.5);
}

TEST(ExpectedMesoPerKillTest, AppliesLevelPenalty) {
  Mob mob;
  mob.set_level(10);
  // Player 20 levels over: 0.60 * 20 * 0.80 penalty.
  EXPECT_DOUBLE_EQ(ExpectedMesoPerKill(mob, 30), 6.0 * 0.60 * 20.0 * 0.80);
}

}  // namespace
}  // namespace ms
