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

  EquipPrototype MakeWeapon(int base_att = 0,
                            EquipJobCategory cat = EQUIP_JOB_CATEGORY_WARRIOR,
                            int required_level = 0) {
    EquipPrototype e;
    e.set_name("Sword");
    e.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
    e.mutable_base_stats()->set_attack(base_att);
    e.add_equip_job_categories(cat);
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
  EXPECT_EQ(item.equip_state().remaining_upgrade_slots(), 0);
}

TEST_F(EquipInstanceTest, ZeroPercentScrollFails) {
  EquipPrototype proto = MakeEquip(1);
  EquipInstance item(proto);
  EXPECT_EQ(item.Scroll(MakeScroll(0, 2), rng_), kScrollFail);
  EXPECT_EQ(item.stats().attack(), 0);
  EXPECT_EQ(item.equip_state().remaining_upgrade_slots(),
            0);  // slot still consumed
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
  EXPECT_EQ(item.equip_state().remaining_upgrade_slots(), 0);
}

// Verify that a sub-100% scroll produces both successes and failures over
// enough trials with a fixed seed.
TEST_F(EquipInstanceTest, SeededRngProducesBothOutcomes) {
  EquipPrototype proto = MakeEquip(20);
  EquipInstance item(proto);
  int successes = 0;
  for (int i = 0; i < 20; ++i) {
    if (item.Scroll(MakeScroll(50, 1), rng_) == kScrollSuccess) {
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

TEST_F(EquipInstanceTest, CanStarForceReturnsFalseWithSlotsRemaining) {
  EquipPrototype proto = MakeEquip(/*upgrade_slots=*/7);
  EquipInstance item(proto);
  EXPECT_FALSE(item.CanStarForce());
}

TEST_F(EquipInstanceTest, StarForceFailsWithSlotsRemaining) {
  EquipPrototype proto = MakeEquip(/*upgrade_slots=*/7);
  EquipInstance item(proto);
  EXPECT_EQ(item.StarForce(rng_), kStarForceFail);
  EXPECT_EQ(item.stars(), 0);
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

// --- StarForceStatGains ---

TEST_F(EquipInstanceTest, StarForceStatGainsZeroStarsAllZero) {
  EquipInstance item(MakeWeapon(100));
  EquipStats gains = item.StarForceStatGains();
  EXPECT_EQ(gains.str(), 0);
  EXPECT_EQ(gains.attack(), 0);
  EXPECT_EQ(gains.max_hp(), 0);
}

TEST_F(EquipInstanceTest, StarForceStatGainsWarriorWeaponPrimaryStats) {
  // At 2★: kPrimaryStatDeltas[0]+[1] = 2+2 = 4 each for STR and DEX.
  Equip state;
  state.set_stars(2);
  EquipInstance item(MakeWeapon(), state);
  EquipStats gains = item.StarForceStatGains();
  EXPECT_EQ(gains.str(), 4);
  EXPECT_EQ(gains.dex(), 4);
  EXPECT_EQ(gains.int_(), 0);
  EXPECT_EQ(gains.luk(), 0);
}

TEST_F(EquipInstanceTest, StarForceStatGainsMagicianWeaponPrimaryStats) {
  Equip state;
  state.set_stars(1);
  EquipInstance item(MakeWeapon(0, EQUIP_JOB_CATEGORY_MAGICIAN), state);
  EquipStats gains = item.StarForceStatGains();
  EXPECT_EQ(gains.int_(), 2);
  EXPECT_EQ(gains.luk(), 2);
  EXPECT_EQ(gains.str(), 0);
  EXPECT_EQ(gains.dex(), 0);
}

TEST_F(EquipInstanceTest, StarForceStatGainsWeaponHpMp) {
  // kHpMpDeltas: {5,5,5,10,...}. At 4★: 5+5+5+10 = 25.
  Equip state;
  state.set_stars(4);
  EquipInstance item(MakeWeapon(), state);
  EquipStats gains = item.StarForceStatGains();
  EXPECT_EQ(gains.max_hp(), 25);
  EXPECT_EQ(gains.max_mp(), 25);
}

TEST_F(EquipInstanceTest, StarForceStatGainsWeaponAtkFormula) {
  // base_att=100: gain = floor(100/50)+1 = 3 at 1★.
  Equip state;
  state.set_stars(1);
  EquipInstance item(MakeWeapon(100), state);
  EXPECT_EQ(item.StarForceStatGains().attack(), 3);
}

TEST_F(EquipInstanceTest, StarForceStatGainsWeaponAtkAccumulates) {
  // base_att=100.
  // 0→1★: floor(100/50)+1=3. sf_att=3.
  // 1→2★: floor(103/50)+1=3. sf_att=6.
  // 2→3★: floor(106/50)+1=3. sf_att=9.
  Equip state;
  state.set_stars(3);
  EquipInstance item(MakeWeapon(100), state);
  EXPECT_EQ(item.StarForceStatGains().attack(), 9);
}

TEST_F(EquipInstanceTest, StarForceStatGainsMultiJobUnion) {
  // Warrior+Thief weapon: STR+DEX (warrior) ∪ DEX+LUK (thief) = STR+DEX+LUK.
  EquipPrototype proto = MakeWeapon();
  proto.add_equip_job_categories(EQUIP_JOB_CATEGORY_THIEF);
  Equip state;
  state.set_stars(1);
  EquipInstance item(proto, state);
  EquipStats gains = item.StarForceStatGains();
  EXPECT_EQ(gains.str(), 2);
  EXPECT_EQ(gains.dex(), 2);
  EXPECT_EQ(gains.luk(), 2);
  EXPECT_EQ(gains.int_(), 0);
}

TEST_F(EquipInstanceTest, StarForceStatGainsHighStar) {
  // Level 160 weapon at 16★. base_att=0.
  // Low-star ATK: each gain = floor((0+sf_att)/50)+1 = 1 per star → sf_att=15.
  // Low-star primary: sum(kPrimaryStatDeltas[0..14]) = 5*2+10*3 = 40 each.
  // High-star entry at 16★: kHighStar160_199[0] = {13, 9}.
  // Total: STR=DEX=53, ATK=24.
  Equip state;
  state.set_stars(16);
  EquipInstance item(MakeWeapon(0, EQUIP_JOB_CATEGORY_WARRIOR, 160), state);
  EquipStats gains = item.StarForceStatGains();
  EXPECT_EQ(gains.str(), 53);
  EXPECT_EQ(gains.dex(), 53);
  EXPECT_EQ(gains.attack(), 24);
}

TEST_F(EquipInstanceTest, StatsIncludesStarForceGains) {
  // stats() = base(100 ATK) + scroll(0) + star_force(3 ATK at 1★).
  Equip state;
  state.set_stars(1);
  EquipInstance item(MakeWeapon(100), state);
  EXPECT_EQ(item.stats().attack(), 103);
}

TEST_F(EquipInstanceTest, DestinyAxeFinalStats) {
  EquipPrototype proto;
  proto.set_name("Destiny Axe");
  proto.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  proto.set_required_level(250);
  proto.mutable_base_stats()->set_attack(358);
  proto.mutable_base_stats()->set_str(190);
  proto.mutable_base_stats()->set_dex(190);
  proto.add_equip_job_categories(EQUIP_JOB_CATEGORY_WARRIOR);

  Equip state;
  state.set_stars(22);
  state.mutable_scroll_stats()->set_attack(72);
  state.mutable_scroll_stats()->set_str(32);

  EquipInstance item(proto, state);
  EquipStats final_stats = item.stats();

  EXPECT_EQ(final_stats.str(), 381);
  EXPECT_EQ(final_stats.dex(), 349);
  EXPECT_EQ(final_stats.max_hp(), 255);
  EXPECT_EQ(final_stats.max_mp(), 255);
  EXPECT_EQ(final_stats.attack(), 710);
}

TEST_F(EquipInstanceTest, GenesisGuardsFinalStats) {
  EquipPrototype proto;
  proto.set_name("Genesis Guards");
  proto.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  proto.set_required_level(200);
  proto.mutable_base_stats()->set_attack(172);
  proto.mutable_base_stats()->set_dex(150);
  proto.mutable_base_stats()->set_luk(150);
  proto.add_equip_job_categories(EQUIP_JOB_CATEGORY_THIEF);

  Equip state;
  state.set_stars(22);

  EquipInstance item(proto, state);
  EquipStats final_stats = item.stats();

  EXPECT_EQ(final_stats.dex(), 295);
  EXPECT_EQ(final_stats.luk(), 295);
  EXPECT_EQ(final_stats.max_hp(), 255);
  EXPECT_EQ(final_stats.max_mp(), 255);
  EXPECT_EQ(final_stats.attack(), 342);
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
