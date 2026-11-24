#include "analysis/star_force_curve.h"

#include <gtest/gtest.h>

#include <functional>
#include <limits>

namespace ms {
namespace {

// A piece whose stars add almost nothing until 16, then a great deal: what the
// 16th star onwards does to a real item's attack.
double SteepPast15(int to) {
  return to <= 15 ? to : 1000.0 * to;
}

constexpr double kAnyCopies = std::numeric_limits<double>::infinity();

TEST(BestStarRunTest, LooksPastThePoorStarsToTheOnesTheyUnlock) {
  StarRunChoice run = BestStarRun(160, 12, 30, 0.0, kAnyCopies, SteepPast15);
  EXPECT_GT(run.to, 15);
  EXPECT_DOUBLE_EQ(run.step_cost, StarForceRunTo(160, 12, 13).meso);
  EXPECT_GT(run.cost, run.step_cost);
}

TEST(BestStarRunTest, StopsShortOfABoomNothingCanRecover) {
  StarRunChoice run = BestStarRun(160, 12, 30, 0.0, 0.0, SteepPast15);
  EXPECT_GT(run.to, 12);
  EXPECT_LE(run.to, 15);
  EXPECT_EQ(StarForceRunTo(160, 12, run.to).booms, 0.0);
}

// A boom's spare is part of the price, so a dear enough one keeps the run
// below the stars that can destroy the item.
TEST(BestStarRunTest, ADearSpareKeepsTheRunSafe) {
  StarRunChoice run = BestStarRun(160, 12, 30, 1e15, kAnyCopies, SteepPast15);
  EXPECT_LE(run.to, 15);
}

// A piece worth little until `at` stars, then a great deal.
std::function<double(int)> WorthAt(int at) {
  return [at](int to) { return to >= at ? 1e12 : to; };
}

// Each spare in hand lets the run risk one more expected boom: about seven
// take 17 to 22, and with copies no limit the run goes on to 25.
TEST(BestStarRunTest, TheSparesInHandCapTheRun) {
  StarRunChoice few = BestStarRun(160, 17, 30, 0.0, 3.0, WorthAt(22));
  StarRunChoice enough = BestStarRun(160, 17, 30, 0.0, 8.0, WorthAt(22));
  StarRunChoice any = BestStarRun(160, 17, 30, 0.0, kAnyCopies, WorthAt(25));
  EXPECT_LT(few.to, 22);
  EXPECT_LE(StarForceRunTo(160, 17, few.to).booms, 3.0);
  EXPECT_EQ(enough.to, 22);
  EXPECT_EQ(any.to, 25);
}

TEST(BestStarRunTest, NothingPastTheCeiling) {
  EXPECT_EQ(BestStarRun(160, 15, 15, 0.0, kAnyCopies, SteepPast15).to, 0);
  EXPECT_LE(BestStarRun(160, 12, 14, 0.0, kAnyCopies, SteepPast15).to, 14);
}

}  // namespace
}  // namespace ms

namespace ms {
namespace {

// Nothing below 15 stars can boom, so no spare is ever needed to get there.
TEST(StarForceBoomsCoveringTest, NoSpareBelowTheFirstBoom) {
  EXPECT_EQ(StarForceBoomsCovering(0, 15, 0.99), 0);
}

// Nine runs in ten need more spares than the average run, and more stars need
// more.
TEST(StarForceBoomsCoveringTest, TheTailIsAboveTheMean) {
  const double mean = StarForceRunTo(200, 0, 22).booms;
  const int half = StarForceBoomsCovering(0, 22, 0.5);
  const int most = StarForceBoomsCovering(0, 22, 0.9);
  EXPECT_LE(half, mean);
  EXPECT_GT(most, mean);
  EXPECT_GT(StarForceBoomsCovering(0, 23, 0.9), most);
}

}  // namespace
}  // namespace ms
