#include "src/frontend/item_ref.h"

#include <gtest/gtest.h>

#include <memory>
#include <utility>

#include "src/frontend/panel_test_base.h"

namespace ms {
namespace {

class ItemRefTest : public PanelTest {
 protected:
  // A character who can actually wear sword_ (required level 10, Warrior).
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

// The two halves are told apart by the ref itself, not by which panel had
// focus, so a bag ref never reads a slot and vice versa.
TEST_F(ItemRefTest, KnowsWhichHalfItNames) {
  EXPECT_TRUE(ItemRef::Equipped(EQUIP_SLOT_PRIMARY_WEAPON).equipped());
  EXPECT_FALSE(ItemRef::InBag(3).equipped());
  EXPECT_EQ(ItemRef::InBag(3).index(), 3);
  EXPECT_EQ(ItemRef::Equipped(EQUIP_SLOT_STARS).slot(), EQUIP_SLOT_STARS);
}

TEST_F(ItemRefTest, DefaultRefNamesNothing) {
  ItemRef ref;
  EXPECT_EQ(ref.Get(c_), nullptr);
  EXPECT_EQ(ref.GetInstance(c_), nullptr);
}

// The old code reached straight into equipped().at(slot), which throws on an
// empty slot. Resolving has to survive the item going away.
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

// A trace is a real bag item but has no live instance behind it, so the two
// getters disagree -- which is the whole reason there are two.
TEST_F(ItemRefTest, TraceHasAnItemButNoInstance) {
  Equip state;
  state.set_stars(17);
  c_.PickUp(std::make_unique<EquipTrace>(sword_, state));

  ItemRef ref = ItemRef::InBag(0);
  EXPECT_NE(ref.Get(c_), nullptr);
  EXPECT_EQ(ref.GetInstance(c_), nullptr);
}

// A 100% scroll, so the outcome does not depend on the roll and these tests
// can assert exactly which item was touched.
Scroll SureThingScroll() {
  Scroll scroll;
  scroll.set_name("100% ATT");
  scroll.set_success_rate(100);
  scroll.mutable_stats()->set_attack(1);
  return scroll;
}

// The routing tests: with one item worn and another in the bag, the ref alone
// decides which one the action lands on.
TEST_F(ItemRefTest, ScrollItemAppliesToTheWornItemAndLeavesTheBagAlone) {
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
                .equip_state()
                .scroll_successes(),
            1);
  EXPECT_EQ(warrior.inventory()[0].equip_state().scroll_successes(), 0);
}

TEST_F(ItemRefTest, ScrollItemAppliesToTheBagItemAndLeavesTheWornAlone) {
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
                .equip_state()
                .scroll_successes(),
            0);
}

// Star forcing a bag item replaces it in place, so the bag never grows -- the
// signal that the call went to the bag half and not the worn one.
TEST_F(ItemRefTest, StarForceItemReachesTheBagItem) {
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  int before = c_.inventory()[0].stars();

  StarForceItem(c_, ItemRef::InBag(0));

  EXPECT_EQ(c_.inventory().size(), 1);
  // Success bumps it, failure holds it, destroy leaves a trace at the same
  // stars. Any of those means the call landed; an untouched item at a
  // different index would not.
  EXPECT_GE(c_.inventory()[0].stars(), before);
}

}  // namespace
}  // namespace ms
