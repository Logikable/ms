#include "src/character/arcane_force.h"

#include <gtest/gtest.h>

namespace ms {
namespace {

// A map outside Arcane River requires nothing, and the factors leave the fight
// unchanged.
TEST(ArcaneForceTest, NoRequirementLeavesTheFightAlone) {
  ForceFactors none = ArcaneFactorsFor(0, 0);
  EXPECT_DOUBLE_EQ(none.damage_dealt, 1.0);
  EXPECT_DOUBLE_EQ(none.damage_taken, 1.0);
}

TEST(ArcaneForceTest, TheFactorTableStepsWithThePercentageMet) {
  struct Case {
    int owned;
    double dealt;
    double taken;
  };
  // Against a requirement of 100, so `owned` equals the percent met.
  const Case cases[] = {
      {0, 0.10, 2.8},   {9, 0.10, 2.8},   {10, 0.30, 2.4},  {29, 0.30, 2.4},
      {30, 0.60, 1.8},  {50, 0.70, 1.6},  {70, 0.80, 1.4},  {99, 0.80, 1.4},
      {100, 1.00, 1.0}, {109, 1.00, 1.0}, {110, 1.10, 0.8}, {130, 1.30, 0.4},
      {150, 1.50, 0.0}, {900, 1.50, 0.0},
  };
  for (const Case& c : cases) {
    ForceFactors factors = ArcaneFactorsFor(c.owned, 100);
    EXPECT_DOUBLE_EQ(factors.damage_dealt, c.dealt) << c.owned;
    EXPECT_DOUBLE_EQ(factors.damage_taken, c.taken) << c.owned;
  }
}

// The percent is rounded down, so the point just before a step gives nothing.
TEST(ArcaneForceTest, ThePercentageRoundsDown) {
  EXPECT_DOUBLE_EQ(ArcaneFactorsFor(38, 130).damage_dealt, 0.30);
  EXPECT_DOUBLE_EQ(ArcaneFactorsFor(39, 130).damage_dealt, 0.60);
  // A level 1 symbol's 30 is exactly what the first Vanishing Journey map
  // requires.
  EXPECT_DOUBLE_EQ(ArcaneFactorsFor(30, 30).damage_dealt, 1.00);
}

}  // namespace
}  // namespace ms
