#include "src/item/equip_instance.h"

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

  // A piece of gear worn somewhere other than the hand. `str` is there so a
  // test can tell the two stat rules apart: below 16★ a star raises the stats
  // the job needs, above it the stats the item shows.
  EquipPrototype MakeArmour(EquipSlot slot, int base_def = 0,
                            int required_level = 0, int str = 0) {
    EquipPrototype e;
    e.set_name("Hat");
    e.set_equip_slot(slot);
    e.mutable_base_stats()->set_def(base_def);
    e.mutable_base_stats()->set_str(str);
    e.add_equip_job_categories(EQUIP_JOB_CATEGORY_WARRIOR);
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

// --- Golden Hammer ---

TEST_F(EquipInstanceTest, HammerOpensASlot) {
  EquipPrototype proto = MakeEquip(1);
  EquipInstance item(proto);
  ASSERT_EQ(item.Scroll(MakeScroll(100, 2), rng_), kScrollSuccess);
  EXPECT_TRUE(item.Hammer());
  EXPECT_EQ(item.equip_state().hammers(), 1);
  EXPECT_EQ(item.equip_state().remaining_upgrade_slots(), 1);
  EXPECT_EQ(TotalUpgradeSlots(proto, item.equip_state()), 2);
  // The new slot scrolls like any other, and what it lands stacks.
  EXPECT_EQ(item.Scroll(MakeScroll(100, 2), rng_), kScrollSuccess);
  EXPECT_EQ(item.stats().attack(), 4);
}

TEST_F(EquipInstanceTest, HammerStopsAtTwo) {
  EquipInstance item(MakeEquip(1));
  EXPECT_TRUE(item.Hammer());
  EXPECT_TRUE(item.Hammer());
  EXPECT_FALSE(item.CanHammer());
  EXPECT_FALSE(item.Hammer());
  EXPECT_EQ(item.equip_state().hammers(), kMaxHammers);
  EXPECT_EQ(item.equip_state().remaining_upgrade_slots(), 3);
}

TEST_F(EquipInstanceTest, HammerNeedsAShelfToWiden) {
  EquipInstance no_slots(MakeEquip(0));
  EXPECT_FALSE(no_slots.CanHammer());
  EXPECT_FALSE(no_slots.Hammer());

  EquipPrototype refuses = MakeEquip(3);
  refuses.add_unsupported_upgrades(UPGRADE_SCROLL);
  EquipInstance item(refuses);
  EXPECT_FALSE(item.CanHammer());
  EXPECT_FALSE(item.Hammer());
}

TEST_F(EquipInstanceTest, HammerAfterStarsKeepsThemAndHoldsTheNextOne) {
  EquipInstance item(MakeEquip(1));
  ASSERT_EQ(item.Scroll(MakeScroll(100, 2), rng_), kScrollSuccess);
  StarForceOutcome outcome = kStarForceFail;
  for (int i = 0; i < 100 && outcome != kStarForceSuccess; ++i) {
    outcome = item.StarForce(rng_);
  }
  ASSERT_EQ(item.stars(), 1);

  ASSERT_TRUE(item.Hammer());
  EXPECT_EQ(item.stars(), 1);  // the stars it has are not touched
  EXPECT_FALSE(item.CanStarForce());
  EXPECT_EQ(item.StarForce(rng_), kStarForceFail);
  EXPECT_EQ(item.stars(), 1);

  // Spending the hammer's slot -- landing it or not -- opens the way again.
  ASSERT_EQ(item.Scroll(MakeScroll(0, 2), rng_), kScrollFail);
  EXPECT_TRUE(item.CanStarForce());
}

TEST_F(EquipInstanceTest, CleanSlateBuysBackAHammerSlot) {
  ms::Scroll slate;
  slate.set_success_rate(100);
  slate.set_scroll_category(SCROLL_CATEGORY_CLEAN_SLATE);

  EquipInstance item(MakeEquip(1));
  ASSERT_EQ(item.Scroll(MakeScroll(0, 2), rng_), kScrollFail);
  ASSERT_TRUE(item.Hammer());
  ASSERT_EQ(item.Scroll(MakeScroll(0, 2), rng_), kScrollFail);
  ASSERT_EQ(item.equip_state().remaining_upgrade_slots(), 0);

  // Both failures come back: the cap counts the hammer's slot as the item's.
  EXPECT_EQ(item.Scroll(slate, rng_), kScrollSuccess);
  EXPECT_EQ(item.Scroll(slate, rng_), kScrollSuccess);
  EXPECT_EQ(item.equip_state().remaining_upgrade_slots(), 2);
  EXPECT_EQ(item.Scroll(slate, rng_), kScrollNoSlots);
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

// Star force wants every slot scrolled and a star still to come.
TEST_F(EquipInstanceTest, CanStarForceUntilTheStarsRunOut) {
  EXPECT_TRUE(EquipInstance(MakeEquip(0)).CanStarForce());

  Equip maxed;
  maxed.set_stars(5);
  EXPECT_FALSE(EquipInstance(MakeEquip(0), maxed).CanStarForce());

  EXPECT_FALSE(EquipInstance(MakeEquip(/*upgrade_slots=*/7)).CanStarForce());
}

TEST_F(EquipInstanceTest, StarForceFailsWithSlotsRemaining) {
  EquipPrototype proto = MakeEquip(/*upgrade_slots=*/7);
  EquipInstance item(proto);
  EXPECT_EQ(item.StarForce(rng_), kStarForceFail);
  EXPECT_EQ(item.stars(), 0);
}

// An item that takes no upgrade slots has nothing left to scroll, which the
// slot check alone reads as ready for stars. Throwing stars are exactly that
// shape, so without the declaration they star force like any other weapon.
TEST_F(EquipInstanceTest, AnItemThatRefusesStarForceCannotStarForce) {
  EquipPrototype proto = MakeEquip(/*upgrade_slots=*/0);
  proto.add_unsupported_upgrades(UPGRADE_STAR_FORCE);
  EquipInstance item(proto);
  EXPECT_FALSE(item.CanStarForce());
  EXPECT_EQ(item.StarForce(rng_), kStarForceFail);
  EXPECT_EQ(item.stars(), 0);
}

// Refusing one path says nothing about the other.
TEST_F(EquipInstanceTest, RefusingScrollsLeavesStarForceAlone) {
  EquipPrototype proto = MakeEquip(/*upgrade_slots=*/0);
  proto.add_unsupported_upgrades(UPGRADE_SCROLL);
  EquipInstance item(proto);
  EXPECT_TRUE(item.CanStarForce());
  EXPECT_EQ(item.Scroll(MakeScroll(100, 2), rng_), kScrollNoSlots);
}

// The slots are the trap: a data file could carry both, and the declaration
// has to win over them.
TEST_F(EquipInstanceTest, RefusingScrollsBeatsRemainingSlots) {
  EquipPrototype proto = MakeEquip(/*upgrade_slots=*/7);
  proto.add_unsupported_upgrades(UPGRADE_SCROLL);
  EquipInstance item(proto);
  EXPECT_EQ(item.Scroll(MakeScroll(100, 2), rng_), kScrollNoSlots);
  EXPECT_EQ(item.stats().attack(), 0);
  EXPECT_EQ(item.equip_state().remaining_upgrade_slots(), 7);
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

TEST_F(EquipInstanceTest, AWarriorWeaponGainsItsPrimaryStats) {
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

TEST_F(EquipInstanceTest, AMagicianWeaponGainsItsPrimaryStats) {
  Equip state;
  state.set_stars(1);
  EquipInstance item(MakeWeapon(0, EQUIP_JOB_CATEGORY_MAGICIAN), state);
  EquipStats gains = item.StarForceStatGains();
  EXPECT_EQ(gains.int_(), 2);
  EXPECT_EQ(gains.luk(), 2);
  EXPECT_EQ(gains.str(), 0);
  EXPECT_EQ(gains.dex(), 0);
}

// The weapon is the one item that gains both. GMS's cumulative table reads 25
// at 4★ and 50 at 6★; the second is there because the 6th star is the one the
// per-star deltas used to have wrong.
TEST_F(EquipInstanceTest, AWeaponGainsMaxHpAndMaxMp) {
  Equip state;
  state.set_stars(4);
  EquipInstance four(MakeWeapon(), state);
  EXPECT_EQ(four.StarForceStatGains().max_hp(), 25);
  EXPECT_EQ(four.StarForceStatGains().max_mp(), 25);
  state.set_stars(6);
  EquipInstance six(MakeWeapon(), state);
  EXPECT_EQ(six.StarForceStatGains().max_hp(), 50);
  EXPECT_EQ(six.StarForceStatGains().max_mp(), 50);
}

// Max HP is not the weapon's alone: it goes to every slot on GMS's Category A
// list, which the armour a character wears is most of.
TEST_F(EquipInstanceTest, CategoryAArmourGainsMaxHpButNoMp) {
  Equip state;
  state.set_stars(4);
  for (EquipSlot slot :
       {EQUIP_SLOT_HAT, EQUIP_SLOT_TOP, EQUIP_SLOT_BOTTOM, EQUIP_SLOT_CAPE,
        EQUIP_SLOT_RING, EQUIP_SLOT_RING_4, EQUIP_SLOT_PENDANT,
        EQUIP_SLOT_PENDANT_2, EQUIP_SLOT_BELT, EQUIP_SLOT_SHOULDER}) {
    EquipInstance item(MakeArmour(slot), state);
    EquipStats gains = item.StarForceStatGains();
    EXPECT_EQ(gains.max_hp(), 25) << EquipSlot_Name(slot);
    EXPECT_EQ(gains.max_mp(), 0) << EquipSlot_Name(slot);
  }
}

// And the accessories are not on that list, however many stars go into them.
// Gloves and shoes are not either, however much they look like the armour
// above: GMS's Category A leaves both out.
TEST_F(EquipInstanceTest, AnAccessoryGainsNoMaxHp) {
  Equip state;
  state.set_stars(10);
  for (EquipSlot slot : {EQUIP_SLOT_EYE_ACCESSORY, EQUIP_SLOT_EARRINGS,
                         EQUIP_SLOT_GLOVES, EQUIP_SLOT_SHOES}) {
    EquipInstance item(MakeArmour(slot, 100), state);
    EquipStats gains = item.StarForceStatGains();
    EXPECT_EQ(gains.max_hp(), 0) << EquipSlot_Name(slot);
    EXPECT_EQ(gains.max_mp(), 0) << EquipSlot_Name(slot);
    EXPECT_GT(gains.def(), 0) << "it still gains the defense every star gives";
  }
}

// Defense climbs by a twentieth of what the item already carries, the next
// star's share taken from the last star's total.
TEST_F(EquipInstanceTest, ArmourGainsDefenseThatCompounds) {
  Equip state;
  state.set_stars(1);
  EXPECT_EQ(EquipInstance(MakeArmour(EQUIP_SLOT_HAT, 100), state)
                .StarForceStatGains()
                .def(),
            6)
      << "1 + a twentieth of 100";
  state.set_stars(5);
  EXPECT_EQ(EquipInstance(MakeArmour(EQUIP_SLOT_HAT, 100), state)
                .StarForceStatGains()
                .def(),
            31)
      << "6, 12, 18, 24, then 31: the fifth star's share is of 124, not 100";
}

// The weapon is the exception: its stars go into attack instead.
TEST_F(EquipInstanceTest, AWeaponGainsNoDefense) {
  Equip state;
  state.set_stars(5);
  EquipPrototype proto = MakeWeapon(100);
  proto.mutable_base_stats()->set_def(100);
  EXPECT_EQ(EquipInstance(proto, state).StarForceStatGains().def(), 0);
}

// GMS raises a scaled stat only where the item already shows one. A sword
// carries no magic attack, so no number of stars gives it any.
TEST_F(EquipInstanceTest, AStatTheItemDoesNotShowGainsNothing) {
  Equip state;
  state.set_stars(5);
  EquipStats gains = EquipInstance(MakeWeapon(100), state).StarForceStatGains();
  EXPECT_GT(gains.attack(), 0);
  EXPECT_EQ(gains.magic_attack(), 0);
  EXPECT_EQ(EquipInstance(MakeArmour(EQUIP_SLOT_HAT, 0), state)
                .StarForceStatGains()
                .def(),
            0)
      << "and an item with no defense gains none";
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

// The two stat rules, in one item. A warrior weapon showing STR and no DEX
// takes both up to 15★, because that is what the job needs -- and from 16★
// only the STR it shows, which is where the high-star table takes over.
TEST_F(EquipInstanceTest, PastFifteenOnlyTheStatsTheItemShowsClimb) {
  Equip state;
  state.set_stars(16);
  EquipPrototype proto = MakeWeapon(100, EQUIP_JOB_CATEGORY_WARRIOR, 160);
  proto.mutable_base_stats()->set_str(10);
  EquipStats gains = EquipInstance(proto, state).StarForceStatGains();
  EXPECT_EQ(gains.str(), 53) << "40 from the first 15 stars, then 13";
  EXPECT_EQ(gains.dex(), 40) << "the job's, but never shown, so it stops at 15";
  EXPECT_EQ(gains.attack(), 54) << "45 scaled, then a flat 9";
}

// Armour used to gain nothing at all past 15★. GMS gives it the same stat as
// a weapon of its level and a flat attack of its own -- a bigger one, in fact.
TEST_F(EquipInstanceTest, ArmourGainsStatAndAttackPastFifteen) {
  Equip state;
  state.set_stars(16);
  EquipInstance item(MakeArmour(EQUIP_SLOT_HAT, 100, 150, /*str=*/10), state);
  EquipStats gains = item.StarForceStatGains();
  EXPECT_EQ(gains.str(), 51) << "40 from the first 15 stars, then 11";
  EXPECT_EQ(gains.attack(), 9) << "flat, and it shows none to begin with";
  EXPECT_EQ(gains.magic_attack(), 9);
  EXPECT_EQ(gains.def(), 119) << "defense stops climbing at 15★";
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
  EXPECT_EQ(EquipTabItem::MaxStarsForLevel(0), 5);
  EXPECT_EQ(EquipTabItem::MaxStarsForLevel(94), 5);
  EXPECT_EQ(EquipTabItem::MaxStarsForLevel(95), 8);
  EXPECT_EQ(EquipTabItem::MaxStarsForLevel(107), 8);
  EXPECT_EQ(EquipTabItem::MaxStarsForLevel(108), 10);
  EXPECT_EQ(EquipTabItem::MaxStarsForLevel(117), 10);
  EXPECT_EQ(EquipTabItem::MaxStarsForLevel(118), 15);
  EXPECT_EQ(EquipTabItem::MaxStarsForLevel(127), 15);
  EXPECT_EQ(EquipTabItem::MaxStarsForLevel(128), 20);
  EXPECT_EQ(EquipTabItem::MaxStarsForLevel(137), 20);
  EXPECT_EQ(EquipTabItem::MaxStarsForLevel(138), 30);
  EXPECT_EQ(EquipTabItem::MaxStarsForLevel(200), 30);
}

// --- RecoveryStars ---

TEST_F(EquipInstanceTest, RecoveryStarsBoundaries) {
  EXPECT_EQ(EquipInstance::RecoveryStars(15), 12);
  EXPECT_EQ(EquipInstance::RecoveryStars(19), 12);
  EXPECT_EQ(EquipInstance::RecoveryStars(20), 15);
  EXPECT_EQ(EquipInstance::RecoveryStars(21), 17);
  EXPECT_EQ(EquipInstance::RecoveryStars(22), 17);
  EXPECT_EQ(EquipInstance::RecoveryStars(23), 19);
  EXPECT_EQ(EquipInstance::RecoveryStars(25), 19);
  EXPECT_EQ(EquipInstance::RecoveryStars(26), 20);
  EXPECT_EQ(EquipInstance::RecoveryStars(30), 20);
}

TEST_F(EquipInstanceTest, RecoveryStarsBelowMinReturnsZero) {
  EXPECT_EQ(EquipInstance::RecoveryStars(14), 0);
  EXPECT_EQ(EquipInstance::RecoveryStars(0), 0);
}

// What a glove is paid instead of Max HP: a flat point of attack on seven of
// the first fifteen stars, and only on the attack it already shows.
TEST_F(EquipInstanceTest, AGloveGainsAttackFromItsStars) {
  EquipPrototype glove = MakeArmour(EQUIP_SLOT_GLOVES);
  glove.mutable_base_stats()->set_attack(3);
  Equip state;
  state.set_stars(5);
  EXPECT_EQ(EquipInstance(glove, state).StarForceStatGains().attack(), 1);
  state.set_stars(15);
  EquipInstance full(glove, state);
  EXPECT_EQ(full.StarForceStatGains().attack(), 7);
  EXPECT_EQ(full.StarForceStatGains().magic_attack(), 0)
      << "the glove shows no magic attack to climb";
  EXPECT_EQ(full.StarForceStatGains().max_hp(), 0);

  // The shoes beside them are paid neither, so a star gives them nothing an
  // accessory would not get.
  EquipPrototype shoes = MakeArmour(EQUIP_SLOT_SHOES);
  shoes.mutable_base_stats()->set_attack(3);
  EXPECT_EQ(EquipInstance(shoes, state).StarForceStatGains().attack(), 0);
}

TEST_F(EquipInstanceTest, EverySlotNamesTheScrollsItTakes) {
  EXPECT_EQ(TargetForSlot(EQUIP_SLOT_PRIMARY_WEAPON), SCROLL_TARGET_WEAPON);
  EXPECT_EQ(TargetForSlot(EQUIP_SLOT_HAT), SCROLL_TARGET_ARMOUR);
  EXPECT_EQ(TargetForSlot(EQUIP_SLOT_FACE_ACCESSORY), SCROLL_TARGET_ACCESSORY);
  EXPECT_EQ(TargetForSlot(EQUIP_SLOT_EYE_ACCESSORY), SCROLL_TARGET_ACCESSORY);
  EXPECT_EQ(TargetForSlot(EQUIP_SLOT_SECONDARY), SCROLL_TARGET_UNSPECIFIED);
  EXPECT_EQ(TargetForSlot(EQUIP_SLOT_PROJECTILE), SCROLL_TARGET_UNSPECIFIED);
  // The slots nothing is worn in yet. The shoulderpad takes armour scrolls
  // rather than the accessory ones it is worn beside, as it does in GMS.
  EXPECT_EQ(TargetForSlot(EQUIP_SLOT_SHOULDER), SCROLL_TARGET_ARMOUR);
  EXPECT_EQ(TargetForSlot(EQUIP_SLOT_RING), SCROLL_TARGET_ACCESSORY);
  EXPECT_EQ(TargetForSlot(EQUIP_SLOT_RING_4), SCROLL_TARGET_ACCESSORY);
  EXPECT_EQ(TargetForSlot(EQUIP_SLOT_PENDANT), SCROLL_TARGET_ACCESSORY);
  EXPECT_EQ(TargetForSlot(EQUIP_SLOT_PENDANT_2), SCROLL_TARGET_ACCESSORY);
  EXPECT_EQ(TargetForSlot(EQUIP_SLOT_BELT), SCROLL_TARGET_ACCESSORY);
  EXPECT_EQ(TargetForSlot(EQUIP_SLOT_EARRINGS), SCROLL_TARGET_ACCESSORY);
  EXPECT_EQ(TargetForSlot(EQUIP_SLOT_POCKET), SCROLL_TARGET_UNSPECIFIED);
  // Shoes scroll as armour, gloves and hearts on shelves of their own, and
  // the three trophies take no scroll at all.
  EXPECT_EQ(TargetForSlot(EQUIP_SLOT_SHOES), SCROLL_TARGET_ARMOUR);
  EXPECT_EQ(TargetForSlot(EQUIP_SLOT_GLOVES), SCROLL_TARGET_GLOVES);
  EXPECT_EQ(TargetForSlot(EQUIP_SLOT_HEART), SCROLL_TARGET_HEART);
  EXPECT_EQ(TargetForSlot(EQUIP_SLOT_BADGE), SCROLL_TARGET_UNSPECIFIED);
  EXPECT_EQ(TargetForSlot(EQUIP_SLOT_EMBLEM), SCROLL_TARGET_UNSPECIFIED);
  EXPECT_EQ(TargetForSlot(EQUIP_SLOT_MEDAL), SCROLL_TARGET_UNSPECIFIED);
}

}  // namespace
}  // namespace ms
