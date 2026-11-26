#include "src/character/matrix/sol_erda.h"

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

TEST(SolErdaTest, RollsAreWholeDropsAndNothingForNoKills) {
  std::mt19937 rng(7);
  EXPECT_EQ(RollMobFragments(0, 0.0, rng), 0);
  EXPECT_EQ(RollMobSolErdaEnergy(-5, 0.0, rng), 0);
  const int64_t energy = RollMobSolErdaEnergy(1'000'000, 0.0, rng);
  EXPECT_EQ(energy % kEnergyPerDrop, 0);
  // Within ten percent of the mean; the binomial sd is about 3%.
  const double drops = 1'000'000 * kEnergyDropChance;
  EXPECT_NEAR(static_cast<double>(energy / kEnergyPerDrop), drops, drops / 10);
  const double fragments = 1'000'000 * kFragmentDropChance;
  EXPECT_NEAR(static_cast<double>(RollMobFragments(1'000'000, 0.0, rng)),
              fragments, fragments / 5);
}

}  // namespace
}  // namespace ms
