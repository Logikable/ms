#include "src/character/consumables.h"

#include <gtest/gtest.h>

#include <random>
#include <utility>

#include "src/character/character.h"
#include "src/protos/character.pb.h"

namespace ms {
namespace {

CharacterInstance MakeCharacter(std::mt19937& rng, int level, int64_t meso) {
  Character proto;
  proto.set_level(level);
  proto.set_meso(meso);
  return CharacterInstance(rng, std::move(proto));
}

TEST(ConsumablesTest, EveryPotHasATableRowAndTheyOpenInOrder) {
  int last = 0;
  for (const ConsumableInfo& info : AllConsumables()) {
    EXPECT_EQ(ConsumableInfoFor(info.type), &info);
    EXPECT_GT(info.price, 0);
    EXPECT_GT(info.permanent_price, info.price);
    EXPECT_GE(info.unlock_level, last);
    last = info.unlock_level;
  }
  EXPECT_EQ(ConsumableInfoFor(CONSUMABLE_TYPE_UNSPECIFIED), nullptr);
}

TEST(ConsumablesTest, APotWaitsForItsOwnLevel) {
  std::mt19937 rng(1);
  CharacterInstance c = MakeCharacter(rng, 169, 0);
  EXPECT_FALSE(c.ToggleConsumable(CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION));
  EXPECT_FALSE(c.ConsumableActive(CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION));

  CharacterInstance open = MakeCharacter(rng, 170, 0);
  EXPECT_TRUE(open.ToggleConsumable(CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION));
  EXPECT_TRUE(
      open.ConsumableInEffect(CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION));
  // The green potion is twenty levels further out.
  EXPECT_FALSE(open.ToggleConsumable(CONSUMABLE_TYPE_EXTREME_GREEN_POTION));
  EXPECT_FALSE(
      open.ToggleConsumable(CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION));
  EXPECT_FALSE(
      open.ConsumableInEffect(CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION));
}

// Buying it is all or nothing, and it stops the charging for good.
TEST(ConsumablesTest, BuyingOutrightEndsTheRent) {
  std::mt19937 rng(1);
  CharacterInstance c = MakeCharacter(rng, 170, 99'999'999);
  ASSERT_TRUE(c.ToggleConsumable(CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION));
  EXPECT_FALSE(c.BuyConsumable(CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION));
  EXPECT_EQ(c.meso(), 99'999'999);

  c.AddMeso(1);
  ASSERT_TRUE(c.BuyConsumable(CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION));
  EXPECT_EQ(c.meso(), 0);
  EXPECT_TRUE(c.ConsumableOwned(CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION));
  // Owned and switched on: it works and costs nothing.
  EXPECT_TRUE(c.ConsumableInEffect(CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION));
  EXPECT_EQ(c.ChargeConsumable(CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION, 60),
            0);
  // And it is not bought twice.
  c.AddMeso(500'000'000);
  EXPECT_FALSE(c.BuyConsumable(CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION));
  EXPECT_EQ(c.meso(), 500'000'000);

  // Both halves survive a save: an owned pot the player has to switch back on
  // every launch is an owned pot they will think they lost.
  Character saved = c.ToProto();
  ASSERT_EQ(saved.consumables().owned_size(), 1);
  EXPECT_EQ(saved.consumables().owned(0),
            CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION);
  ASSERT_EQ(saved.consumables().active_size(), 1);
  EXPECT_EQ(saved.consumables().active(0),
            CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION);
}

// A pot that is off costs nothing, and one that is on is charged per proc.
TEST(ConsumablesTest, OnlyASwitchedOnPotCharges) {
  std::mt19937 rng(1);
  CharacterInstance c = MakeCharacter(rng, 190, 10'000'000);
  EXPECT_EQ(c.ChargeConsumable(CONSUMABLE_TYPE_EXTREME_GREEN_POTION, 1), 0);

  ASSERT_TRUE(c.ToggleConsumable(CONSUMABLE_TYPE_EXTREME_GREEN_POTION));
  EXPECT_EQ(c.ChargeConsumable(CONSUMABLE_TYPE_EXTREME_GREEN_POTION, 1),
            1'000'000);
  EXPECT_EQ(c.meso(), 9'000'000);
}

// The purse pays what it has and stops at nothing. The pot is still on.
TEST(ConsumablesTest, AShortPurseGetsItAtADiscount) {
  std::mt19937 rng(1);
  CharacterInstance c = MakeCharacter(rng, 190, 400'000);
  ASSERT_TRUE(c.ToggleConsumable(CONSUMABLE_TYPE_EXTREME_GREEN_POTION));

  EXPECT_EQ(c.ChargeConsumable(CONSUMABLE_TYPE_EXTREME_GREEN_POTION, 1),
            400'000);
  EXPECT_EQ(c.meso(), 0);
  EXPECT_EQ(c.ChargeConsumable(CONSUMABLE_TYPE_EXTREME_GREEN_POTION, 1), 0);
  EXPECT_TRUE(c.ConsumableInEffect(CONSUMABLE_TYPE_EXTREME_GREEN_POTION));
}

// The live tick charges three times a second. A fraction of a meso left on
// the floor each time would cost the player a tenth of a percent of the price.
TEST(ConsumablesTest, PartOfASecondCarriesRatherThanRoundingAway) {
  std::mt19937 rng(1);
  CharacterInstance c = MakeCharacter(rng, 170, 1'000'000);
  ASSERT_TRUE(c.ToggleConsumable(CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION));

  int64_t paid = 0;
  for (int tick = 0; tick < 300; ++tick) {
    paid += c.ChargeConsumable(CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION,
                               1.0 / 3.0);
  }
  EXPECT_EQ(paid, 100'000);  // a hundred seconds at a thousand each
  EXPECT_EQ(c.meso(), 900'000);
}

}  // namespace
}  // namespace ms
