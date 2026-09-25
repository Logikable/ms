#include "src/combat/loot.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <random>

#include "src/protos/mob.pb.h"

namespace ms {
namespace {

// One drop table line of each kind. Only the rate and which oneof field is set
// matter here, since BossDropRate never looks up the name.
MobDrop Equip(double per_kill) {
  MobDrop drop;
  drop.set_equip("gear");
  drop.set_per_kill(per_kill);
  return drop;
}

MobDrop Item(double per_kill) {
  MobDrop drop;
  drop.set_item("token");
  drop.set_per_kill(per_kill);
  return drop;
}

// A rate below one is a chance per kill. A large sample lands near the rate,
// but no single kill is guaranteed a drop.
TEST(RollDropsTest, PaysTheRateOverManyKills) {
  std::mt19937 rng(1234);
  int64_t dropped = RollDrops(0.4, 100000, rng);
  EXPECT_NEAR(dropped, 40000, 1500);
}

// Drops are rolled, not paid out on a fixed schedule such as every 5,000th
// kill.
TEST(RollDropsTest, DoesNotPayOnASchedule) {
  std::mt19937 rng(7);
  bool differed = false;
  for (int trial = 0; trial < 20 && !differed; ++trial) {
    differed = RollDrops(0.5, 10, rng) != 5;
  }
  EXPECT_TRUE(differed) << "twenty batches all paid exactly the mean";
}

// A rate above one pays its whole part every time and rolls only the remainder.
TEST(RollDropsTest, ARateAboveOnePaysItsWholePartEveryTime) {
  std::mt19937 rng(99);
  for (int trial = 0; trial < 10; ++trial) {
    int64_t dropped = RollDrops(2.5, 100, rng);
    EXPECT_GE(dropped, 200);
    EXPECT_LE(dropped, 300);
  }
}

// A boss table is paid once per clear, so drop rate raises the chance of a gear
// drop but never past certain, and never adds a second copy.
TEST(BossDropRateTest, GearTakesTheChanceAndNeverTheCertainty) {
  EXPECT_DOUBLE_EQ(BossDropRate(Equip(1.0), 2.0), 1.0);
  EXPECT_DOUBLE_EQ(BossDropRate(Equip(0.4), 1.0), 0.8);
  EXPECT_DOUBLE_EQ(BossDropRate(Equip(0.4), 4.0), 1.0);
  EXPECT_DOUBLE_EQ(BossDropRate(Equip(2.0), 3.0), 2.0);
  EXPECT_DOUBLE_EQ(BossDropRate(Equip(2.5), 1.0), 3.0);
  EXPECT_DOUBLE_EQ(BossDropRate(Equip(0.5), 0.0), 0.5);
}

// Tokens and soul shards stack, so drop rate adds copies. A certain drop at
// 250% rate gives two outright and a coin flip for a third.
TEST(BossDropRateTest, AStackableTakesTheCopies) {
  EXPECT_DOUBLE_EQ(BossDropRate(Item(1.0), 1.5), 2.5);
  EXPECT_DOUBLE_EQ(BossDropRate(Item(1.0), 2.0), 3.0);
  EXPECT_DOUBLE_EQ(BossDropRate(Item(0.4), 1.0), 0.8);
  EXPECT_DOUBLE_EQ(BossDropRate(Item(0.4), 4.0), 2.0);
  EXPECT_DOUBLE_EQ(BossDropRate(Item(1.0), 0.0), 1.0);
}

TEST(BossDropRateTest, NothingComesOfNothing) {
  EXPECT_DOUBLE_EQ(BossDropRate(Item(0.0), 1.0), 0.0);
  EXPECT_DOUBLE_EQ(BossDropRate(Equip(-1.0), 1.0), 0.0);
  double nan = std::numeric_limits<double>::quiet_NaN();
  EXPECT_DOUBLE_EQ(BossDropRate(Item(nan), 1.0), 0.0);
  EXPECT_DOUBLE_EQ(BossDropRate(Equip(0.5), nan), 0.5);
  EXPECT_DOUBLE_EQ(BossDropRate(Item(0.5), nan), 0.5);
}

TEST(RollDropsTest, NothingComesOfNothing) {
  std::mt19937 rng(3);
  EXPECT_EQ(RollDrops(0.0, 1000, rng), 0);
  EXPECT_EQ(RollDrops(-1.0, 1000, rng), 0);
  EXPECT_EQ(RollDrops(std::numeric_limits<double>::quiet_NaN(), 1000, rng), 0);
  EXPECT_EQ(RollDrops(0.5, 0, rng), 0);
}

// The roll must average what the meso curve says, or the sims would measure a
// different economy from the one the player sees.
TEST(RollMesoTest, AveragesTheExpectedAmount) {
  Mob mob;
  mob.set_level(70);
  std::mt19937 rng(2024);
  int64_t total = RollMeso(mob, 200000, 0.0, rng);
  double expected = ExpectedMesoPerKill(mob, 0.0) * 200000;
  EXPECT_NEAR(total / expected, 1.0, 0.01);
}

// Each paying kill lands within a fifth either side of the band's mean, not on
// the mean itself.
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

// Both halves of the module read the same drop chance, so a test that only
// compares them would pass at any rate. This one measures the rate directly.
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

// Drop rate raises the chance of a meso drop, not its size, and stops helping
// once every kill pays.
TEST(MesoDropChanceTest, RisesWithDropRateAndCapsAtEveryKill) {
  EXPECT_DOUBLE_EQ(MesoDropChance(0.0), 0.60);
  EXPECT_DOUBLE_EQ(MesoDropChance(0.50), 0.90);
  EXPECT_DOUBLE_EQ(MesoDropChance(2.0), 1.0);
  // A negative or NaN rate should never happen, and falls back to the base
  // chance.
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
  // Capped, so every kill pays and none pays twice.
  EXPECT_GT(RollMeso(mob, 500, 2.0, rng), 500 * 6.0 * 70 * 4.8);
}

// Drop rate changes the chance only. The amount a paying kill gives comes from
// the mob's level band.
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

// The 6.0 in each of these is the Heroic world rate, written out so a change to
// it is easy to spot.
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

// The meso amount the inspect panel shows for a mob. This is what one drop is
// worth, not the per-kill average; the panel shows the 60% chance on its own
// row.
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
