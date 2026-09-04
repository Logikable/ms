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

// GMS's own totals, which are what the three ladders have to come to.
TEST(VMatrixTest, EachLadderCostsWhatGmsChargesForTheWholeNode) {
  EXPECT_EQ(MaxVNodeLevel(V_NODE_KIND_COMMON), 30);
  EXPECT_EQ(MaxVNodeLevel(V_NODE_KIND_JOB), 30);
  EXPECT_EQ(MaxVNodeLevel(V_NODE_KIND_BOOST), 60);
  EXPECT_EQ(MaxVNodeLevel(V_NODE_KIND_UNSPECIFIED), 0);

  EXPECT_EQ(VNodeCost(V_NODE_KIND_COMMON, 0, 30), 193);
  EXPECT_EQ(VNodeCost(V_NODE_KIND_JOB, 0, 30), 186);
  EXPECT_EQ(VNodeCost(V_NODE_KIND_BOOST, 0, 60), 80);

  // The rungs the bands change on, and the two first levels that differ.
  EXPECT_EQ(VNodeStepCost(V_NODE_KIND_JOB, 1), 0);
  EXPECT_EQ(VNodeStepCost(V_NODE_KIND_COMMON, 1), 7);
  EXPECT_EQ(VNodeStepCost(V_NODE_KIND_COMMON, 10), 4);
  EXPECT_EQ(VNodeStepCost(V_NODE_KIND_COMMON, 11), 6);
  EXPECT_EQ(VNodeStepCost(V_NODE_KIND_COMMON, 21), 9);
  EXPECT_EQ(VNodeStepCost(V_NODE_KIND_BOOST, 40), 1);
  EXPECT_EQ(VNodeStepCost(V_NODE_KIND_BOOST, 41), 2);
  // Nothing is charged past the end of the ladder, or before its start.
  EXPECT_EQ(VNodeStepCost(V_NODE_KIND_COMMON, 31), 0);
  EXPECT_EQ(VNodeStepCost(V_NODE_KIND_BOOST, 61), 0);
  EXPECT_EQ(VNodeCost(V_NODE_KIND_COMMON, 30, 30), 0);
  EXPECT_EQ(VNodeCost(V_NODE_KIND_COMMON, 30, 20), 0);
  // A part of the climb costs what those levels cost and nothing else.
  EXPECT_EQ(VNodeCost(V_NODE_KIND_COMMON, 9, 12), 4 + 6 + 6);
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
