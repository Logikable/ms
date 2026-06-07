#include "src/character.h"

#include <gtest/gtest.h>

#include <random>

#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/scroll.pb.h"

namespace ms {
namespace {

CharacterInstance MakeCharacter(std::mt19937& rng, int level = 1, int ap = 0,
                                int job_stage = 0) {
  Character proto;
  proto.set_level(level);
  proto.set_ap(ap);
  proto.set_job_stage(job_stage);
  return CharacterInstance(rng, std::move(proto));
}

// Base fixture providing a deterministic RNG. All character test fixtures
// derive from this so tests never need a local std::mt19937.
class CharacterTest : public testing::Test {
 protected:
  std::mt19937 rng_{0};
};

// Fixture for LevelUp tests. Provides a default level-1 character.
class LevelUpTest : public CharacterTest {
 protected:
  CharacterInstance c_ = MakeCharacter(rng_);
};

// Fixture for AdvanceJob tests. Each test needs a different starting level /
// job_stage, so c_ is created locally per test using rng_.
class AdvanceJobTest : public CharacterTest {};

// Fixture for AllocateStat tests. Each test needs a different ap value, so
// c_ is created locally per test using rng_.
class AllocateStatTest : public CharacterTest {};

// Shared fixture for tests that operate on a character with a sword prototype.
// Provides c_ (fresh level-1 character) and sword_ (named "Sword", primary
// weapon slot, 7 upgrade slots). Tests pick up and equip as needed.
class CharacterEquipFixture : public CharacterTest {
 protected:
  void SetUp() override {
    sword_.set_name("Sword");
    sword_.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
    sword_.set_upgrade_slots(7);
  }
  CharacterInstance c_ = MakeCharacter(rng_);
  EquipPrototype sword_;
};

class PickUpTest : public CharacterEquipFixture {};
class EquipTest : public CharacterEquipFixture {};
class UnequipTest : public CharacterEquipFixture {};
class ScrollEquippedTest : public CharacterEquipFixture {};
class ScrollInventoryTest : public CharacterEquipFixture {};
class CanEquipTest : public CharacterEquipFixture {};

// --- LevelUp ---

TEST_F(LevelUpTest, GrantsFiveAp) {
  c_.LevelUp();
  EXPECT_EQ(c_.proto().ap(), 5);
  EXPECT_EQ(c_.proto().level(), 2);
}

TEST_F(LevelUpTest, AccumulatesAcrossMultipleLevels) {
  c_.LevelUp();
  c_.LevelUp();
  c_.LevelUp();
  EXPECT_EQ(c_.proto().ap(), 15);
  EXPECT_EQ(c_.proto().level(), 4);
}

// --- AdvanceJob ---

TEST_F(AdvanceJobTest, IncrementsStageAndSetsJob) {
  CharacterInstance c = MakeCharacter(rng_, /*level=*/10);
  c.AdvanceJob(JOB_WARRIOR);
  EXPECT_EQ(c.proto().job_stage(), 1);
  EXPECT_EQ(c.proto().job(), JOB_WARRIOR);
}

TEST_F(AdvanceJobTest, NoApBonusAtStagesOneAndTwo) {
  CharacterInstance c = MakeCharacter(rng_, /*level=*/10, /*ap=*/0);
  c.AdvanceJob(JOB_WARRIOR);
  EXPECT_EQ(c.proto().ap(), 0);
  c.AdvanceJob(JOB_WARRIOR);
  EXPECT_EQ(c.proto().ap(), 0);
}

TEST_F(AdvanceJobTest, ApBonusAtThirdJob) {
  CharacterInstance c =
      MakeCharacter(rng_, /*level=*/60, /*ap=*/0, /*job_stage=*/2);
  c.AdvanceJob(JOB_WARRIOR);
  EXPECT_EQ(c.proto().job_stage(), 3);
  EXPECT_EQ(c.proto().ap(), 5);
}

TEST_F(AdvanceJobTest, ApBonusAtFourthJob) {
  CharacterInstance c =
      MakeCharacter(rng_, /*level=*/100, /*ap=*/0, /*job_stage=*/3);
  c.AdvanceJob(JOB_WARRIOR);
  EXPECT_EQ(c.proto().job_stage(), 4);
  EXPECT_EQ(c.proto().ap(), 5);
}

// --- AllocateStat ---

TEST_F(AllocateStatTest, DeductsApAndAddsStat) {
  CharacterInstance c = MakeCharacter(rng_, /*level=*/1, /*ap=*/5);
  EXPECT_TRUE(c.AllocateStat(STAT_FIELD_STR));
  EXPECT_EQ(c.proto().ap(), 4);
  EXPECT_EQ(c.proto().allocated_stats().str(), 1);
}

TEST_F(AllocateStatTest, DefaultAmountIsOne) {
  CharacterInstance c = MakeCharacter(rng_, /*level=*/1, /*ap=*/3);
  c.AllocateStat(STAT_FIELD_DEX);
  EXPECT_EQ(c.proto().allocated_stats().dex(), 1);
  EXPECT_EQ(c.proto().ap(), 2);
}

TEST_F(AllocateStatTest, MultipleAmountWorks) {
  CharacterInstance c = MakeCharacter(rng_, /*level=*/1, /*ap=*/10);
  EXPECT_TRUE(c.AllocateStat(STAT_FIELD_LUK, 7));
  EXPECT_EQ(c.proto().allocated_stats().luk(), 7);
  EXPECT_EQ(c.proto().ap(), 3);
}

TEST_F(AllocateStatTest, ReturnsFalseOnInsufficientAp) {
  CharacterInstance c = MakeCharacter(rng_, /*level=*/1, /*ap=*/2);
  EXPECT_FALSE(c.AllocateStat(STAT_FIELD_STR, 3));
  EXPECT_EQ(c.proto().ap(), 2);
  EXPECT_EQ(c.proto().allocated_stats().str(), 0);
}

TEST_F(AllocateStatTest, ReturnsFalseForUnspecifiedField) {
  CharacterInstance c = MakeCharacter(rng_, /*level=*/1, /*ap=*/5);
  EXPECT_FALSE(c.AllocateStat(STAT_FIELD_UNSPECIFIED));
  EXPECT_EQ(c.proto().ap(), 5);
}

TEST_F(AllocateStatTest, AllFieldsWork) {
  CharacterInstance c = MakeCharacter(rng_, /*level=*/1, /*ap=*/10);
  EXPECT_TRUE(c.AllocateStat(STAT_FIELD_STR));
  EXPECT_TRUE(c.AllocateStat(STAT_FIELD_DEX));
  EXPECT_TRUE(c.AllocateStat(STAT_FIELD_INT));
  EXPECT_TRUE(c.AllocateStat(STAT_FIELD_LUK));
  EXPECT_TRUE(c.AllocateStat(STAT_FIELD_HP));
  EXPECT_TRUE(c.AllocateStat(STAT_FIELD_MP));
  EXPECT_EQ(c.proto().ap(), 4);
}

// --- PickUp ---

TEST_F(PickUpTest, AddsItemToInventory) {
  c_.PickUp(sword_);
  ASSERT_EQ(c_.inventory().size(), 1u);
  EXPECT_EQ(c_.inventory()[0].prototype().name(), "Sword");
  EXPECT_EQ(c_.inventory()[0].proto().remaining_upgrade_slots(), 7);
}

TEST_F(PickUpTest, MultiplePickUpsAccumulate) {
  c_.PickUp(sword_);
  c_.PickUp(sword_);
  EXPECT_EQ(c_.inventory().size(), 2u);
}

TEST_F(PickUpTest, FreshItemHasNoScrollStats) {
  c_.PickUp(sword_);
  EXPECT_EQ(c_.inventory()[0].proto().scroll_stats().attack(), 0);
}

// --- Equip ---

TEST_F(EquipTest, EquipsItemIntoEmptySlot) {
  c_.PickUp(sword_);
  EXPECT_TRUE(c_.Equip(0));
  EXPECT_EQ(c_.inventory().size(), 0u);
  ASSERT_TRUE(c_.equipped().count(EQUIP_SLOT_PRIMARY_WEAPON));
  EXPECT_EQ(c_.equipped().at(EQUIP_SLOT_PRIMARY_WEAPON).prototype().name(),
            "Sword");
}

TEST_F(EquipTest, DisplacesExistingItemToInventory) {
  EquipPrototype axe;
  axe.set_name("Axe");
  axe.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  c_.PickUp(sword_);
  c_.Equip(0);
  c_.PickUp(axe);
  EXPECT_TRUE(c_.Equip(0));
  EXPECT_EQ(c_.equipped().at(EQUIP_SLOT_PRIMARY_WEAPON).prototype().name(),
            "Axe");
  ASSERT_EQ(c_.inventory().size(), 1u);
  EXPECT_EQ(c_.inventory()[0].prototype().name(), "Sword");
}

TEST_F(EquipTest, ReturnsFalseForUnspecifiedSlotOnPrototype) {
  EquipPrototype proto;
  proto.set_name("Unknown");
  // equip_slot intentionally left unspecified
  c_.PickUp(proto);
  EXPECT_FALSE(c_.Equip(0));
}

TEST_F(EquipTest, ReturnsFalseForOutOfBoundsIndex) {
  EXPECT_FALSE(c_.Equip(0));
}

// --- Unequip ---

TEST_F(UnequipTest, MovesItemToInventory) {
  c_.PickUp(sword_);
  c_.Equip(0);
  EXPECT_TRUE(c_.Unequip(EQUIP_SLOT_PRIMARY_WEAPON));
  EXPECT_EQ(c_.equipped().count(EQUIP_SLOT_PRIMARY_WEAPON), 0u);
  ASSERT_EQ(c_.inventory().size(), 1u);
  EXPECT_EQ(c_.inventory()[0].prototype().name(), "Sword");
}

TEST_F(UnequipTest, ReturnsFalseForUnspecifiedSlot) {
  EXPECT_FALSE(c_.Unequip(EQUIP_SLOT_UNSPECIFIED));
}

TEST_F(UnequipTest, ReturnsFalseForUnoccupiedSlot) {
  EXPECT_FALSE(c_.Unequip(EQUIP_SLOT_PRIMARY_WEAPON));
}

// --- ScrollEquipped ---

TEST_F(ScrollEquippedTest, ReturnsFalseIfSlotEmpty) {
  Scroll scroll;
  scroll.set_success_rate(100);
  EXPECT_FALSE(c_.ScrollEquipped(EQUIP_SLOT_PRIMARY_WEAPON, scroll));
}

TEST_F(ScrollEquippedTest, UpdatesEquippedStateOnSuccess) {
  sword_.set_upgrade_slots(3);
  c_.PickUp(sword_);
  c_.Equip(0);

  Scroll scroll;
  scroll.set_success_rate(100);
  scroll.mutable_stats()->set_attack(5);

  EXPECT_TRUE(c_.ScrollEquipped(EQUIP_SLOT_PRIMARY_WEAPON, scroll));
  EXPECT_EQ(c_.equipped()
                .at(EQUIP_SLOT_PRIMARY_WEAPON)
                .proto()
                .scroll_stats()
                .attack(),
            5);
  EXPECT_EQ(c_.equipped()
                .at(EQUIP_SLOT_PRIMARY_WEAPON)
                .proto()
                .remaining_upgrade_slots(),
            2);
}

// --- ScrollInventory ---

TEST_F(ScrollInventoryTest, ReturnsFalseIfIndexOutOfRange) {
  Scroll scroll;
  scroll.set_success_rate(100);
  EXPECT_FALSE(c_.ScrollInventory(0, scroll));
}

TEST_F(ScrollInventoryTest, UpdatesInventoryItemOnSuccess) {
  sword_.set_upgrade_slots(3);
  c_.PickUp(sword_);

  Scroll scroll;
  scroll.set_success_rate(100);
  scroll.mutable_stats()->set_attack(5);

  EXPECT_TRUE(c_.ScrollInventory(0, scroll));
  EXPECT_EQ(c_.inventory()[0].proto().scroll_stats().attack(), 5);
  EXPECT_EQ(c_.inventory()[0].proto().remaining_upgrade_slots(), 2);
}

// --- CanEquip ---

TEST_F(CanEquipTest, ReturnsTrueWhenLevelAndJobMatch) {
  sword_.set_required_level(1);
  sword_.add_equip_job_categories(EQUIP_JOB_CATEGORY_WARRIOR);
  c_.AdvanceJob(JOB_WARRIOR);
  EXPECT_TRUE(c_.CanEquip(sword_));
}

TEST_F(CanEquipTest, ReturnsFalseWhenLevelTooLow) {
  sword_.set_required_level(10);
  sword_.add_equip_job_categories(EQUIP_JOB_CATEGORY_WARRIOR);
  c_.AdvanceJob(JOB_WARRIOR);
  EXPECT_FALSE(c_.CanEquip(sword_));
}

TEST_F(CanEquipTest, ReturnsFalseWhenWrongJob) {
  sword_.set_required_level(1);
  sword_.add_equip_job_categories(EQUIP_JOB_CATEGORY_BOWMAN);
  c_.AdvanceJob(JOB_WARRIOR);
  EXPECT_FALSE(c_.CanEquip(sword_));
}

TEST_F(CanEquipTest, ReturnsTrueForUniversalItem) {
  sword_.set_required_level(1);
  sword_.add_equip_job_categories(EQUIP_JOB_CATEGORY_UNIVERSAL);
  c_.AdvanceJob(JOB_WARRIOR);
  EXPECT_TRUE(c_.CanEquip(sword_));
}

TEST_F(CanEquipTest, ReturnsFalseWhenJobUnspecified) {
  sword_.set_required_level(1);
  sword_.add_equip_job_categories(EQUIP_JOB_CATEGORY_WARRIOR);
  EXPECT_FALSE(c_.CanEquip(sword_));
}

TEST_F(CanEquipTest, ReturnsTrueWhenExactLevelMet) {
  sword_.set_required_level(1);
  sword_.add_equip_job_categories(EQUIP_JOB_CATEGORY_WARRIOR);
  c_.AdvanceJob(JOB_WARRIOR);
  EXPECT_TRUE(c_.CanEquip(sword_));
}

}  // namespace
}  // namespace ms
