#include "src/item/item.h"

#include <algorithm>
#include <vector>

#include "gtest/gtest.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"

namespace ms {
namespace {

class EquipTraceTest : public ::testing::Test {
 protected:
  EquipPrototype MakeSword() {
    EquipPrototype p;
    p.set_name("Sword");
    p.set_required_level(10);
    p.add_equip_job_categories(EQUIP_JOB_CATEGORY_WARRIOR);
    return p;
  }
};

TEST_F(EquipTraceTest, NameAppendsTraceSuffix) {
  EquipTrace trace(MakeSword(), Equip());
  EXPECT_EQ(trace.name(), "Sword Trace");
}

TEST_F(EquipTraceTest, PrototypeMatchesInput) {
  EquipTrace trace(MakeSword(), Equip());
  EXPECT_EQ(trace.prototype().name(), "Sword");
  EXPECT_EQ(trace.prototype().required_level(), 10);
}

TEST_F(EquipTraceTest, EquipStateMatchesInput) {
  Equip state;
  state.set_stars(3);
  EquipTrace trace(MakeSword(), state);
  EXPECT_EQ(trace.equip_state().stars(), 3);
}

TEST_F(EquipTraceTest, StarsAccessorReflectsState) {
  Equip state;
  state.set_stars(4);
  EquipTrace trace(MakeSword(), state);
  EXPECT_EQ(trace.stars(), 4);
}

TEST_F(EquipTraceTest, MaxStarsMatchesRequiredLevel) {
  EquipTrace trace(MakeSword(), Equip());
  EXPECT_EQ(trace.max_stars(), 5);
}

TEST_F(EquipTraceTest, StatsReflectsBaseAndScrollStats) {
  EquipPrototype proto = MakeSword();
  proto.mutable_base_stats()->set_str(5);
  Equip state;
  state.mutable_scroll_stats()->set_str(3);
  EquipTrace trace(proto, state);
  EXPECT_EQ(trace.stats().str(), 8);
}

TEST_F(EquipTraceTest, StarForceStatGainsInherited) {
  // At 2★, warrior item: kPrimaryStatDeltas[0]+[1] = 4 each for STR and DEX.
  Equip state;
  state.set_stars(2);
  EquipTrace trace(MakeSword(), state);
  EquipStats gains = trace.StarForceStatGains();
  EXPECT_EQ(gains.str(), 4);
  EXPECT_EQ(gains.dex(), 4);
  EXPECT_EQ(gains.int_(), 0);
  EXPECT_EQ(gains.luk(), 0);
}

class StackableItemTest : public ::testing::Test {
 protected:
  ItemPrototype MakeShell() {
    ItemPrototype p;
    p.set_name("Green Snail Shell");
    p.set_category(ITEM_CATEGORY_ETC);
    return p;
  }
};

TEST_F(StackableItemTest, ExposesNameCountAndPrototype) {
  StackableItem stack(MakeShell(), 37);
  EXPECT_EQ(stack.name(), "Green Snail Shell");
  EXPECT_EQ(stack.count(), 37);
  EXPECT_EQ(stack.prototype().category(), ITEM_CATEGORY_ETC);
}

// An explicit max_stack wins; a blank one falls back to the category's, and a
// blank category to a stack of one.
TEST_F(StackableItemTest, MaxStackTakesTheItemsOrItsCategorys) {
  ItemPrototype explicit_stack = MakeShell();
  explicit_stack.set_max_stack(50);
  EXPECT_EQ(StackableItem(explicit_stack, 1).max_stack(), 50);

  EXPECT_EQ(StackableItem(MakeShell(), 1).max_stack(), 200);

  ItemPrototype use;
  use.set_category(ITEM_CATEGORY_USE);
  EXPECT_EQ(StackableItem(use, 1).max_stack(), 9999);

  EXPECT_EQ(StackableItem(ItemPrototype(), 1).max_stack(), 1);
}

// A ring answers with its four slots wherever it is asked from, and every
// other slot with the one it is. The order is the order they fill.
TEST(UpgradeSlotsTest, AShelfIsSlotsPlusHammers) {
  EquipPrototype proto;
  proto.set_upgrade_slots(7);
  Equip state;
  EXPECT_EQ(TotalUpgradeSlots(proto, state), 7);
  state.set_hammers(2);
  EXPECT_EQ(TotalUpgradeSlots(proto, state), 9);
}

TEST(UpgradeSlotsTest, AShelfNeedsScrollsAndSlots) {
  EquipPrototype proto;
  proto.set_upgrade_slots(7);
  EXPECT_TRUE(TakesUpgradeSlots(proto));

  EquipPrototype slotless = proto;
  slotless.set_upgrade_slots(0);
  EXPECT_FALSE(TakesUpgradeSlots(slotless));

  EquipPrototype refuses = proto;
  refuses.add_unsupported_upgrades(UPGRADE_SCROLL);
  EXPECT_FALSE(TakesUpgradeSlots(refuses));
}

TEST(SlotFamilyTest, RingsAndPendantsAnswerWithTheirWholeFamily) {
  const std::vector<EquipSlot> kRings = {EQUIP_SLOT_RING, EQUIP_SLOT_RING_2,
                                         EQUIP_SLOT_RING_3, EQUIP_SLOT_RING_4};
  EXPECT_EQ(SlotFamily(EQUIP_SLOT_RING), kRings);
  EXPECT_EQ(SlotFamily(EQUIP_SLOT_RING_3), kRings) << "asked from anywhere";
  EXPECT_EQ(SlotFamily(EQUIP_SLOT_PENDANT),
            (std::vector<EquipSlot>{EQUIP_SLOT_PENDANT, EQUIP_SLOT_PENDANT_2}));
  EXPECT_EQ(SlotFamily(EQUIP_SLOT_HAT),
            (std::vector<EquipSlot>{EQUIP_SLOT_HAT}));
}

// The base is what a prototype names, and the index is where in the family a
// worn slot sits -- together they are the whole of what a family is for.
TEST(SlotFamilyTest, TheBaseIsWhatAPrototypeNames) {
  EXPECT_EQ(BaseSlot(EQUIP_SLOT_RING_4), EQUIP_SLOT_RING);
  EXPECT_EQ(BaseSlot(EQUIP_SLOT_PENDANT_2), EQUIP_SLOT_PENDANT);
  EXPECT_EQ(BaseSlot(EQUIP_SLOT_HAT), EQUIP_SLOT_HAT);
  EXPECT_EQ(SlotIndex(EQUIP_SLOT_RING), 0);
  EXPECT_EQ(SlotIndex(EQUIP_SLOT_RING_4), 3);
  EXPECT_EQ(SlotIndex(EQUIP_SLOT_PENDANT_2), 1);
  EXPECT_EQ(SlotIndex(EQUIP_SLOT_HAT), 0);
}

// No slot belongs to two families, and none of them is left out of its own.
TEST(SlotFamilyTest, EverySlotIsInExactlyOneFamily) {
  for (int i = 1; i <= EquipSlot_MAX; ++i) {
    EquipSlot slot = static_cast<EquipSlot>(i);
    if (!EquipSlot_IsValid(i)) {
      continue;
    }
    std::vector<EquipSlot> family = SlotFamily(slot);
    EXPECT_EQ(std::count(family.begin(), family.end(), slot), 1)
        << EquipSlot_Name(slot);
    EXPECT_EQ(SlotFamily(BaseSlot(slot)), family) << EquipSlot_Name(slot);
  }
}

}  // namespace
}  // namespace ms
