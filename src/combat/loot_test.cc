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

// Drop rate lifts the chance and not the copies: a boss's table is paid once,
// so a certain drop stays one however much drop gear is worn, and a rate
// stating two of something states two.
TEST(BossDropRateTest, LiftsTheChanceButNeverTheCertainty) {
  EXPECT_DOUBLE_EQ(BossDropRate(1.0, 2.0), 1.0);
  EXPECT_DOUBLE_EQ(BossDropRate(0.4, 1.0), 0.8);
  EXPECT_DOUBLE_EQ(BossDropRate(0.4, 4.0), 1.0);
  EXPECT_DOUBLE_EQ(BossDropRate(2.0, 3.0), 2.0);
  EXPECT_DOUBLE_EQ(BossDropRate(2.5, 1.0), 3.0);
  EXPECT_DOUBLE_EQ(BossDropRate(0.5, 0.0), 0.5);
}

TEST(BossDropRateTest, NothingComesOfNothing) {
  EXPECT_DOUBLE_EQ(BossDropRate(0.0, 1.0), 0.0);
  EXPECT_DOUBLE_EQ(BossDropRate(-1.0, 1.0), 0.0);
  EXPECT_DOUBLE_EQ(BossDropRate(std::numeric_limits<double>::quiet_NaN(), 1.0),
                   0.0);
  EXPECT_DOUBLE_EQ(BossDropRate(0.5, std::numeric_limits<double>::quiet_NaN()),
                   0.5);
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
  int64_t total = RollMeso(mob, 200000, 0.0, rng);
  double expected = ExpectedMesoPerKill(mob, 0.0) * 200000;
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
    int64_t meso = RollMeso(mob, 1, 0.0, rng);
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

// Both halves of the module read one drop chance, so a test that only compares
// them to each other would pass at any rate. This one measures it.
TEST(RollMesoTest, PaysSixKillsInTen) {
  Mob mob;
  mob.set_level(70);
  std::mt19937 rng(7);
  int paid = 0;
  const int kKills = 20000;
  for (int i = 0; i < kKills; ++i) {
    if (RollMeso(mob, 1, 0.0, rng) > 0) {
      ++paid;
    }
  }
  EXPECT_NEAR(static_cast<double>(paid) / kKills, 0.60, 0.02);
}

// Drop rate buys a better chance of a drop, not a bigger one, and it stops
// buying anything once every kill already pays.
TEST(MesoDropChanceTest, RisesWithDropRateAndCapsAtEveryKill) {
  EXPECT_DOUBLE_EQ(MesoDropChance(0.0), 0.60);
  EXPECT_DOUBLE_EQ(MesoDropChance(0.50), 0.90);
  EXPECT_DOUBLE_EQ(MesoDropChance(2.0), 1.0);
  // A rate nothing granted, and one no arithmetic should have produced.
  EXPECT_DOUBLE_EQ(MesoDropChance(-1.0), 0.60);
  EXPECT_DOUBLE_EQ(MesoDropChance(std::numeric_limits<double>::quiet_NaN()),
                   0.60);
}

TEST(RollMesoTest, DropRatePaysMoreKills) {
  Mob mob;
  mob.set_level(70);
  std::mt19937 rng(9);
  int paid = 0;
  const int kKills = 20000;
  for (int i = 0; i < kKills; ++i) {
    if (RollMeso(mob, 1, 0.50, rng) > 0) {
      ++paid;
    }
  }
  EXPECT_NEAR(static_cast<double>(paid) / kKills, 0.90, 0.02);
  // Capped, so every kill pays and none of them pays twice.
  EXPECT_GT(RollMeso(mob, 500, 2.0, rng), 500 * 6.0 * 70 * 4.8);
}

// The chance moves and the amount does not: what a paying kill is worth is the
// band's business, and drop rate has none of it.
TEST(ExpectedMesoPerKillTest, DropRateScalesTheChanceOnly) {
  Mob mob;
  mob.set_level(70);
  EXPECT_DOUBLE_EQ(ExpectedMesoPerKill(mob, 0.50),
                   ExpectedMesoPerKill(mob, 0.0) * 1.5);
  EXPECT_DOUBLE_EQ(ExpectedMesoPerKill(mob, 2.0), 6.0 * 1.0 * 70 * 6.0);
}

TEST(RollMesoTest, ALevelOneMobPaysAFlatMeso) {
  Mob mob;
  mob.set_level(1);
  std::mt19937 rng(11);
  int64_t total = RollMeso(mob, 10000, 0.0, rng);
  // 60% of kills, one meso each, at the Heroic world's 6x.
  EXPECT_NEAR(total, 36000, 1000);
}

// The 6.0 in each of these is the Heroic world rate, written out rather than
// folded into the number so a change to it reads as itself.
TEST(ExpectedMesoPerKillTest, LevelOneMobDropsFlatBase) {
  Mob mob;
  mob.set_level(1);
  // 0.60 drop chance * 1 flat meso.
  EXPECT_DOUBLE_EQ(ExpectedMesoPerKill(mob, 0.0), 6.0 * 0.60);
}

TEST(ExpectedMesoPerKillTest, ScalesByLevelBandMean) {
  Mob mob;
  mob.set_level(10);
  EXPECT_DOUBLE_EQ(ExpectedMesoPerKill(mob, 0.0), 6.0 * 0.60 * 10 * 2.0);
  mob.set_level(21);  // the next band up
  EXPECT_DOUBLE_EQ(ExpectedMesoPerKill(mob, 0.0), 6.0 * 0.60 * 21 * 2.5);
}

// What the inspect panel shows as a mob's meso, which is the amount a drop is
// worth rather than the per-kill mean -- the 60% is a row of its own there.
TEST(MeanMesoPerDropTest, LeavesTheDropChanceOut) {
  Mob mob;
  mob.set_level(10);
  EXPECT_DOUBLE_EQ(MeanMesoPerDrop(mob), 6.0 * 10 * 2.0);
  EXPECT_DOUBLE_EQ(ExpectedMesoPerKill(mob, 0.0), 0.60 * MeanMesoPerDrop(mob));
  mob.set_level(1);
  EXPECT_DOUBLE_EQ(MeanMesoPerDrop(mob), 6.0);
}

}  // namespace
}  // namespace ms
