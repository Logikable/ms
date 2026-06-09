#include "src/equip_instance.h"

#include <random>

#include "gtest/gtest.h"
#include "src/protos/equip.pb.h"
#include "src/protos/scroll.pb.h"

namespace ms {
namespace {

class EquipInstanceTest : public ::testing::Test {
 protected:
  EquipPrototype MakeEquip(int upgrade_slots, int required_level = 0) {
    EquipPrototype e;
    e.set_name("Test");
    e.set_upgrade_slots(upgrade_slots);
    e.set_required_level(required_level);
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
  EXPECT_EQ(item.Scroll(MakeScroll(100, 2), rng_), kScrollSuccess);
  EXPECT_EQ(item.stats().attack(), 2);
  EXPECT_EQ(item.proto().remaining_upgrade_slots(), 0);
}

TEST_F(EquipInstanceTest, ZeroPercentScrollFails) {
  EquipPrototype proto = MakeEquip(1);
  EquipInstance item(proto);
  EXPECT_EQ(item.Scroll(MakeScroll(0, 2), rng_), kScrollFail);
  EXPECT_EQ(item.stats().attack(), 0);
  EXPECT_EQ(item.proto().remaining_upgrade_slots(), 0);  // slot still consumed
}

TEST_F(EquipInstanceTest, NoSlotsReturnsNoSlots) {
  EquipPrototype proto = MakeEquip(0);
  EquipInstance item(proto);
  EXPECT_EQ(item.Scroll(MakeScroll(100, 2), rng_), kScrollNoSlots);
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
    if (item.Scroll(MakeScroll(50, 1), rng_)) {
      ++successes;
    }
  }
  EXPECT_GT(successes, 0);
  EXPECT_LT(successes, 20);
}

// --- Star Force ---

TEST_F(EquipInstanceTest, StarForceSuccessIncrementsStars) {
  EquipPrototype proto = MakeEquip(0);
  EquipInstance item(proto);
  // At 0★, success=9500 (95%). Run until we get a success.
  StarForceOutcome outcome = kStarForceFail;
  for (int i = 0; i < 100 && outcome != kStarForceSuccess; ++i) {
    outcome = item.StarForce(rng_);
  }
  EXPECT_EQ(outcome, kStarForceSuccess);
  EXPECT_EQ(item.stars(), 1);
}

TEST_F(EquipInstanceTest, StarForceFailDoesNotChangeStars) {
  EquipPrototype proto = MakeEquip(0);
  std::mt19937 deterministic_rng(42);
  EquipInstance item(proto);
  bool saw_fail = false;
  for (int i = 0; i < 200 && !saw_fail; ++i) {
    int stars_before = item.stars();
    StarForceOutcome o = item.StarForce(deterministic_rng);
    if (o == kStarForceFail) {
      EXPECT_EQ(item.stars(), stars_before);
      saw_fail = true;
    }
  }
  EXPECT_TRUE(saw_fail);
}

TEST_F(EquipInstanceTest, StarForceAtMaxReturnsFailWithoutChange) {
  // Level 0 item has max 5★.
  EquipPrototype proto = MakeEquip(0);
  Equip state;
  state.set_stars(5);
  EquipInstance item(proto, state);
  EXPECT_EQ(item.StarForce(rng_), kStarForceFail);
  EXPECT_EQ(item.stars(), 5);
}

TEST_F(EquipInstanceTest, CanStarForceReturnsFalseAtMax) {
  EquipPrototype proto = MakeEquip(0);
  Equip state;
  state.set_stars(5);
  EquipInstance item(proto, state);
  EXPECT_FALSE(item.CanStarForce());
}

TEST_F(EquipInstanceTest, CanStarForceTrueBeforeMax) {
  EquipPrototype proto = MakeEquip(0);
  EquipInstance item(proto);
  EXPECT_TRUE(item.CanStarForce());
}

TEST_F(EquipInstanceTest, RateAtReturnsZeroOutOfRange) {
  StarForceRate r = EquipInstance::RateAt(-1);
  EXPECT_EQ(r.success, 0);
  EXPECT_EQ(r.destroy, 0);
  r = EquipInstance::RateAt(kMaxStarForce);
  EXPECT_EQ(r.success, 0);
}

TEST_F(EquipInstanceTest, StarForceDestroyOccursAtHighStars) {
  // At 19★, destroy=850 (8.5%). Use a level 138 item (max 30★) so 19★ is
  // reachable. Run enough attempts to observe destruction.
  EquipPrototype proto = MakeEquip(0, /*required_level=*/138);
  Equip state;
  state.set_stars(19);
  bool saw_destroy = false;
  for (int trial = 0; trial < 50 && !saw_destroy; ++trial) {
    EquipInstance item(proto, state);
    if (item.StarForce(rng_) == kStarForceDestroy) {
      saw_destroy = true;
    }
  }
  EXPECT_TRUE(saw_destroy);
}

// --- MaxStarsForLevel ---

TEST_F(EquipInstanceTest, MaxStarsForLevelBoundaries) {
  EXPECT_EQ(EquipInstance::MaxStarsForLevel(0), 5);
  EXPECT_EQ(EquipInstance::MaxStarsForLevel(94), 5);
  EXPECT_EQ(EquipInstance::MaxStarsForLevel(95), 8);
  EXPECT_EQ(EquipInstance::MaxStarsForLevel(107), 8);
  EXPECT_EQ(EquipInstance::MaxStarsForLevel(108), 10);
  EXPECT_EQ(EquipInstance::MaxStarsForLevel(117), 10);
  EXPECT_EQ(EquipInstance::MaxStarsForLevel(118), 15);
  EXPECT_EQ(EquipInstance::MaxStarsForLevel(127), 15);
  EXPECT_EQ(EquipInstance::MaxStarsForLevel(128), 20);
  EXPECT_EQ(EquipInstance::MaxStarsForLevel(137), 20);
  EXPECT_EQ(EquipInstance::MaxStarsForLevel(138), 30);
  EXPECT_EQ(EquipInstance::MaxStarsForLevel(200), 30);
}

}  // namespace
}  // namespace ms
