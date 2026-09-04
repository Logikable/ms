#include "src/character/v_matrix.h"

#include <cstdint>
#include <random>

#include "gtest/gtest.h"

namespace ms {
namespace {

TEST(VMatrixTest, DropRateLiftsTheRateAndNeverPastCertainty) {
  EXPECT_DOUBLE_EQ(VPointsPerKill(0.0), kVPointDropChance);
  EXPECT_DOUBLE_EQ(VPointsPerKill(1.0), 2 * kVPointDropChance);
  // A negative rate is a bug elsewhere; it must not cut the payment.
  EXPECT_DOUBLE_EQ(VPointsPerKill(-0.5), kVPointDropChance);
  EXPECT_DOUBLE_EQ(VPointsPerKill(10000.0), 1.0);
}

TEST(VMatrixTest, TheRollPaysAboutWhatTheRateSays) {
  std::mt19937 rng(20260903);
  const int64_t kills = 2'000'000;
  int64_t points = RollMobVPoints(kills, /*item_drop_pct=*/0.0, rng);
  // 0.1% of two million is 2,000, and the binomial's own spread is about 45.
  EXPECT_NEAR(points, kills * kVPointDropChance, 250);
  // Doubled drop rate, doubled payment.
  int64_t lifted = RollMobVPoints(kills, /*item_drop_pct=*/1.0, rng);
  EXPECT_NEAR(lifted, 2 * kills * kVPointDropChance, 350);

  EXPECT_EQ(RollMobVPoints(0, 0.0, rng), 0);
  EXPECT_EQ(RollMobVPoints(-5, 0.0, rng), 0);
}

}  // namespace
}  // namespace ms
