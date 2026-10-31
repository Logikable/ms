#include "src/character/sol_erda.h"

#include <cstdint>
#include <random>

#include "gtest/gtest.h"

namespace ms {
namespace {

TEST(SolErdaTest, DropRateLiftsBothRatesAndNeverPastCertainty) {
  EXPECT_DOUBLE_EQ(FragmentsPerKill(0.0), kFragmentDropChance);
  EXPECT_DOUBLE_EQ(FragmentsPerKill(1.0), 2 * kFragmentDropChance);
  EXPECT_DOUBLE_EQ(FragmentsPerKill(-0.5), kFragmentDropChance);
  EXPECT_DOUBLE_EQ(FragmentsPerKill(100000.0), 1.0);
  EXPECT_DOUBLE_EQ(SolErdaEnergyPerKill(0.0),
                   kEnergyPerDrop * kEnergyDropChance);
  EXPECT_DOUBLE_EQ(SolErdaEnergyPerKill(100000.0), kEnergyPerDrop);
}

// The pacing the rates were set by: about 52,500 Fragments and 1,702 Sol Erda
// at 160,000 kills a day, with Fragments the slower of the two.
TEST(SolErdaTest, AFullMatrixTakesAboutAYearWithNoDropRate) {
  constexpr double kKillsPerDay = 160000.0;
  const double fragment_days = 52500.0 / (kKillsPerDay * FragmentsPerKill(0.0));
  const double energy_days =
      1702.0 * kEnergyPerSolErda / (kKillsPerDay * SolErdaEnergyPerKill(0.0));
  EXPECT_GT(fragment_days, 300.0);
  EXPECT_LT(fragment_days, 365.0);
  EXPECT_LT(energy_days, fragment_days);
  EXPECT_GT(energy_days, 0.7 * fragment_days);
  EXPECT_LT(52500.0 / (kKillsPerDay * FragmentsPerKill(0.7)), 200.0);
}

TEST(SolErdaTest, RollsAreWholeDropsAndNothingForNoKills) {
  std::mt19937 rng(7);
  EXPECT_EQ(RollMobFragments(0, 0.0, rng), 0);
  EXPECT_EQ(RollMobSolErdaEnergy(-5, 0.0, rng), 0);
  const int64_t energy = RollMobSolErdaEnergy(1'000'000, 0.0, rng);
  EXPECT_EQ(energy % kEnergyPerDrop, 0);
  // 4,000 drops expected; a binomial sd of about 63.
  EXPECT_NEAR(static_cast<double>(energy / kEnergyPerDrop), 4000.0, 400.0);
  EXPECT_NEAR(static_cast<double>(RollMobFragments(1'000'000, 0.0, rng)),
              1000.0, 200.0);
}

}  // namespace
}  // namespace ms
