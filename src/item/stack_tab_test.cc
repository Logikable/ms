#include "src/item/stack_tab.h"

#include <gtest/gtest.h>

#include <map>
#include <string>

#include "src/protos/item.pb.h"

namespace ms {
namespace {

ItemPrototype Proto(const std::string& name, int max_stack) {
  ItemPrototype proto;
  proto.set_name(name);
  proto.set_max_stack(max_stack);
  return proto;
}

TEST(StackTabTest, AddingTopsUpBeforeOpeningStacks) {
  StackTab tab;
  ItemPrototype shell = Proto("Green Snail Shell", 100);
  EXPECT_EQ(tab.Add(shell, 60), 60);
  EXPECT_EQ(tab.size(), 1);

  // The open stack takes 40 without costing a slot; the rest opens one.
  EXPECT_EQ(tab.Add(shell, 60), 60);
  EXPECT_EQ(tab.size(), 2);
  EXPECT_EQ(tab[0].count(), 100);
  EXPECT_EQ(tab[1].count(), 20);
  EXPECT_EQ(tab.Count("Green Snail Shell"), 120);
  EXPECT_EQ(tab.Add(shell, 0), 0);
}

// A full tab still absorbs what its open stacks can hold, and loses the rest.
TEST(StackTabTest, AFullTabTakesWhatFits) {
  StackTab tab;
  ItemPrototype shell = Proto("Green Snail Shell", 100);
  for (int i = 0; i < kTabCapacity; ++i) {
    tab.Add(Proto("Drop " + std::to_string(i), 100), 100);
  }
  EXPECT_TRUE(tab.full());
  EXPECT_EQ(tab.room(), 0);
  EXPECT_EQ(tab.RoomFor(shell), 0);
  EXPECT_EQ(tab.Add(shell, 50), 0);

  StackTab room;
  room.Add(shell, 10);
  for (int i = 1; i < kTabCapacity; ++i) {
    room.Add(Proto("Drop " + std::to_string(i), 100), 100);
  }
  EXPECT_EQ(room.RoomFor(shell), 90) << "the open stack, and no free slot";
  EXPECT_EQ(room.Add(shell, 200), 90);
}

// Spending is all or nothing, takes from the newest stack first, and drops a
// stack it empties.
TEST(StackTabTest, SpendingEmptiesStacksAndLeavesNoZeroRow) {
  StackTab tab;
  ItemPrototype shell = Proto("Green Snail Shell", 100);
  tab.Add(shell, 150);
  ASSERT_EQ(tab.size(), 2);

  EXPECT_FALSE(tab.Spend("Green Snail Shell", 200));
  EXPECT_EQ(tab.Count("Green Snail Shell"), 150);
  EXPECT_FALSE(tab.Spend("Green Snail Shell", 0));
  EXPECT_FALSE(tab.Spend("Nothing At All", 1));

  EXPECT_TRUE(tab.Spend("Green Snail Shell", 60));
  EXPECT_EQ(tab.size(), 1) << "the 50 stack went, and 10 came off the other";
  EXPECT_EQ(tab.Count("Green Snail Shell"), 90);

  EXPECT_TRUE(tab.Spend("Green Snail Shell", 90));
  EXPECT_TRUE(tab.empty());
}

TEST(StackTabTest, TakeClampsAndDropsAnEmptiedStack) {
  StackTab tab;
  tab.Add(Proto("Green Snail Shell", 100), 30);
  EXPECT_EQ(tab.Take(-1, 5), 0);
  EXPECT_EQ(tab.Take(1, 5), 0);
  EXPECT_EQ(tab.Take(0, 10), 10);
  EXPECT_EQ(tab[0].count(), 20);
  EXPECT_EQ(tab.Take(0, 99), 20) << "clamped to what is in the stack";
  EXPECT_TRUE(tab.empty());
}

TEST(StackTabTest, SaveDropsAStackWhoseItemIsGone) {
  ItemPrototype shell = Proto("Green Snail Shell", 100);
  StackTab tab;
  tab.Add(shell, 30);

  StackableStack saved;
  google::protobuf::RepeatedPtrField<StackableStack> field;
  tab.AppendTo(&field);
  ASSERT_EQ(field.size(), 1);
  EXPECT_EQ(field[0].name(), "Green Snail Shell");
  EXPECT_EQ(field[0].count(), 30);

  saved.set_name("Item That Left The Data");
  saved.set_count(5);
  *field.Add() = saved;

  std::map<std::string, const ItemPrototype*> by_name = {
      {shell.name(), &shell}};
  StackTab restored;
  restored.RestoreFrom(field, by_name);
  EXPECT_EQ(restored.size(), 1);
  EXPECT_EQ(restored.Count("Green Snail Shell"), 30);
}

}  // namespace
}  // namespace ms
