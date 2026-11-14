#include "src/character/honor.h"

#include <cstdint>
#include <random>

#include "gtest/gtest.h"

namespace ms {
namespace {

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
