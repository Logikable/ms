#include "src/character.h"

#include <gtest/gtest.h>

#include <memory>
#include <random>

#include "src/equip_instance.h"
#include "src/exp_table.h"
#include "src/item.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/scroll.pb.h"
#include "src/protos/skill.pb.h"

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

class AddExpTest : public CharacterTest {};

// Fixture for AdvanceJob tests. Each test needs a different starting level /
// job_stage, so c_ is created locally per test using rng_.
class AdvanceJobTest : public CharacterTest {};

// Fixture for LearnSkill tests. Each test seeds its own stage SP, so no shared
// character.
class LearnSkillTest : public CharacterTest {};

// A character carrying `sp` skill points in `stage` and nothing else.
CharacterInstance MakeCharacterWithSp(std::mt19937& rng, int stage, int sp) {
  Character proto;
  (*proto.mutable_sp_by_stage())[stage] = sp;
  return CharacterInstance(rng, std::move(proto));
}

// A minimal skill: only the fields LearnSkill reads.
Skill MakeSkill(const std::string& name, int stage, int max_level) {
  Skill skill;
  skill.set_name(name);
  skill.set_stage(stage);
  skill.set_max_level(max_level);
  return skill;
}

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

class CanEquipTest : public CharacterEquipFixture {};
class MeetsLevelTest : public CharacterEquipFixture {};
class MeetsJobTest : public CharacterEquipFixture {};
class PickUpTest : public CharacterEquipFixture {};
class EquipTest : public CharacterEquipFixture {};
class UnequipTest : public CharacterEquipFixture {};
class ScrollEquippedTest : public CharacterEquipFixture {};
class ScrollInventoryTest : public CharacterEquipFixture {};

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

TEST_F(LevelUpTest, GrantsHpAndMpAtTheDefaultRate) {
  c_.LevelUp();
  c_.LevelUp();
  // A Beginner is neither warrior nor mage, so it takes the middle rate.
  EXPECT_EQ(c_.proto().allocated_stats().hp(), 2 * 36);
  EXPECT_EQ(c_.proto().allocated_stats().mp(), 2 * 24);
}

TEST_F(LevelUpTest, WarriorsGainMoreHpAndLessMp) {
  Character proto;
  proto.set_level(15);
  proto.set_job(JOB_WARRIOR);
  CharacterInstance c(rng_, std::move(proto));
  c.LevelUp();
  EXPECT_EQ(c.proto().allocated_stats().hp(), 48);
  EXPECT_EQ(c.proto().allocated_stats().mp(), 12);
}

TEST_F(LevelUpTest, AdvancingDoesNotBackdateEarlierLevels) {
  // The rate is the one held at the time, so the two Beginner levels below
  // keep the Beginner grant even after the character becomes a Warrior.
  c_.LevelUp();
  c_.LevelUp();
  c_.AdvanceJob(JOB_WARRIOR);
  c_.LevelUp();
  EXPECT_EQ(c_.proto().allocated_stats().hp(), 2 * 36 + 48);
  EXPECT_EQ(c_.proto().allocated_stats().mp(), 2 * 24 + 12);
}

TEST_F(LevelUpTest, GrantsNoSpBelowTheFirstJobBand) {
  c_.LevelUp();  // level 1 -> 2, below the level-11 start of 1st-job SP
  EXPECT_EQ(c_.sp(1), 0);
}

TEST_F(LevelUpTest, GrantsFirstJobSpAcrossTheEarlyBand) {
  CharacterInstance c = MakeCharacter(rng_, /*level=*/11);
  c.LevelUp();  // lands on level 12, in the 1st-job band
  EXPECT_EQ(c.sp(1), 3);
}

TEST_F(LevelUpTest, FirstJobSpTotalsSixtyOneAndStopsAtTheBandEnd) {
  CharacterInstance c = MakeCharacter(rng_, /*level=*/10);
  c.AdvanceJob(JOB_WARRIOR);  // +1 into stage 1 at the advancement
  EXPECT_EQ(c.sp(1), 1);
  for (int i = 0; i < 20; ++i) {
    c.LevelUp();  // levels 11..30, each +3 into stage 1
  }
  EXPECT_EQ(c.proto().level(), 30);
  EXPECT_EQ(c.sp(1), 61);  // 1 + 20 * 3
  c.LevelUp();             // level 31 crosses into the 2nd-job band
  EXPECT_EQ(c.sp(1), 61);  // no more 1st-job SP
  EXPECT_EQ(c.sp(2), 3);   // 2nd-job SP begins
}

// --- AddExp ---

TEST_F(AddExpTest, AccumulatesExpBelowThreshold) {
  CharacterInstance c = MakeCharacter(rng_, /*level=*/1);
  c.AddExp(10);  // level 1 threshold is 15
  EXPECT_EQ(c.proto().level(), 1);
  EXPECT_EQ(c.proto().exp(), 10);
}

TEST_F(AddExpTest, LevelsUpExactlyAtThreshold) {
  CharacterInstance c = MakeCharacter(rng_, /*level=*/1);
  c.AddExp(15);
  EXPECT_EQ(c.proto().level(), 2);
  EXPECT_EQ(c.proto().exp(), 0);
}

TEST_F(AddExpTest, CarriesOverExcessExp) {
  CharacterInstance c = MakeCharacter(rng_, /*level=*/1);
  c.AddExp(20);  // 20 - 15 = 5 remaining
  EXPECT_EQ(c.proto().level(), 2);
  EXPECT_EQ(c.proto().exp(), 5);
}

TEST_F(AddExpTest, LevelsUpMultipleTimes) {
  CharacterInstance c = MakeCharacter(rng_, /*level=*/1);
  // Level 1→2 costs 15, level 2→3 costs 34; total 49.
  c.AddExp(49);
  EXPECT_EQ(c.proto().level(), 3);
  EXPECT_EQ(c.proto().exp(), 0);
}

TEST_F(AddExpTest, GrantsFiveApPerLevelUp) {
  CharacterInstance c = MakeCharacter(rng_, /*level=*/1);
  c.AddExp(49);  // two level-ups
  EXPECT_EQ(c.proto().ap(), 10);
}

TEST_F(AddExpTest, NoOpAtMaxLevel) {
  CharacterInstance c = MakeCharacter(rng_, /*level=*/kMaxLevel);
  c.AddExp(1000000);
  EXPECT_EQ(c.proto().level(), kMaxLevel);
  EXPECT_EQ(c.proto().exp(), 0);
}

TEST_F(AddExpTest, CapsAtMaxLevelAndZeroesExp) {
  CharacterInstance c = MakeCharacter(rng_, /*level=*/299);
  // Add far more than the 299→300 threshold.
  c.AddExp(1737759854037637LL * 2);
  EXPECT_EQ(c.proto().level(), kMaxLevel);
  EXPECT_EQ(c.proto().exp(), 0);
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

TEST_F(AdvanceJobTest, GrantsFirstJobStartingSp) {
  CharacterInstance c = MakeCharacter(rng_, /*level=*/10);
  c.AdvanceJob(JOB_WARRIOR);
  EXPECT_EQ(c.sp(1), 1);
}

// --- PendingJobAdvancement ---

TEST_F(AdvanceJobTest, EligibleForWarriorAtLevelTen) {
  CharacterInstance c = MakeCharacter(rng_, /*level=*/10);
  EXPECT_EQ(c.PendingJobAdvancement(), JOB_WARRIOR);
}

TEST_F(AdvanceJobTest, NotEligibleBelowLevelTen) {
  CharacterInstance c = MakeCharacter(rng_, /*level=*/9);
  EXPECT_EQ(c.PendingJobAdvancement(), JOB_UNSPECIFIED);
}

TEST_F(AdvanceJobTest, NothingPendingOnceAdvanced) {
  CharacterInstance c = MakeCharacter(rng_, /*level=*/10);
  c.AdvanceJob(JOB_WARRIOR);
  EXPECT_EQ(c.PendingJobAdvancement(), JOB_UNSPECIFIED);
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

// --- LearnSkill ---

TEST_F(LearnSkillTest, SpendsSpAndRaisesLevel) {
  CharacterInstance c = MakeCharacterWithSp(rng_, /*stage=*/1, /*sp=*/5);
  Skill skill = MakeSkill("Slash Blast", /*stage=*/1, /*max_level=*/20);
  EXPECT_TRUE(c.LearnSkill(skill));
  EXPECT_EQ(c.skill_level(skill), 1);
  EXPECT_EQ(c.sp(1), 4);
}

TEST_F(LearnSkillTest, DefaultAmountIsOne) {
  CharacterInstance c = MakeCharacterWithSp(rng_, /*stage=*/1, /*sp=*/5);
  Skill skill = MakeSkill("Slash Blast", /*stage=*/1, /*max_level=*/20);
  c.LearnSkill(skill);
  c.LearnSkill(skill);
  EXPECT_EQ(c.skill_level(skill), 2);
  EXPECT_EQ(c.sp(1), 3);
}

TEST_F(LearnSkillTest, MultiPointSpendWorks) {
  CharacterInstance c = MakeCharacterWithSp(rng_, /*stage=*/1, /*sp=*/10);
  Skill skill = MakeSkill("Slash Blast", /*stage=*/1, /*max_level=*/20);
  EXPECT_TRUE(c.LearnSkill(skill, 7));
  EXPECT_EQ(c.skill_level(skill), 7);
  EXPECT_EQ(c.sp(1), 3);
}

TEST_F(LearnSkillTest, RejectsWhenStageLacksSp) {
  CharacterInstance c = MakeCharacterWithSp(rng_, /*stage=*/1, /*sp=*/2);
  Skill skill = MakeSkill("Slash Blast", /*stage=*/1, /*max_level=*/20);
  EXPECT_FALSE(c.LearnSkill(skill, 3));
  EXPECT_EQ(c.skill_level(skill), 0);
  EXPECT_EQ(c.sp(1), 2);
}

TEST_F(LearnSkillTest, RejectsRaisingPastMaxLevel) {
  CharacterInstance c = MakeCharacterWithSp(rng_, /*stage=*/1, /*sp=*/10);
  Skill skill = MakeSkill("Slash Blast", /*stage=*/1, /*max_level=*/3);
  EXPECT_TRUE(c.LearnSkill(skill, 3));   // to the cap
  EXPECT_FALSE(c.LearnSkill(skill, 1));  // one past
  EXPECT_EQ(c.skill_level(skill), 3);
  EXPECT_EQ(c.sp(1), 7);
}

TEST_F(LearnSkillTest, RejectsNonPositiveAmount) {
  CharacterInstance c = MakeCharacterWithSp(rng_, /*stage=*/1, /*sp=*/5);
  Skill skill = MakeSkill("Slash Blast", /*stage=*/1, /*max_level=*/20);
  EXPECT_FALSE(c.LearnSkill(skill, 0));
  EXPECT_FALSE(c.LearnSkill(skill, -2));
  EXPECT_EQ(c.skill_level(skill), 0);
  EXPECT_EQ(c.sp(1), 5);
}

TEST_F(LearnSkillTest, SpendsFromTheSkillsOwnStage) {
  // SP lives in stage 2; a stage-1 skill can't touch it.
  CharacterInstance c = MakeCharacterWithSp(rng_, /*stage=*/2, /*sp=*/5);
  Skill stage1 = MakeSkill("Slash Blast", /*stage=*/1, /*max_level=*/20);
  EXPECT_FALSE(c.LearnSkill(stage1));
  Skill stage2 = MakeSkill("Brandish", /*stage=*/2, /*max_level=*/20);
  EXPECT_TRUE(c.LearnSkill(stage2));
  EXPECT_EQ(c.sp(1), 0);
  EXPECT_EQ(c.sp(2), 4);
}

TEST_F(LearnSkillTest, UnlearnedSkillIsLevelZero) {
  CharacterInstance c = MakeCharacterWithSp(rng_, /*stage=*/1, /*sp=*/5);
  Skill skill = MakeSkill("Slash Blast", /*stage=*/1, /*max_level=*/20);
  EXPECT_EQ(c.skill_level(skill), 0);
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

// --- MeetsLevel ---

TEST_F(MeetsLevelTest, TrueWhenNoRequiredLevel) {
  EXPECT_TRUE(c_.MeetsLevel(sword_));
}

TEST_F(MeetsLevelTest, TrueWhenLevelExactlyMet) {
  sword_.set_required_level(1);
  EXPECT_TRUE(c_.MeetsLevel(sword_));
}

TEST_F(MeetsLevelTest, TrueWhenLevelExceeded) {
  CharacterInstance c = MakeCharacter(rng_, /*level=*/10);
  sword_.set_required_level(5);
  EXPECT_TRUE(c.MeetsLevel(sword_));
}

TEST_F(MeetsLevelTest, FalseWhenLevelTooLow) {
  sword_.set_required_level(10);
  EXPECT_FALSE(c_.MeetsLevel(sword_));
}

// --- MeetsJob ---

TEST_F(MeetsJobTest, TrueWhenNoJobCategories) {
  // Empty categories are treated as universal (unlike CanEquip).
  c_.AdvanceJob(JOB_WARRIOR);
  EXPECT_TRUE(c_.MeetsJob(sword_));
}

TEST_F(MeetsJobTest, TrueForUniversalCategory) {
  sword_.add_equip_job_categories(EQUIP_JOB_CATEGORY_UNIVERSAL);
  c_.AdvanceJob(JOB_WARRIOR);
  EXPECT_TRUE(c_.MeetsJob(sword_));
}

TEST_F(MeetsJobTest, TrueWhenJobMatches) {
  sword_.add_equip_job_categories(EQUIP_JOB_CATEGORY_WARRIOR);
  c_.AdvanceJob(JOB_WARRIOR);
  EXPECT_TRUE(c_.MeetsJob(sword_));
}

TEST_F(MeetsJobTest, FalseWhenJobDoesNotMatch) {
  sword_.add_equip_job_categories(EQUIP_JOB_CATEGORY_BOWMAN);
  c_.AdvanceJob(JOB_WARRIOR);
  EXPECT_FALSE(c_.MeetsJob(sword_));
}

TEST_F(MeetsJobTest, FalseWhenJobUnspecified) {
  sword_.add_equip_job_categories(EQUIP_JOB_CATEGORY_WARRIOR);
  EXPECT_FALSE(c_.MeetsJob(sword_));
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

// --- AddStackable ---

// Fixture for AddStackable tests. Provides c_ and two Etc-category item
// prototypes (default max_stack 200) to exercise stacking and splitting.
class AddStackableTest : public CharacterTest {
 protected:
  void SetUp() override {
    shell_.set_name("Green Snail Shell");
    shell_.set_category(ITEM_CATEGORY_ETC);
    other_.set_name("Blue Snail Shell");
    other_.set_category(ITEM_CATEGORY_ETC);
  }
  CharacterInstance c_ = MakeCharacter(rng_);
  ItemPrototype shell_;
  ItemPrototype other_;
};

TEST_F(AddStackableTest, OpensNewStack) {
  c_.AddStackable(shell_, 5);
  ASSERT_EQ(c_.stackables(ITEM_CATEGORY_ETC).size(), 1);
  EXPECT_EQ(c_.stackables(ITEM_CATEGORY_ETC)[0].name(), "Green Snail Shell");
  EXPECT_EQ(c_.stackables(ITEM_CATEGORY_ETC)[0].count(), 5);
}

TEST_F(AddStackableTest, MergesIntoExistingStack) {
  c_.AddStackable(shell_, 5);
  c_.AddStackable(shell_, 3);
  ASSERT_EQ(c_.stackables(ITEM_CATEGORY_ETC).size(), 1);
  EXPECT_EQ(c_.stackables(ITEM_CATEGORY_ETC)[0].count(), 8);
}

TEST_F(AddStackableTest, SplitsOverflowAtMaxStack) {
  c_.AddStackable(shell_, 250);
  ASSERT_EQ(c_.stackables(ITEM_CATEGORY_ETC).size(), 2);
  EXPECT_EQ(c_.stackables(ITEM_CATEGORY_ETC)[0].count(), 200);
  EXPECT_EQ(c_.stackables(ITEM_CATEGORY_ETC)[1].count(), 50);
}

TEST_F(AddStackableTest, KeepsDistinctItemsSeparate) {
  c_.AddStackable(shell_, 5);
  c_.AddStackable(other_, 3);
  ASSERT_EQ(c_.stackables(ITEM_CATEGORY_ETC).size(), 2);
  EXPECT_EQ(c_.stackables(ITEM_CATEGORY_ETC)[0].name(), "Green Snail Shell");
  EXPECT_EQ(c_.stackables(ITEM_CATEGORY_ETC)[1].name(), "Blue Snail Shell");
}

TEST_F(AddStackableTest, NonPositiveCountIsNoOp) {
  c_.AddStackable(shell_, 0);
  c_.AddStackable(shell_, -4);
  EXPECT_TRUE(c_.stackables(ITEM_CATEGORY_ETC).empty());
}

// --- AddMeso ---

class AddMesoTest : public CharacterTest {
 protected:
  CharacterInstance c_ = MakeCharacter(rng_);
};

TEST_F(AddMesoTest, AccumulatesAcrossCalls) {
  c_.AddMeso(1000);
  c_.AddMeso(234);
  EXPECT_EQ(c_.meso(), 1234);
}

TEST_F(AddMesoTest, NonPositiveAmountIsNoOp) {
  c_.AddMeso(500);
  c_.AddMeso(0);
  c_.AddMeso(-100);
  EXPECT_EQ(c_.meso(), 500);
}

// --- SellStackable ---

// Fixture providing a sellable Etc item (7 meso each) and an unsellable one.
class SellStackableTest : public CharacterTest {
 protected:
  void SetUp() override {
    shell_.set_name("Green Snail Shell");
    shell_.set_category(ITEM_CATEGORY_ETC);
    shell_.set_sell_price(7);
    junk_.set_name("Worthless Junk");
    junk_.set_category(ITEM_CATEGORY_ETC);  // sell_price 0: unsellable
  }
  CharacterInstance c_ = MakeCharacter(rng_);
  ItemPrototype shell_;
  ItemPrototype junk_;
};

TEST_F(SellStackableTest, SellsCopiesAndCreditsMeso) {
  c_.AddStackable(shell_, 10);
  EXPECT_EQ(c_.SellStackable(ITEM_CATEGORY_ETC, 0, 4), 28);  // 4 * 7
  ASSERT_EQ(c_.stackables(ITEM_CATEGORY_ETC).size(), 1);
  EXPECT_EQ(c_.stackables(ITEM_CATEGORY_ETC)[0].count(), 6);
  EXPECT_EQ(c_.meso(), 28);
}

TEST_F(SellStackableTest, SellingWholeStackRemovesIt) {
  c_.AddStackable(shell_, 5);
  EXPECT_EQ(c_.SellStackable(ITEM_CATEGORY_ETC, 0, 5), 35);
  EXPECT_TRUE(c_.stackables(ITEM_CATEGORY_ETC).empty());
  EXPECT_EQ(c_.meso(), 35);
}

TEST_F(SellStackableTest, ClampsCountToStackSize) {
  c_.AddStackable(shell_, 3);
  EXPECT_EQ(c_.SellStackable(ITEM_CATEGORY_ETC, 0, 10), 21);  // only 3 exist
  EXPECT_TRUE(c_.stackables(ITEM_CATEGORY_ETC).empty());
  EXPECT_EQ(c_.meso(), 21);
}

TEST_F(SellStackableTest, NonPositiveCountIsNoOp) {
  c_.AddStackable(shell_, 5);
  EXPECT_EQ(c_.SellStackable(ITEM_CATEGORY_ETC, 0, 0), 0);
  EXPECT_EQ(c_.SellStackable(ITEM_CATEGORY_ETC, 0, -2), 0);
  EXPECT_EQ(c_.stackables(ITEM_CATEGORY_ETC)[0].count(), 5);
  EXPECT_EQ(c_.meso(), 0);
}

TEST_F(SellStackableTest, UnsellableItemIsNoOp) {
  c_.AddStackable(junk_, 5);
  EXPECT_EQ(c_.SellStackable(ITEM_CATEGORY_ETC, 0, 3), 0);
  EXPECT_EQ(c_.stackables(ITEM_CATEGORY_ETC)[0].count(), 5);
  EXPECT_EQ(c_.meso(), 0);
}

TEST_F(SellStackableTest, OutOfRangeIndexIsNoOp) {
  c_.AddStackable(shell_, 5);
  EXPECT_EQ(c_.SellStackable(ITEM_CATEGORY_ETC, 3, 1), 0);
  EXPECT_EQ(c_.SellStackable(ITEM_CATEGORY_ETC, -1, 1), 0);
  EXPECT_EQ(c_.meso(), 0);
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
