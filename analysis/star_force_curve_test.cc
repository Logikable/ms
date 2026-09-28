#include "analysis/star_force_curve.h"

#include <gtest/gtest.h>

namespace ms {
namespace {

// A piece whose stars add almost nothing until 16, then a great deal: what the
// 16th star onwards does to a real item's attack.
double SteepPast15(int to) {
  return to <= 15 ? to : 1000.0 * to;
}

TEST(BestStarRunTest, LooksPastThePoorStarsToTheOnesTheyUnlock) {
  StarRunChoice run = BestStarRun(160, 12, 30, 0.0, true, SteepPast15);
  EXPECT_GT(run.to, 15);
  EXPECT_DOUBLE_EQ(run.step_cost, StarForceRunTo(160, 12, 13).meso);
  EXPECT_GT(run.cost, run.step_cost);
}

TEST(BestStarRunTest, StopsShortOfABoomNothingCanRecover) {
  StarRunChoice run = BestStarRun(160, 12, 30, 0.0, false, SteepPast15);
  EXPECT_GT(run.to, 12);
  EXPECT_LE(run.to, 15);
  EXPECT_EQ(StarForceRunTo(160, 12, run.to).booms, 0.0);
}

// A boom's spare is part of the price, so a dear enough one keeps the run
// below the stars that can destroy the item.
TEST(BestStarRunTest, ADearSpareKeepsTheRunSafe) {
  StarRunChoice run = BestStarRun(160, 12, 30, 1e15, true, SteepPast15);
  EXPECT_LE(run.to, 15);
}

TEST(BestStarRunTest, NothingPastTheCeiling) {
  EXPECT_EQ(BestStarRun(160, 15, 15, 0.0, true, SteepPast15).to, 0);
  EXPECT_LE(BestStarRun(160, 12, 14, 0.0, true, SteepPast15).to, 14);
}

}  // namespace
}  // namespace ms
