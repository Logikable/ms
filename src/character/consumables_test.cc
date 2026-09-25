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

TEST(ConsumablesTest, EveryBuffHasATableRowAndTheyOpenInOrder) {
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

TEST(ConsumablesTest, ABuffWaitsForItsOwnLevel) {
  std::mt19937 rng(1);
  CharacterInstance c = MakeCharacter(rng, 169, 0);
  EXPECT_FALSE(c.ToggleConsumable(CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION));
  EXPECT_FALSE(c.ConsumableActive(CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION));

  CharacterInstance open = MakeCharacter(rng, 170, 0);
  EXPECT_TRUE(open.ToggleConsumable(CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION));
  EXPECT_TRUE(
      open.ConsumableInEffect(CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION));
  // The green potion unlocks twenty levels later, and the totem fifty.
  EXPECT_FALSE(open.ToggleConsumable(CONSUMABLE_TYPE_EXTREME_GREEN_POTION));
  EXPECT_FALSE(open.ToggleConsumable(CONSUMABLE_TYPE_WILD_TOTEM));
  EXPECT_FALSE(
      open.ToggleConsumable(CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION));
  EXPECT_FALSE(
      open.ConsumableInEffect(CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION));
}

// Buying outright is all or nothing, and it ends the charges permanently.
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
  // And it can't be bought twice.
  c.AddMeso(500'000'000);
  EXPECT_FALSE(c.BuyConsumable(CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION));
  EXPECT_EQ(c.meso(), 500'000'000);

  // Both states survive a save: an owned buff the player must switch on again
  // every launch would look lost.
  Character saved = c.ToProto();
  ASSERT_EQ(saved.consumables().owned_size(), 1);
  EXPECT_EQ(saved.consumables().owned(0),
            CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION);
  ASSERT_EQ(saved.consumables().active_size(), 1);
  EXPECT_EQ(saved.consumables().active(0),
            CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION);
}

// A buff that is off costs nothing, and one that is on is charged per use.
TEST(ConsumablesTest, OnlyASwitchedOnBuffCharges) {
  std::mt19937 rng(1);
  CharacterInstance c = MakeCharacter(rng, 190, 10'000'000);
  EXPECT_EQ(c.ChargeConsumable(CONSUMABLE_TYPE_EXTREME_GREEN_POTION, 1), 0);

  ASSERT_TRUE(c.ToggleConsumable(CONSUMABLE_TYPE_EXTREME_GREEN_POTION));
  EXPECT_EQ(c.ChargeConsumable(CONSUMABLE_TYPE_EXTREME_GREEN_POTION, 1),
            1'000'000);
  EXPECT_EQ(c.meso(), 9'000'000);
}

// The purse pays what it has and stops at zero. The buff stays on.
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

// One timer pays for every buff charged per second, and for none of the ones
// charged on entering a boss fight.
TEST(ConsumablesTest, TheFarmingClockChargesWhatIsPaidForBySecond) {
  std::mt19937 rng(1);
  CharacterInstance c = MakeCharacter(rng, 220, 10'000'000);
  ASSERT_TRUE(c.ToggleConsumable(CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION));
  ASSERT_TRUE(c.ToggleConsumable(CONSUMABLE_TYPE_EXTREME_GREEN_POTION));
  ASSERT_TRUE(c.ToggleConsumable(CONSUMABLE_TYPE_WILD_TOTEM));

  // Ten seconds of the potion's thousand and the totem's two thousand.
  EXPECT_EQ(c.ChargeFarmingConsumables(10.0), 30'000);
  EXPECT_EQ(c.meso(), 9'970'000);
}

// The live tick charges three times a second. Dropping a fraction of a meso
// each time would cost the player a tenth of a percent of the price.
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
