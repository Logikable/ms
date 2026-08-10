#include "src/item/inventory.h"

#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "src/protos/equip.pb.h"

namespace ms {
namespace {

EquipPrototype MakeProto(const std::string& name) {
  EquipPrototype proto;
  proto.set_name(name);
  proto.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  return proto;
}

TEST(InventoryInstanceTest, StartsEmptyAndGrowsOnAdd) {
  InventoryInstance inv;
  EXPECT_TRUE(inv.empty());
  EXPECT_EQ(inv.size(), 0);
  inv.add(std::make_unique<EquipInstance>(MakeProto("Sword")));
  EXPECT_FALSE(inv.empty());
  EXPECT_EQ(inv.size(), 1);
  EXPECT_EQ(inv[0].prototype().name(), "Sword");
}

// equip_instance() is the live-item accessor, so it answers for an ordinary
// item and refuses everything else: a trace, and either end of the range. The
// const overload is the same function and is checked alongside it.
TEST(InventoryInstanceTest, EquipInstanceAnswersOnlyForALiveItem) {
  InventoryInstance inv;
  inv.add(std::make_unique<EquipInstance>(MakeProto("Sword")));
  inv.add(std::make_unique<EquipTrace>(MakeProto("Axe"), Equip{}));
  EXPECT_NE(inv.equip_instance(0), nullptr);
  EXPECT_EQ(inv.equip_instance(1), nullptr) << "a trace is not a live item";
  EXPECT_EQ(inv.equip_instance(-1), nullptr);
  EXPECT_EQ(inv.equip_instance(2), nullptr);
  const InventoryInstance& const_inv = inv;
  EXPECT_NE(const_inv.equip_instance(0), nullptr);
}

TEST(InventoryInstanceTest, RemoveEquipExtractsItem) {
  InventoryInstance inv;
  inv.add(std::make_unique<EquipInstance>(MakeProto("Sword")));
  std::unique_ptr<EquipTabItem> item = inv.remove_equip(0);
  EXPECT_EQ(item->prototype().name(), "Sword");
  EXPECT_TRUE(inv.empty());
}

TEST(InventoryInstanceTest, AddWithIndexInsertsAtPosition) {
  InventoryInstance inv;
  inv.add(std::make_unique<EquipInstance>(MakeProto("Sword")));
  inv.add(std::make_unique<EquipInstance>(MakeProto("Bow")));
  inv.add(std::make_unique<EquipInstance>(MakeProto("Axe")), 1);
  ASSERT_EQ(inv.size(), 3);
  EXPECT_EQ(inv[0].prototype().name(), "Sword");
  EXPECT_EQ(inv[1].prototype().name(), "Axe");
  EXPECT_EQ(inv[2].prototype().name(), "Bow");
}

TEST(InventoryInstanceTest, SetReplacesItem) {
  InventoryInstance inv;
  inv.add(std::make_unique<EquipInstance>(MakeProto("Sword")));
  inv.set(0, std::make_unique<EquipInstance>(MakeProto("Axe")));
  EXPECT_EQ(inv[0].prototype().name(), "Axe");
  EXPECT_EQ(inv.size(), 1);
}

TEST(InventoryInstanceTest, TracesReturnsOnlyTraces) {
  InventoryInstance inv;
  inv.add(std::make_unique<EquipInstance>(MakeProto("Sword")));
  inv.add(std::make_unique<EquipTrace>(MakeProto("Axe"), Equip{}));
  std::vector<const EquipTrace*> traces = inv.traces();
  ASSERT_EQ(traces.size(), 1u);
  EXPECT_EQ(traces[0]->prototype().name(), "Axe");
}

}  // namespace
}  // namespace ms
