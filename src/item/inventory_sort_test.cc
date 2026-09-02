#include "src/item/inventory_sort.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "src/item/equip_instance.h"
#include "src/item/item.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"

namespace ms {
namespace {

EquipPrototype Proto(const std::string& name, EquipSlot slot) {
  EquipPrototype proto;
  proto.set_name(name);
  proto.set_equip_slot(slot);
  proto.set_upgrade_slots(10);
  return proto;
}

std::unique_ptr<EquipTabItem> Item(const std::string& name, EquipSlot slot,
                                   int stars = 0, int scrolls = 0) {
  Equip state;
  state.set_stars(stars);
  state.set_scroll_successes(scrolls);
  return std::make_unique<EquipInstance>(Proto(name, slot), state);
}

std::vector<std::string> Names(
    const std::vector<std::unique_ptr<EquipTabItem>>& items) {
  std::vector<std::string> names;
  for (const std::unique_ptr<EquipTabItem>& item : items) {
    names.push_back(item->name());
  }
  return names;
}

// Everything can be worn, so the sort falls through to the keys after it: the
// most stars, then the most scrolls, then the slot's place, then the name.
TEST(SortEquipItemsTest, RanksStarsThenScrollsThenSlotThenName) {
  std::vector<std::unique_ptr<EquipTabItem>> items;
  items.push_back(Item("Hat B", EQUIP_SLOT_HAT));
  items.push_back(Item("Hat A", EQUIP_SLOT_HAT));
  items.push_back(Item("Sword", EQUIP_SLOT_PRIMARY_WEAPON));
  items.push_back(Item("Scrolled Cape", EQUIP_SLOT_CAPE, /*stars=*/0,
                       /*scrolls=*/4));
  items.push_back(Item("Starred Shoes", EQUIP_SLOT_SHOES, /*stars=*/5));
  SortEquipItems(items, [](const EquipPrototype&) { return true; });
  EXPECT_EQ(Names(items),
            (std::vector<std::string>{"Starred Shoes", "Scrolled Cape", "Sword",
                                      "Hat A", "Hat B"}));
}

// What cannot be worn goes below what can, however far along it is, and a
// trace is never wearable even when its prototype would be.
TEST(SortEquipItemsTest, WearableFirstAndTracesBelow) {
  std::vector<std::unique_ptr<EquipTabItem>> items;
  items.push_back(Item("Locked", EQUIP_SLOT_HAT, /*stars=*/20));
  items.push_back(
      std::make_unique<EquipTrace>(Proto("Wearable", EQUIP_SLOT_HAT), Equip{}));
  items.push_back(Item("Wearable", EQUIP_SLOT_HAT));
  SortEquipItems(items, [](const EquipPrototype& proto) {
    return proto.name() == "Wearable";
  });
  ASSERT_EQ(items.size(), 3u);
  EXPECT_EQ(items[0]->name(), "Wearable");
  EXPECT_FALSE(items[0]->is_trace());
  // The trace outranks the locked hat on stars alone; both are below the one
  // item that can be worn.
  EXPECT_TRUE(items[1]->is_trace() || items[2]->is_trace());
  EXPECT_EQ(items[1]->name(), "Locked");
}

StackableItem Stack(const std::string& name, int count,
                    ItemKind kind = ITEM_KIND_UNSPECIFIED) {
  ItemPrototype proto;
  proto.set_name(name);
  proto.set_category(ITEM_CATEGORY_ETC);
  proto.set_kind(kind);
  return StackableItem(proto, count);
}

TEST(SortStacksTest, RanksKindThenCountThenName) {
  std::vector<StackableItem> stacks = {
      Stack("Egg Shell", 5),
      Stack("Zakum's Soul Shard", 2, ITEM_KIND_SOUL_SHARD),
      Stack("Broken Horn", 90),
      Stack("Frozen Weapon Token", 1, ITEM_KIND_TOKEN),
      Stack("Spell Trace", 30, ITEM_KIND_SPELL_TRACE),
  };
  SortStacks(stacks);
  std::vector<std::string> names;
  for (const StackableItem& stack : stacks) {
    names.push_back(stack.name());
  }
  EXPECT_EQ(names, (std::vector<std::string>{
                       "Spell Trace", "Frozen Weapon Token",
                       "Zakum's Soul Shard", "Broken Horn", "Egg Shell"}));
}

// Equal kinds fall to the count, largest first, and equal counts to the name,
// so a tab sorted twice comes out the same both times.
TEST(SortStacksTest, TiesSettleOnCountThenName) {
  std::vector<StackableItem> stacks = {
      Stack("Beta", 10),
      Stack("Alpha", 10),
      Stack("Gamma", 99),
  };
  SortStacks(stacks);
  EXPECT_EQ(stacks[0].name(), "Gamma");
  EXPECT_EQ(stacks[1].name(), "Alpha");
  EXPECT_EQ(stacks[2].name(), "Beta");
}

}  // namespace
}  // namespace ms
