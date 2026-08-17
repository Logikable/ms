#include "src/item/star_force_cost.h"

#include <gtest/gtest.h>

#include <cstdint>

namespace ms {
namespace {

// Below ten stars GMS charges a plain multiple of the star count, so these are
// exact rather than approximate: a level 100 item pays 40,000 a star over the
// 1,000 every attempt starts at.
TEST(StarForceCostTest, TheFirstTenStarsClimbLinearly) {
  EXPECT_EQ(StarForceCost(100, 0), 41000);
  EXPECT_EQ(StarForceCost(100, 4), 201000);
  EXPECT_EQ(StarForceCost(100, 9), 401000);
  // The level is cubed, so low level gear is priced almost entirely by the
  // flat 1,000 every attempt starts at: a level 30 first star is 2,100.
  EXPECT_EQ(StarForceCost(30, 0), 2100);
}

// Every rung of one item's ladder, worked from the wiki's formula outside this
// code. Whole rather than sampled: each star from 10 up has its own divisor,
// and a sample leaves most of them checked only against themselves.
TEST(StarForceCostTest, MatchesTheQuotedPrices) {
  const int64_t kLevel150[] = {
      136000,    271000,    406000,    541000,    676000,    811000,
      946000,    1081000,   1216000,   1351000,   5470800,   12579800,
      22900700,  38145000,  67400800,  30087200,  35437900,  55134800,
      136714200, 244255300, 62696400,  113738800, 80152000,  89912300,
      100389000, 111603000, 123574700, 136324400, 149872300, 164238100};
  for (int stars = 0; stars < 30; ++stars) {
    EXPECT_EQ(StarForceCost(150, stars), kLevel150[stars])
        << "going from " << stars << " stars to " << stars + 1;
  }
  // A second level, so the L^3 term is held as well as the star term.
  EXPECT_EQ(StarForceCost(200, 29), 389303700);
}

// The shelves are the shape of the whole system: a star gets dearer until it
// reaches a wall, then the next one is cheap again. Without them the top of
// the ladder would just be a straight climb nobody stops on.
TEST(StarForceCostTest, TheWallsAndShelvesLandWhereGmsPutsThem) {
  const int kLevel = 150;
  // 14 -> 15 is the first wall; 15 -> 16 is less than half of it.
  EXPECT_GT(StarForceCost(kLevel, 14), StarForceCost(kLevel, 13));
  EXPECT_LT(StarForceCost(kLevel, 15), StarForceCost(kLevel, 14) / 2);
  // 19 -> 20 is the big one, and 20 -> 21 is a quarter of the price.
  EXPECT_GT(StarForceCost(kLevel, 19), StarForceCost(kLevel, 18));
  EXPECT_LT(StarForceCost(kLevel, 20), StarForceCost(kLevel, 19) / 3);
  // Past 22 one formula runs to the end, so the climb never breaks again.
  for (int stars = 22; stars < 29; ++stars) {
    EXPECT_LT(StarForceCost(kLevel, stars), StarForceCost(kLevel, stars + 1))
        << "at " << stars << " stars";
  }
}

// Every price is quoted to the hundred, including the smallest one there is.
TEST(StarForceCostTest, EveryPriceIsRoundedToAHundred) {
  for (int level = 1; level <= 200; level += 7) {
    for (int stars = 0; stars < 30; ++stars) {
      int64_t cost = StarForceCost(level, stars);
      EXPECT_EQ(cost % 100, 0) << "level " << level << " at " << stars;
      EXPECT_GT(cost, 0) << "level " << level << " at " << stars;
    }
  }
}

// Nothing the game can produce reaches these, so a price here would be a
// price charged for an attempt that cannot happen.
TEST(StarForceCostTest, PricesNothingOutsideTheLadder) {
  EXPECT_EQ(StarForceCost(0, 0), 0);
  EXPECT_EQ(StarForceCost(-5, 0), 0);
  EXPECT_EQ(StarForceCost(150, -1), 0);
  EXPECT_EQ(StarForceCost(150, 30), 0);
}

}  // namespace
}  // namespace ms
