#include "analysis/soul_plan.h"

#include <vector>

#include "gtest/gtest.h"

namespace ms {
namespace {

// One roll is the mean; more rolls approach the best line, and never pass it.
TEST(SoulPlanTest, RollsAreWorthMoreTheMoreThereAre) {
  const std::vector<double> values = {1, 2, 3, 4, 5, 6, 7};
  EXPECT_EQ(SoulRollWorth(values, 0), 0.0);
  EXPECT_DOUBLE_EQ(SoulRollWorth(values, 1), 4.0);
  // Two rolls: keep a 5, 6 or 7, else take the second roll's mean of 4.
  EXPECT_DOUBLE_EQ(SoulRollWorth(values, 2), (4 * 4.0 + 5 + 6 + 7) / 7);
  double last = 0.0;
  for (int rolls = 1; rolls <= 100; ++rolls) {
    const double worth = SoulRollWorth(values, rolls);
    EXPECT_GE(worth, last) << rolls;
    EXPECT_LE(worth, 7.0);
    last = worth;
  }
  EXPECT_GT(last, 6.99);
}

// Holding the best line, or a soul the rolls can't beat, keeps the shards.
TEST(SoulPlanTest, ASoulIsRolledOnlyWhenTheOddsBeatIt) {
  const std::vector<double> values = {1, 1, 1, 1, 1, 1, 10};
  EXPECT_FALSE(ShouldRollSoul(10, values, 20));
  EXPECT_FALSE(ShouldRollSoul(5, values, 1)) << "one roll's mean is 16/7";
  EXPECT_TRUE(ShouldRollSoul(5, values, 20)) << "twenty rolls nearly surely "
                                                "land the 10";
  EXPECT_TRUE(ShouldRollSoul(0, values, 1));
  EXPECT_FALSE(ShouldRollSoul(0, values, 0));
}

}  // namespace
}  // namespace ms
