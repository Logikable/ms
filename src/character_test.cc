#include "src/character.h"

#include <gtest/gtest.h>
#include "src/equip.pb.h"
#include "src/scroll.pb.h"

namespace ms {
namespace {

CharacterInstance MakeCharacter(int level = 1, int ap = 0, int job_stage = 0) {
  Character proto;
  proto.set_level(level);
  proto.set_ap(ap);
  proto.set_job_stage(job_stage);
  return CharacterInstance(std::move(proto));
}

TEST(LevelUpTest, GrantsFiveAp) {
  CharacterInstance c = MakeCharacter(/*level=*/1, /*ap=*/0);
  c.LevelUp();
  EXPECT_EQ(c.proto().ap(), 5);
  EXPECT_EQ(c.proto().level(), 2);
}

TEST(LevelUpTest, AccumulatesAcrossMultipleLevels) {
  CharacterInstance c = MakeCharacter();
  c.LevelUp();
  c.LevelUp();
  c.LevelUp();
  EXPECT_EQ(c.proto().ap(), 15);
  EXPECT_EQ(c.proto().level(), 4);
}

TEST(AdvanceJobTest, IncrementsStageAndSetsJob) {
  CharacterInstance c = MakeCharacter(/*level=*/10);
  c.AdvanceJob(JOB_WARRIOR);
  EXPECT_EQ(c.proto().job_stage(), 1);
  EXPECT_EQ(c.proto().job(), JOB_WARRIOR);
}

TEST(AdvanceJobTest, NoApBonusAtStagesOneAndTwo) {
  CharacterInstance c = MakeCharacter(/*level=*/10, /*ap=*/0);
  c.AdvanceJob(JOB_WARRIOR);
  EXPECT_EQ(c.proto().ap(), 0);
  c.AdvanceJob(JOB_WARRIOR);
  EXPECT_EQ(c.proto().ap(), 0);
}

TEST(AdvanceJobTest, ApBonusAtThirdJob) {
  CharacterInstance c = MakeCharacter(/*level=*/60, /*ap=*/0, /*job_stage=*/2);
  c.AdvanceJob(JOB_WARRIOR);
  EXPECT_EQ(c.proto().job_stage(), 3);
  EXPECT_EQ(c.proto().ap(), 5);
}

TEST(AdvanceJobTest, ApBonusAtFourthJob) {
  CharacterInstance c = MakeCharacter(/*level=*/100, /*ap=*/0, /*job_stage=*/3);
  c.AdvanceJob(JOB_WARRIOR);
  EXPECT_EQ(c.proto().job_stage(), 4);
  EXPECT_EQ(c.proto().ap(), 5);
}

TEST(AllocateStatTest, DeductsApAndAddsStat) {
  CharacterInstance c = MakeCharacter(/*level=*/1, /*ap=*/5);
  EXPECT_TRUE(c.AllocateStat(STAT_FIELD_STR));
  EXPECT_EQ(c.proto().ap(), 4);
  EXPECT_EQ(c.proto().allocated_stats().str(), 1);
}

TEST(AllocateStatTest, DefaultAmountIsOne) {
  CharacterInstance c = MakeCharacter(/*level=*/1, /*ap=*/3);
  c.AllocateStat(STAT_FIELD_DEX);
  EXPECT_EQ(c.proto().allocated_stats().dex(), 1);
  EXPECT_EQ(c.proto().ap(), 2);
}

TEST(AllocateStatTest, MultipleAmountWorks) {
  CharacterInstance c = MakeCharacter(/*level=*/1, /*ap=*/10);
  EXPECT_TRUE(c.AllocateStat(STAT_FIELD_LUK, 7));
  EXPECT_EQ(c.proto().allocated_stats().luk(), 7);
  EXPECT_EQ(c.proto().ap(), 3);
}

TEST(AllocateStatTest, ReturnsFalseOnInsufficientAp) {
  CharacterInstance c = MakeCharacter(/*level=*/1, /*ap=*/2);
  EXPECT_FALSE(c.AllocateStat(STAT_FIELD_STR, 3));
  EXPECT_EQ(c.proto().ap(), 2);
  EXPECT_EQ(c.proto().allocated_stats().str(), 0);
}

TEST(AllocateStatTest, ReturnsFalseForUnspecifiedField) {
  CharacterInstance c = MakeCharacter(/*level=*/1, /*ap=*/5);
  EXPECT_FALSE(c.AllocateStat(STAT_FIELD_UNSPECIFIED));
  EXPECT_EQ(c.proto().ap(), 5);
}

TEST(AllocateStatTest, AllFieldsWork) {
  CharacterInstance c = MakeCharacter(/*level=*/1, /*ap=*/10);
  EXPECT_TRUE(c.AllocateStat(STAT_FIELD_STR));
  EXPECT_TRUE(c.AllocateStat(STAT_FIELD_DEX));
  EXPECT_TRUE(c.AllocateStat(STAT_FIELD_INT));
  EXPECT_TRUE(c.AllocateStat(STAT_FIELD_LUK));
  EXPECT_TRUE(c.AllocateStat(STAT_FIELD_HP));
  EXPECT_TRUE(c.AllocateStat(STAT_FIELD_MP));
  EXPECT_EQ(c.proto().ap(), 4);
}

TEST(PickUpTest, AddsItemToInventory) {
  CharacterInstance c = MakeCharacter();
  EquipPrototype proto;
  proto.set_name("Sword");
  proto.set_upgrade_slots(7);
  c.PickUp(proto);
  ASSERT_EQ(c.proto().inventory().equip_tab_size(), 1);
  EXPECT_EQ(c.proto().inventory().equip_tab(0).equip_name(), "Sword");
  EXPECT_EQ(c.proto().inventory().equip_tab(0).remaining_upgrade_slots(), 7);
}

TEST(PickUpTest, MultiplePickUpsAccumulate) {
  CharacterInstance c = MakeCharacter();
  EquipPrototype proto;
  proto.set_name("Sword");
  proto.set_upgrade_slots(7);
  c.PickUp(proto);
  c.PickUp(proto);
  EXPECT_EQ(c.proto().inventory().equip_tab_size(), 2);
}

TEST(PickUpTest, FreshItemHasNoScrollStats) {
  CharacterInstance c = MakeCharacter();
  EquipPrototype proto;
  proto.set_name("Sword");
  c.PickUp(proto);
  EXPECT_EQ(c.proto().inventory().equip_tab(0).scroll_stats().attack(), 0);
}

TEST(EquipTest, EquipsItemIntoEmptySlot) {
  CharacterInstance c = MakeCharacter();
  EquipPrototype proto;
  proto.set_name("Sword");
  proto.set_upgrade_slots(7);
  c.PickUp(proto);
  EXPECT_TRUE(c.Equip(EQUIP_SLOT_PRIMARY_WEAPON, 0));
  EXPECT_EQ(c.proto().inventory().equip_tab_size(), 0);
  ASSERT_TRUE(c.proto().equipped().contains(EQUIP_SLOT_PRIMARY_WEAPON));
  EXPECT_EQ(c.proto().equipped().at(EQUIP_SLOT_PRIMARY_WEAPON).equip_name(),
            "Sword");
}

TEST(EquipTest, SwapsDisplacedItemToInventory) {
  CharacterInstance c = MakeCharacter();
  EquipPrototype sword;
  sword.set_name("Sword");
  EquipPrototype axe;
  axe.set_name("Axe");
  c.PickUp(sword);
  c.Equip(EQUIP_SLOT_PRIMARY_WEAPON, 0);
  c.PickUp(axe);
  EXPECT_TRUE(c.Equip(EQUIP_SLOT_PRIMARY_WEAPON, 0));
  EXPECT_EQ(c.proto().equipped().at(EQUIP_SLOT_PRIMARY_WEAPON).equip_name(),
            "Axe");
  ASSERT_EQ(c.proto().inventory().equip_tab_size(), 1);
  EXPECT_EQ(c.proto().inventory().equip_tab(0).equip_name(), "Sword");
}

TEST(EquipTest, ReturnsFalseForUnspecifiedSlot) {
  CharacterInstance c = MakeCharacter();
  EquipPrototype proto;
  proto.set_name("Sword");
  c.PickUp(proto);
  EXPECT_FALSE(c.Equip(EQUIP_SLOT_UNSPECIFIED, 0));
}

TEST(EquipTest, ReturnsFalseForOutOfBoundsIndex) {
  CharacterInstance c = MakeCharacter();
  EXPECT_FALSE(c.Equip(EQUIP_SLOT_PRIMARY_WEAPON, 0));
}

TEST(UnequipTest, MovesItemToInventory) {
  CharacterInstance c = MakeCharacter();
  EquipPrototype proto;
  proto.set_name("Sword");
  c.PickUp(proto);
  c.Equip(EQUIP_SLOT_PRIMARY_WEAPON, 0);
  EXPECT_TRUE(c.Unequip(EQUIP_SLOT_PRIMARY_WEAPON));
  EXPECT_EQ(c.proto().equipped().count(EQUIP_SLOT_PRIMARY_WEAPON), 0);
  ASSERT_EQ(c.proto().inventory().equip_tab_size(), 1);
  EXPECT_EQ(c.proto().inventory().equip_tab(0).equip_name(), "Sword");
}

TEST(UnequipTest, ReturnsFalseForUnspecifiedSlot) {
  CharacterInstance c = MakeCharacter();
  EXPECT_FALSE(c.Unequip(EQUIP_SLOT_UNSPECIFIED));
}

TEST(UnequipTest, ReturnsFalseForUnoccupiedSlot) {
  CharacterInstance c = MakeCharacter();
  EXPECT_FALSE(c.Unequip(EQUIP_SLOT_PRIMARY_WEAPON));
}

TEST(ScrollEquippedTest, ReturnsFalseIfSlotEmpty) {
  CharacterInstance c = MakeCharacter();
  EquipPrototype proto;
  proto.set_name("Sword");
  Scroll scroll;
  scroll.set_success_rate(100);
  std::mt19937 rng(0);
  EXPECT_FALSE(c.ScrollEquipped(EQUIP_SLOT_PRIMARY_WEAPON, proto, scroll, rng));
}

TEST(ScrollEquippedTest, UpdatesEquippedStateOnSuccess) {
  CharacterInstance c = MakeCharacter();
  EquipPrototype proto;
  proto.set_name("Sword");
  proto.set_upgrade_slots(3);
  c.PickUp(proto);
  c.Equip(EQUIP_SLOT_PRIMARY_WEAPON, 0);

  Scroll scroll;
  scroll.set_success_rate(100);
  scroll.mutable_stats()->set_attack(5);
  std::mt19937 rng(0);

  EXPECT_TRUE(c.ScrollEquipped(EQUIP_SLOT_PRIMARY_WEAPON, proto, scroll, rng));
  EXPECT_EQ(
      c.proto().equipped().at(EQUIP_SLOT_PRIMARY_WEAPON).scroll_stats().attack(),
      5);
  EXPECT_EQ(
      c.proto().equipped().at(EQUIP_SLOT_PRIMARY_WEAPON).remaining_upgrade_slots(),
      2);
}

}  // namespace
}  // namespace ms
