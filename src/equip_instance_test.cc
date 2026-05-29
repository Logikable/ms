#include "src/equip_instance.h"

#include <random>

#include "gtest/gtest.h"
#include "src/equip.pb.h"
#include "src/scroll.pb.h"

namespace ms {
namespace {

Equip MakeEquip(int upgrade_slots) {
  Equip e;
  e.set_name("Test");
  e.set_upgrade_slots(upgrade_slots);
  return e;
}

ms::Scroll MakeScroll(int success_rate, int attack) {
  ms::Scroll s;
  s.set_success_rate(success_rate);
  s.mutable_stats()->set_attack(attack);
  return s;
}

TEST(EquipInstanceTest, HundredPercentScrollSucceeds) {
  Equip proto = MakeEquip(1);
  EquipInstance item(proto);
  std::mt19937 rng(0);
  EXPECT_TRUE(item.Scroll(MakeScroll(100, 2), rng));
  EXPECT_EQ(item.stats().attack(), 2);
  EXPECT_EQ(item.remaining_upgrade_slots(), 0);
}

TEST(EquipInstanceTest, ZeroPercentScrollFails) {
  Equip proto = MakeEquip(1);
  EquipInstance item(proto);
  std::mt19937 rng(0);
  EXPECT_FALSE(item.Scroll(MakeScroll(0, 2), rng));
  EXPECT_EQ(item.stats().attack(), 0);
  EXPECT_EQ(item.remaining_upgrade_slots(), 0);  // slot still consumed
}

TEST(EquipInstanceTest, NoSlotsReturnsFalse) {
  Equip proto = MakeEquip(0);
  EquipInstance item(proto);
  std::mt19937 rng(0);
  EXPECT_FALSE(item.Scroll(MakeScroll(100, 2), rng));
  EXPECT_EQ(item.stats().attack(), 0);
}

TEST(EquipInstanceTest, StatsAccumulateAcrossScrolls) {
  Equip proto = MakeEquip(3);
  EquipInstance item(proto);
  std::mt19937 rng(0);
  item.Scroll(MakeScroll(100, 2), rng);
  item.Scroll(MakeScroll(100, 2), rng);
  item.Scroll(MakeScroll(100, 2), rng);
  EXPECT_EQ(item.stats().attack(), 6);
  EXPECT_EQ(item.remaining_upgrade_slots(), 0);
}

// Verify that a sub-100% scroll produces both successes and failures over
// enough trials with a fixed seed.
TEST(EquipInstanceTest, SeededRngProducesBothOutcomes) {
  Equip proto = MakeEquip(20);
  EquipInstance item(proto);
  std::mt19937 rng(0);
  int successes = 0;
  for (int i = 0; i < 20; ++i) {
    if (item.Scroll(MakeScroll(50, 1), rng)) ++successes;
  }
  EXPECT_GT(successes, 0);
  EXPECT_LT(successes, 20);
}

}  // namespace
}  // namespace ms
