#include "src/equip_instance.h"

#include <random>

#include "gtest/gtest.h"
#include "src/protos/equip.pb.h"
#include "src/protos/scroll.pb.h"

namespace ms {
namespace {

class EquipInstanceTest : public ::testing::Test {
 protected:
  EquipPrototype MakeEquip(int upgrade_slots) {
    EquipPrototype e;
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

  std::mt19937 rng_{0};
};

TEST_F(EquipInstanceTest, HundredPercentScrollSucceeds) {
  EquipPrototype proto = MakeEquip(1);
  EquipInstance item(proto);
  EXPECT_TRUE(item.Scroll(MakeScroll(100, 2), rng_));
  EXPECT_EQ(item.stats().attack(), 2);
  EXPECT_EQ(item.proto().remaining_upgrade_slots(), 0);
}

TEST_F(EquipInstanceTest, ZeroPercentScrollFails) {
  EquipPrototype proto = MakeEquip(1);
  EquipInstance item(proto);
  EXPECT_FALSE(item.Scroll(MakeScroll(0, 2), rng_));
  EXPECT_EQ(item.stats().attack(), 0);
  EXPECT_EQ(item.proto().remaining_upgrade_slots(), 0);  // slot still consumed
}

TEST_F(EquipInstanceTest, NoSlotsReturnsFalse) {
  EquipPrototype proto = MakeEquip(0);
  EquipInstance item(proto);
  EXPECT_FALSE(item.Scroll(MakeScroll(100, 2), rng_));
  EXPECT_EQ(item.stats().attack(), 0);
}

TEST_F(EquipInstanceTest, StatsAccumulateAcrossScrolls) {
  EquipPrototype proto = MakeEquip(3);
  EquipInstance item(proto);
  item.Scroll(MakeScroll(100, 2), rng_);
  item.Scroll(MakeScroll(100, 2), rng_);
  item.Scroll(MakeScroll(100, 2), rng_);
  EXPECT_EQ(item.stats().attack(), 6);
  EXPECT_EQ(item.proto().remaining_upgrade_slots(), 0);
}

// Verify that a sub-100% scroll produces both successes and failures over
// enough trials with a fixed seed.
TEST_F(EquipInstanceTest, SeededRngProducesBothOutcomes) {
  EquipPrototype proto = MakeEquip(20);
  EquipInstance item(proto);
  int successes = 0;
  for (int i = 0; i < 20; ++i) {
    if (item.Scroll(MakeScroll(50, 1), rng_)) ++successes;
  }
  EXPECT_GT(successes, 0);
  EXPECT_LT(successes, 20);
}

}  // namespace
}  // namespace ms
