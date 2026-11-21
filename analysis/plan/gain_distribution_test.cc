#include "analysis/plan/gain_distribution.h"

#include <gtest/gtest.h>

namespace ms {
namespace {

// A quarter at 0, half at 10, a quarter at 20, with the 10s added in two parts
// that Finish merges.
GainDistribution Spread() {
  GainDistribution gains;
  gains.Add(20.0, 1.0);
  gains.Add(10.0, 1.0);
  gains.Add(0.0, 1.0);
  gains.Add(10.0, 1.0);
  gains.Finish();
  return gains;
}

TEST(GainDistributionTest, QueriesWeighTheirChances) {
  GainDistribution gains = Spread();
  EXPECT_DOUBLE_EQ(gains.low(), 0.0);
  EXPECT_DOUBLE_EQ(gains.high(), 20.0);
  EXPECT_DOUBLE_EQ(gains.MeanMax(-1.0), 10.0);
  EXPECT_DOUBLE_EQ(gains.MeanMax(15.0), 0.75 * 15.0 + 0.25 * 20.0);
  EXPECT_DOUBLE_EQ(gains.Below(10.0), 0.25);
  EXPECT_DOUBLE_EQ(gains.Below(10.5), 0.75);
  EXPECT_DOUBLE_EQ(gains.Stopped(10.0), 0.5 * 10.0 + 0.25 * 20.0);
  EXPECT_DOUBLE_EQ(gains.Stopped(25.0), 0.0);
}

TEST(GainDistributionTest, ScalingMovesEveryGain) {
  GainDistribution gains = Spread();
  gains.Scale(0.5);
  EXPECT_DOUBLE_EQ(gains.high(), 10.0);
  EXPECT_DOUBLE_EQ(gains.MeanMax(-1.0), 5.0);
  EXPECT_DOUBLE_EQ(gains.Stopped(5.0), 0.5 * 5.0 + 0.25 * 10.0);
}

// A rare gain keeps its own small chance rather than a sample's share.
TEST(GainDistributionTest, ARareGainKeepsItsChance) {
  GainDistribution gains;
  gains.Add(0.0, 999.0);
  gains.Add(1000.0, 1.0);
  gains.Finish();
  EXPECT_DOUBLE_EQ(gains.Below(1000.0), 0.999);
  EXPECT_NEAR(gains.MeanMax(0.0), 1.0, 1e-12);
}

}  // namespace
}  // namespace ms
