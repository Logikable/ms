#include "src/character.h"

#include <gtest/gtest.h>

#include <memory>
#include <random>

#include "src/equip_instance.h"
#include "src/item.h"
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

class AllocateAllStatTest : public CharacterTest {};
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

// --- AllocateAllStat ---

TEST_F(AllocateAllStatTest, AllocatesAllAvailableAp) {
  CharacterInstance c = MakeCharacter(rng_, /*level=*/1, /*ap=*/10);
  EXPECT_TRUE(c.AllocateAllStat(STAT_FIELD_STR));
  EXPECT_EQ(c.proto().allocated_stats().str(), 10);
  EXPECT_EQ(c.proto().ap(), 0);
}

TEST_F(AllocateAllStatTest, ReturnsFalseWhenNoAp) {
  CharacterInstance c = MakeCharacter(rng_, /*level=*/1, /*ap=*/0);
  EXPECT_FALSE(c.AllocateAllStat(STAT_FIELD_STR));
  EXPECT_EQ(c.proto().allocated_stats().str(), 0);
}

TEST_F(AllocateAllStatTest, ReturnsFalseForUnspecifiedField) {
  CharacterInstance c = MakeCharacter(rng_, /*level=*/1, /*ap=*/5);
  EXPECT_FALSE(c.AllocateAllStat(STAT_FIELD_UNSPECIFIED));
  EXPECT_EQ(c.proto().ap(), 5);
}

// --- PickUp ---

TEST_F(PickUpTest, AddsItemToInventory) {
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  ASSERT_EQ(c_.inventory().size(), 1);
  const EquipInstance* item = c_.inventory().equip_instance(0);
  ASSERT_NE(item, nullptr);
  EXPECT_EQ(item->prototype().name(), "Sword");
  EXPECT_EQ(item->equip_state().remaining_upgrade_slots(), 7);
}

TEST_F(PickUpTest, MultiplePickUpsAccumulate) {
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  EXPECT_EQ(c_.inventory().size(), 2);
}

TEST_F(PickUpTest, FreshItemHasNoScrollStats) {
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  const EquipInstance* item = c_.inventory().equip_instance(0);
  ASSERT_NE(item, nullptr);
  EXPECT_EQ(item->equip_state().scroll_stats().attack(), 0);
}

// --- Equip ---

TEST_F(EquipTest, EquipsItemIntoEmptySlot) {
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  EXPECT_TRUE(c_.Equip(0));
  EXPECT_EQ(c_.inventory().size(), 0);
  ASSERT_TRUE(c_.equipped().count(EQUIP_SLOT_PRIMARY_WEAPON));
  EXPECT_EQ(c_.equipped().at(EQUIP_SLOT_PRIMARY_WEAPON).prototype().name(),
            "Sword");
}

TEST_F(EquipTest, DisplacesExistingItemToInventory) {
  EquipPrototype axe;
  axe.set_name("Axe");
  axe.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  c_.Equip(0);
  c_.PickUp(std::make_unique<EquipInstance>(axe));
  EXPECT_TRUE(c_.Equip(0));
  EXPECT_EQ(c_.equipped().at(EQUIP_SLOT_PRIMARY_WEAPON).prototype().name(),
            "Axe");
  ASSERT_EQ(c_.inventory().size(), 1);
  EXPECT_EQ(c_.inventory()[0].prototype().name(), "Sword");
}

TEST_F(EquipTest, DisplacedItemTakesVacatedPosition) {
  EquipPrototype axe;
  axe.set_name("Axe");
  axe.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  EquipPrototype bow;
  bow.set_name("Bow");
  bow.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  c_.PickUp(std::make_unique<EquipInstance>(sword_));  // index 0
  c_.PickUp(std::make_unique<EquipInstance>(axe));     // index 1
  c_.PickUp(std::make_unique<EquipInstance>(bow));     // index 2
  c_.Equip(0);  // sword equipped; inventory = [axe(0), bow(1)]
  c_.Equip(0);  // axe equipped; sword displaced back to index 0
  ASSERT_EQ(c_.inventory().size(), 2);
  EXPECT_EQ(c_.inventory()[0].prototype().name(), "Sword");
  EXPECT_EQ(c_.inventory()[1].prototype().name(), "Bow");
}

TEST_F(EquipTest, ReturnsFalseForUnspecifiedSlotOnPrototype) {
  EquipPrototype proto;
  proto.set_name("Unknown");
  // equip_slot intentionally left unspecified
  c_.PickUp(std::make_unique<EquipInstance>(proto));
  EXPECT_FALSE(c_.Equip(0));
}

TEST_F(EquipTest, ReturnsFalseForOutOfBoundsIndex) {
  EXPECT_FALSE(c_.Equip(0));
}

// --- Unequip ---

TEST_F(UnequipTest, MovesItemToInventory) {
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  c_.Equip(0);
  EXPECT_TRUE(c_.Unequip(EQUIP_SLOT_PRIMARY_WEAPON));
  EXPECT_EQ(c_.equipped().count(EQUIP_SLOT_PRIMARY_WEAPON), 0u);
  ASSERT_EQ(c_.inventory().size(), 1);
  EXPECT_EQ(c_.inventory()[0].prototype().name(), "Sword");
}

TEST_F(UnequipTest, ReturnsFalseForUnspecifiedSlot) {
  EXPECT_FALSE(c_.Unequip(EQUIP_SLOT_UNSPECIFIED));
}

TEST_F(UnequipTest, ReturnsFalseForUnoccupiedSlot) {
  EXPECT_FALSE(c_.Unequip(EQUIP_SLOT_PRIMARY_WEAPON));
}

// --- ScrollEquipped ---

TEST_F(ScrollEquippedTest, ReturnsFailIfSlotEmpty) {
  Scroll scroll;
  scroll.set_success_rate(100);
  EXPECT_EQ(c_.ScrollEquipped(EQUIP_SLOT_PRIMARY_WEAPON, scroll), kScrollFail);
}

TEST_F(ScrollEquippedTest, UpdatesEquippedStateOnSuccess) {
  sword_.set_upgrade_slots(3);
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  c_.Equip(0);

  Scroll scroll;
  scroll.set_success_rate(100);
  scroll.mutable_stats()->set_attack(5);

  EXPECT_EQ(c_.ScrollEquipped(EQUIP_SLOT_PRIMARY_WEAPON, scroll),
            kScrollSuccess);
  EXPECT_EQ(c_.equipped()
                .at(EQUIP_SLOT_PRIMARY_WEAPON)
                .equip_state()
                .scroll_stats()
                .attack(),
            5);
  EXPECT_EQ(c_.equipped()
                .at(EQUIP_SLOT_PRIMARY_WEAPON)
                .equip_state()
                .remaining_upgrade_slots(),
            2);
}

// --- ScrollInventory ---

TEST_F(ScrollInventoryTest, ReturnsFailIfIndexOutOfRange) {
  Scroll scroll;
  scroll.set_success_rate(100);
  EXPECT_EQ(c_.ScrollInventory(0, scroll), kScrollFail);
}

TEST_F(ScrollInventoryTest, UpdatesInventoryItemOnSuccess) {
  sword_.set_upgrade_slots(3);
  c_.PickUp(std::make_unique<EquipInstance>(sword_));

  Scroll scroll;
  scroll.set_success_rate(100);
  scroll.mutable_stats()->set_attack(5);

  EXPECT_EQ(c_.ScrollInventory(0, scroll), kScrollSuccess);
  const EquipInstance* item = c_.inventory().equip_instance(0);
  ASSERT_NE(item, nullptr);
  EXPECT_EQ(item->equip_state().scroll_stats().attack(), 5);
  EXPECT_EQ(item->equip_state().remaining_upgrade_slots(), 2);
}

// --- equip_stats cache ---

TEST_F(EquipTest, EquipStatsUpdatesOnEquip) {
  sword_.mutable_base_stats()->set_attack(15);
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  c_.Equip(0);
  EXPECT_EQ(c_.equip_stats().attack(), 15);
}

TEST_F(UnequipTest, EquipStatsClearsOnUnequip) {
  sword_.mutable_base_stats()->set_str(10);
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  c_.Equip(0);
  c_.Unequip(EQUIP_SLOT_PRIMARY_WEAPON);
  EXPECT_EQ(c_.equip_stats().str(), 0);
}

TEST_F(ScrollEquippedTest, EquipStatsUpdatesOnScrollSuccess) {
  sword_.set_upgrade_slots(3);
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  c_.Equip(0);
  Scroll scroll;
  scroll.set_success_rate(100);
  scroll.mutable_stats()->set_attack(7);
  c_.ScrollEquipped(EQUIP_SLOT_PRIMARY_WEAPON, scroll);
  EXPECT_EQ(c_.equip_stats().attack(), 7);
}

// --- StarForce traces ---

class StarForceTraceTest : public CharacterTest {
 protected:
  void SetUp() override {
    proto_.set_name("Sword");
    proto_.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
    proto_.set_required_level(138);
  }
  EquipPrototype proto_;
};

TEST_F(StarForceTraceTest, NoTracesInitially) {
  CharacterInstance c = MakeCharacter(rng_);
  EXPECT_TRUE(c.traces().empty());
}

TEST_F(StarForceTraceTest, DestroyedEquippedItemSavesTrace) {
  Equip state;
  state.set_stars(19);
  CharacterInstance c = MakeCharacter(rng_);
  c.PickUp(std::make_unique<EquipInstance>(proto_, state));
  c.Equip(0);
  bool saw_destroy = false;
  for (int i = 0; i < 100 && !saw_destroy; ++i) {
    if (c.StarForceEquipped(EQUIP_SLOT_PRIMARY_WEAPON) == kStarForceDestroy) {
      saw_destroy = true;
    }
  }
  ASSERT_TRUE(saw_destroy);
  ASSERT_EQ(c.traces().size(), 1u);
  EXPECT_EQ(c.traces()[0]->prototype().name(), "Sword");
  EXPECT_GE(c.traces()[0]->equip_state().stars(), 19);
}

TEST_F(StarForceTraceTest, DestroyedInventoryItemSavesTrace) {
  Equip state;
  state.set_stars(19);
  CharacterInstance c = MakeCharacter(rng_);
  c.PickUp(std::make_unique<EquipInstance>(proto_, state));
  bool saw_destroy = false;
  for (int i = 0; i < 100 && !saw_destroy; ++i) {
    if (c.StarForceInventory(0) == kStarForceDestroy) {
      saw_destroy = true;
    }
  }
  ASSERT_TRUE(saw_destroy);
  ASSERT_EQ(c.traces().size(), 1u);
  EXPECT_EQ(c.traces()[0]->prototype().name(), "Sword");
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

TEST_F(CanEquipTest, ReturnsTrueForUniversalItemAsBeginner) {
  sword_.set_required_level(1);
  sword_.add_equip_job_categories(EQUIP_JOB_CATEGORY_UNIVERSAL);
  c_.AdvanceJob(JOB_BEGINNER);
  EXPECT_TRUE(c_.CanEquip(sword_));
}

TEST_F(CanEquipTest, ReturnsTrueForBeginnerCategoryItem) {
  sword_.set_required_level(1);
  sword_.add_equip_job_categories(EQUIP_JOB_CATEGORY_BEGINNER);
  c_.AdvanceJob(JOB_BEGINNER);
  EXPECT_TRUE(c_.CanEquip(sword_));
}

TEST_F(CanEquipTest, ReturnsFalseWhenBeginnerTriesToEquipWarriorItem) {
  sword_.set_required_level(1);
  sword_.add_equip_job_categories(EQUIP_JOB_CATEGORY_WARRIOR);
  c_.AdvanceJob(JOB_BEGINNER);
  EXPECT_FALSE(c_.CanEquip(sword_));
}

TEST_F(CanEquipTest, ReturnsTrueWhenExactLevelMet) {
  sword_.set_required_level(1);
  sword_.add_equip_job_categories(EQUIP_JOB_CATEGORY_WARRIOR);
  c_.AdvanceJob(JOB_WARRIOR);
  EXPECT_TRUE(c_.CanEquip(sword_));
}

TEST_F(CanEquipTest, ReturnsFalseForEmptyJobCategories) {
  sword_.set_required_level(1);
  // No equip_job_categories set; no job can equip it.
  c_.AdvanceJob(JOB_WARRIOR);
  EXPECT_FALSE(c_.CanEquip(sword_));
}

// --- Trace items in inventory ---

TEST_F(StarForceTraceTest, EquipTraceInInventoryReturnsFalse) {
  Equip state;
  state.set_stars(19);
  CharacterInstance c = MakeCharacter(rng_);
  c.PickUp(std::make_unique<EquipInstance>(proto_, state));
  bool saw_destroy = false;
  for (int i = 0; i < 100 && !saw_destroy; ++i) {
    if (c.StarForceInventory(0) == kStarForceDestroy) {
      saw_destroy = true;
    }
  }
  ASSERT_TRUE(saw_destroy);
  ASSERT_EQ(c.inventory().size(), 1);
  // Dynamic cast to EquipInstance fails; Equip() must return false.
  EXPECT_FALSE(c.Equip(0));
}

TEST_F(StarForceTraceTest, ScrollInventoryOnTraceReturnsFail) {
  Equip state;
  state.set_stars(19);
  CharacterInstance c = MakeCharacter(rng_);
  c.PickUp(std::make_unique<EquipInstance>(proto_, state));
  bool saw_destroy = false;
  for (int i = 0; i < 100 && !saw_destroy; ++i) {
    if (c.StarForceInventory(0) == kStarForceDestroy) {
      saw_destroy = true;
    }
  }
  ASSERT_TRUE(saw_destroy);
  Scroll scroll;
  scroll.set_success_rate(100);
  EXPECT_EQ(c.ScrollInventory(0, scroll), kScrollFail);
}

TEST_F(StarForceTraceTest, StarForceInventoryOnTraceReturnsFail) {
  Equip state;
  state.set_stars(19);
  CharacterInstance c = MakeCharacter(rng_);
  c.PickUp(std::make_unique<EquipInstance>(proto_, state));
  bool saw_destroy = false;
  for (int i = 0; i < 100 && !saw_destroy; ++i) {
    if (c.StarForceInventory(0) == kStarForceDestroy) {
      saw_destroy = true;
    }
  }
  ASSERT_TRUE(saw_destroy);
  EXPECT_EQ(c.StarForceInventory(0), kStarForceFail);
}

// --- RecoverTrace ---

class RecoverTraceTest : public CharacterTest {
 protected:
  void SetUp() override {
    proto_.set_name("Sword");
    proto_.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
    proto_.set_required_level(138);
  }
  EquipPrototype proto_;
};

TEST_F(RecoverTraceTest, RecoveryYieldsCorrectStarCount) {
  CharacterInstance c = MakeCharacter(rng_);
  Equip trace_state;
  trace_state.set_stars(20);
  c.PickUp(std::make_unique<EquipTrace>(proto_, trace_state));  // index 0
  c.PickUp(std::make_unique<EquipInstance>(proto_));  // index 1: fresh base
  int stars = c.RecoverTrace(/*trace_index=*/0, /*base_item_index=*/1);
  EXPECT_EQ(stars, 15);
  ASSERT_EQ(c.inventory().size(), 1);
  const EquipInstance* recovered = c.inventory().equip_instance(0);
  ASSERT_NE(recovered, nullptr);
  EXPECT_EQ(recovered->stars(), 15);
}

TEST_F(RecoverTraceTest, RecoveryTransfersScrollStats) {
  CharacterInstance c = MakeCharacter(rng_);
  Equip trace_state;
  trace_state.set_stars(15);
  trace_state.mutable_scroll_stats()->set_attack(7);
  trace_state.set_remaining_upgrade_slots(2);
  c.PickUp(std::make_unique<EquipTrace>(proto_, trace_state));  // index 0
  c.PickUp(std::make_unique<EquipInstance>(proto_));            // index 1
  c.RecoverTrace(0, 1);
  const EquipInstance* recovered = c.inventory().equip_instance(0);
  ASSERT_NE(recovered, nullptr);
  EXPECT_EQ(recovered->equip_state().scroll_stats().attack(), 7);
  EXPECT_EQ(recovered->equip_state().remaining_upgrade_slots(), 2);
}

TEST_F(RecoverTraceTest, BothItemsRemovedFromInventory) {
  CharacterInstance c = MakeCharacter(rng_);
  Equip trace_state;
  trace_state.set_stars(15);
  c.PickUp(std::make_unique<EquipTrace>(proto_, trace_state));  // index 0
  c.PickUp(std::make_unique<EquipInstance>(proto_));            // index 1
  ASSERT_EQ(c.inventory().size(), 2);
  c.RecoverTrace(0, 1);
  EXPECT_EQ(c.inventory().size(), 1);
}

TEST_F(RecoverTraceTest, BaseBeforeTraceInInventoryStillWorks) {
  CharacterInstance c = MakeCharacter(rng_);
  Equip trace_state;
  trace_state.set_stars(21);
  c.PickUp(std::make_unique<EquipInstance>(proto_));  // index 0: base
  c.PickUp(
      std::make_unique<EquipTrace>(proto_, trace_state));  // index 1: trace
  int stars = c.RecoverTrace(/*trace_index=*/1, /*base_item_index=*/0);
  EXPECT_EQ(stars, 17);
  EXPECT_EQ(c.inventory().size(), 1);
  EXPECT_NE(c.inventory().equip_instance(0), nullptr);
}

}  // namespace
}  // namespace ms
