#include "src/frontend/item_ref.h"

#include <gtest/gtest.h>

#include <memory>
#include <utility>

#include "src/frontend/testing/panel_test_base.h"

namespace ms {
namespace {

class ItemRefTest : public PanelTest {
 protected:
  // A character who can wear sword_ (required level 10, Warrior).
  CharacterInstance MakeWarrior() {
    Character proto;
    proto.set_level(10);
    proto.set_job(JOB_SWORDMAN);
    return CharacterInstance(rng_, std::move(proto));
  }
};

TEST_F(ItemRefTest, ResolvesABagIndex) {
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  ItemRef ref = ItemRef::InBag(0);
  ASSERT_NE(ref.Get(c_), nullptr);
  EXPECT_EQ(ref.Get(c_)->prototype().name(), "Sword");
  EXPECT_NE(ref.GetInstance(c_), nullptr);
}

TEST_F(ItemRefTest, ResolvesAnEquipSlot) {
  CharacterInstance warrior = MakeWarrior();
  warrior.PickUp(std::make_unique<EquipInstance>(sword_));
  warrior.Equip(0);

  ItemRef ref = ItemRef::Equipped(EQUIP_SLOT_PRIMARY_WEAPON);
  ASSERT_NE(ref.Get(warrior), nullptr);
  EXPECT_EQ(ref.Get(warrior)->prototype().name(), "Sword");
  EXPECT_NE(ref.GetInstance(warrior), nullptr);
}

// The ref itself says which kind it is, not the focused panel, so a bag ref
// never reads a slot and vice versa.
TEST_F(ItemRefTest, KnowsWhichHalfItNames) {
  EXPECT_TRUE(ItemRef::Equipped(EQUIP_SLOT_PRIMARY_WEAPON).equipped());
  EXPECT_FALSE(ItemRef::InBag(3).equipped());
  EXPECT_EQ(ItemRef::InBag(3).index(), 3);
  EXPECT_EQ(ItemRef::Equipped(EQUIP_SLOT_PROJECTILE).slot(),
            EQUIP_SLOT_PROJECTILE);
}

TEST_F(ItemRefTest, DefaultRefNamesNothing) {
  ItemRef ref;
  EXPECT_EQ(ref.Get(c_), nullptr);
  EXPECT_EQ(ref.GetInstance(c_), nullptr);
}

// Resolving must survive the item being gone, so an empty slot gives null
// instead of throwing.
TEST_F(ItemRefTest, EmptySlotResolvesToNullRatherThanThrowing) {
  ItemRef ref = ItemRef::Equipped(EQUIP_SLOT_PRIMARY_WEAPON);
  EXPECT_EQ(ref.Get(c_), nullptr);
  EXPECT_EQ(ref.GetInstance(c_), nullptr);
}

TEST_F(ItemRefTest, BagIndexPastTheEndResolvesToNull) {
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  EXPECT_EQ(ItemRef::InBag(5).Get(c_), nullptr);
  EXPECT_EQ(ItemRef::InBag(-1).Get(c_), nullptr);
}

// A trace is a real bag item with no live instance, so the two getters give
// different answers. That is why there are two.
TEST_F(ItemRefTest, TraceHasAnItemButNoInstance) {
  Equip state;
  state.set_stars(17);
  c_.PickUp(std::make_unique<EquipTrace>(sword_, state));

  ItemRef ref = ItemRef::InBag(0);
  EXPECT_NE(ref.Get(c_), nullptr);
  EXPECT_EQ(ref.GetInstance(c_), nullptr);
}

// A 100% scroll, so the outcome doesn't depend on the roll and these tests can
// check exactly which item was changed.
Scroll SureThingScroll() {
  Scroll scroll;
  scroll.set_name("100% ATT");
  scroll.set_success_rate(100);
  scroll.mutable_stats()->set_attack(1);
  return scroll;
}

// The routing tests: with one item worn and another in the bag, the ref alone
// decides which one the action affects.
TEST_F(ItemRefTest, ScrollResolvesTheWornItem) {
  EquipPrototype upgradeable = sword_;
  upgradeable.set_upgrade_slots(1);

  CharacterInstance warrior = MakeWarrior();
  warrior.PickUp(std::make_unique<EquipInstance>(upgradeable));
  warrior.Equip(0);
  warrior.PickUp(std::make_unique<EquipInstance>(upgradeable));

  ScrollItem(warrior, ItemRef::Equipped(EQUIP_SLOT_PRIMARY_WEAPON),
             SureThingScroll());

  EXPECT_EQ(warrior.equipped()
                .at(EQUIP_SLOT_PRIMARY_WEAPON)
                ->equip_state()
                .scroll_successes(),
            1);
  EXPECT_EQ(warrior.inventory()[0].equip_state().scroll_successes(), 0);
}

TEST_F(ItemRefTest, ScrollResolvesTheBagItem) {
  EquipPrototype upgradeable = sword_;
  upgradeable.set_upgrade_slots(1);

  CharacterInstance warrior = MakeWarrior();
  warrior.PickUp(std::make_unique<EquipInstance>(upgradeable));
  warrior.Equip(0);
  warrior.PickUp(std::make_unique<EquipInstance>(upgradeable));

  ScrollItem(warrior, ItemRef::InBag(0), SureThingScroll());

  EXPECT_EQ(warrior.inventory()[0].equip_state().scroll_successes(), 1);
  EXPECT_EQ(warrior.equipped()
                .at(EQUIP_SLOT_PRIMARY_WEAPON)
                ->equip_state()
                .scroll_successes(),
            0);
}

// Star forcing a bag item replaces it in place, so the bag never grows. That
// shows the call went to the bag and not the worn item.
TEST_F(ItemRefTest, StarForceItemReachesTheBagItem) {
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  int before = c_.inventory()[0].stars();

  StarForceItem(c_, ItemRef::InBag(0));

  EXPECT_EQ(c_.inventory().size(), 1);
  // Success raises the stars, failure keeps them, and a destroy leaves a trace
  // at the same stars. Any of those means the call reached this item; an
  // untouched item at another index wouldn't show it.
  EXPECT_GE(c_.inventory()[0].stars(), before);
}

// Both kinds again: the worn copy takes the hammer and the bag copy is
// untouched, then the reverse.
TEST_F(ItemRefTest, HammerItemResolvesEitherHalf) {
  EquipPrototype upgradeable = sword_;
  upgradeable.set_upgrade_slots(1);

  CharacterInstance warrior = MakeWarrior();
  warrior.AddMeso(4 * kGoldenHammerCost);
  warrior.PickUp(std::make_unique<EquipInstance>(upgradeable));
  warrior.Equip(0);
  warrior.PickUp(std::make_unique<EquipInstance>(upgradeable));

  EXPECT_TRUE(
      HammerItem(warrior, ItemRef::Equipped(EQUIP_SLOT_PRIMARY_WEAPON)));
  EXPECT_EQ(
      warrior.equipped().at(EQUIP_SLOT_PRIMARY_WEAPON)->equip_state().hammers(),
      1);
  EXPECT_EQ(warrior.inventory()[0].equip_state().hammers(), 0);

  EXPECT_TRUE(HammerItem(warrior, ItemRef::InBag(0)));
  EXPECT_EQ(warrior.inventory()[0].equip_state().hammers(), 1);
}

// Both kinds once more, with the price: each cube gives its item a potential
// and costs kCubeCost.
TEST_F(ItemRefTest, CubeItemResolvesEitherHalfAndCharges) {
  CharacterInstance warrior = MakeWarrior();
  warrior.AddMeso(2 * kCubeCost);
  warrior.PickUp(std::make_unique<EquipInstance>(sword_));
  warrior.Equip(0);
  warrior.PickUp(std::make_unique<EquipInstance>(sword_));

  EXPECT_TRUE(CubeItem(warrior, ItemRef::Equipped(EQUIP_SLOT_PRIMARY_WEAPON),
                       CubeType::kRed));
  EXPECT_EQ(
      warrior.equipped().at(EQUIP_SLOT_PRIMARY_WEAPON)->potential().rank(),
      POTENTIAL_RANK_RARE);
  EXPECT_EQ(warrior.inventory()[0].potential().rank(),
            POTENTIAL_RANK_UNSPECIFIED);

  EXPECT_TRUE(CubeItem(warrior, ItemRef::InBag(0), CubeType::kRed));
  EXPECT_EQ(warrior.inventory()[0].potential().rank(), POTENTIAL_RANK_RARE);
  EXPECT_EQ(warrior.proto().meso(), 0);
}

// A character who can't afford the price buys nothing, for either kind.
TEST_F(ItemRefTest, CubeItemRefusesWhatThePurseCannotCover) {
  CharacterInstance warrior = MakeWarrior();
  warrior.AddMeso(kCubeCost - 1);
  warrior.PickUp(std::make_unique<EquipInstance>(sword_));
  warrior.Equip(0);

  EXPECT_FALSE(CubeItem(warrior, ItemRef::Equipped(EQUIP_SLOT_PRIMARY_WEAPON),
                        CubeType::kRed));
  EXPECT_EQ(
      warrior.equipped().at(EQUIP_SLOT_PRIMARY_WEAPON)->potential().rank(),
      POTENTIAL_RANK_UNSPECIFIED);
  EXPECT_EQ(warrior.proto().meso(), kCubeCost - 1);
}

}  // namespace
}  // namespace ms
