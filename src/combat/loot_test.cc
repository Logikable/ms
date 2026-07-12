#include "src/combat/loot.h"

#include <gtest/gtest.h>

#include "src/protos/mob.pb.h"

namespace ms {
namespace {

TEST(FlushDropsTest, AccumulatesAcrossKillsUntilWhole) {
  double acc = 0.0;
  EXPECT_EQ(FlushDrops(0.5, 1, &acc), 0);  // 0.5 banked, no drop yet
  EXPECT_EQ(FlushDrops(0.5, 1, &acc), 1);  // 0.5 + 0.5 -> 1 drop
  EXPECT_DOUBLE_EQ(acc, 0.0);
}

TEST(FlushDropsTest, FlushesMultipleDropsAndCarriesRemainder) {
  double acc = 0.0;
  EXPECT_EQ(FlushDrops(0.5, 7, &acc), 3);  // 3.5 -> 3 drops
  EXPECT_DOUBLE_EQ(acc, 0.5);
}

TEST(FlushDropsTest, RemainderCarriesIntoNextFlush) {
  double acc = 0.0;
  EXPECT_EQ(FlushDrops(0.25, 10, &acc), 2);  // 2.5 -> 2, 0.5 left
  EXPECT_EQ(FlushDrops(0.25, 10, &acc), 3);  // 0.5 + 2.5 = 3.0 -> 3
  EXPECT_DOUBLE_EQ(acc, 0.0);
}

TEST(FlushDropsTest, NonPositivePerKillYieldsNoDrops) {
  double acc = 0.25;
  EXPECT_EQ(FlushDrops(0.0, 1000, &acc), 0);
  EXPECT_DOUBLE_EQ(acc, 0.25);  // accumulator untouched
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

TEST(ExpectedMesoPerKillTest, LevelOneMobDropsFlatBase) {
  Mob mob;
  mob.set_level(1);
  // 0.60 drop chance * 1 flat meso * no penalty.
  EXPECT_DOUBLE_EQ(ExpectedMesoPerKill(mob, 1), 0.60);
}

TEST(ExpectedMesoPerKillTest, ScalesByLevelBandMean) {
  Mob mob;
  mob.set_level(10);
  // 0.60 * (10 * 2.0 band mean) * no penalty.
  EXPECT_DOUBLE_EQ(ExpectedMesoPerKill(mob, 10), 12.0);
}

TEST(ExpectedMesoPerKillTest, HigherBandUsesHigherMean) {
  Mob mob;
  mob.set_level(21);
  // 0.60 * (21 * 2.5 band mean) * no penalty.
  EXPECT_DOUBLE_EQ(ExpectedMesoPerKill(mob, 21), 31.5);
}

TEST(ExpectedMesoPerKillTest, AppliesLevelPenalty) {
  Mob mob;
  mob.set_level(10);
  // Player 20 levels over: 0.60 * 20 * 0.80 penalty.
  EXPECT_DOUBLE_EQ(ExpectedMesoPerKill(mob, 30), 0.60 * 20.0 * 0.80);
}

}  // namespace
}  // namespace ms
