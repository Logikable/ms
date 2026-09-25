#include "src/character/sacred_power.h"

#include "gtest/gtest.h"

namespace ms {
namespace {

// StrategyWiki's Authentic Force table, row by row.
TEST(SacredPowerTest, FollowsTheGmsTable) {
  struct Case {
    int gap;
    double dealt;
    double taken;
  };
  const Case kCases[] = {
      {-200, 0.05, 2.0}, {-95, 0.05, 2.0}, {-90, 0.10, 2.0}, {-51, 0.49, 2.0},
      {-50, 0.50, 1.5},  {-10, 0.90, 1.5}, {-1, 0.99, 1.5},  {0, 1.00, 1.0},
      {1, 1.00, 1.0},    {10, 1.05, 1.0},  {49, 1.24, 1.0},  {50, 1.25, 1.0},
      {500, 1.25, 1.0},
  };
  for (const Case& c : kCases) {
    SCOPED_TRACE(c.gap);
    ForceFactors factors = SacredFactorsFor(200 + c.gap, 200);
    EXPECT_DOUBLE_EQ(factors.damage_dealt, c.dealt);
    EXPECT_DOUBLE_EQ(factors.damage_taken, c.taken);
  }
}

// A map asking nothing takes nothing, and a negative carry reads as none.
TEST(SacredPowerTest, EdgesOfTheTable) {
  ForceFactors none = SacredFactorsFor(0, 0);
  EXPECT_DOUBLE_EQ(none.damage_dealt, 1.0);
  EXPECT_DOUBLE_EQ(none.damage_taken, 1.0);
  EXPECT_DOUBLE_EQ(SacredFactorsFor(-40, 30).damage_dealt, 0.70);
}

}  // namespace
}  // namespace ms
