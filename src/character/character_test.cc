#include "src/character/character.h"

#include <gtest/gtest.h>

#include <memory>
#include <random>
#include <string>
#include <vector>

#include "src/character/exp_table.h"
#include "src/item/equip_instance.h"
#include "src/item/inventory.h"
#include "src/item/item.h"
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

// A minimal skill: only the fields LearnSkill reads. The advancement fixes the
// SP stage; JOB_ADVANCEMENT_SWORDMAN is a 1st-job (stage 1) advancement.
Skill MakeSkill(const std::string& name, JobAdvancement advancement,
                int max_level) {
  Skill skill;
  skill.set_name(name);
  skill.set_job_advancement(advancement);
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
  proto.set_job(JOB_SWORDMAN);
  CharacterInstance c(rng_, std::move(proto));
  c.LevelUp();
  EXPECT_EQ(c.proto().allocated_stats().hp(), 48);
  EXPECT_EQ(c.proto().allocated_stats().mp(), 12);
}

TEST_F(LevelUpTest, MagesInvertTheWarriorsGrant) {
  Character proto;
  proto.set_level(15);
  proto.set_job(JOB_MAGICIAN);
  CharacterInstance c(rng_, std::move(proto));
  c.LevelUp();
  EXPECT_EQ(c.proto().allocated_stats().hp(), 12);
  EXPECT_EQ(c.proto().allocated_stats().mp(), 48);
}

// Only the two extremes have a rate of their own; everyone else shares the
// middle one, so a rogue levels exactly as an archer does.
TEST_F(LevelUpTest, RoguesLevelAtTheMiddlingRate) {
  Character proto;
  proto.set_level(15);
  proto.set_job(JOB_ROGUE);
  CharacterInstance c(rng_, std::move(proto));
  c.LevelUp();
  EXPECT_EQ(c.proto().allocated_stats().hp(), 36);
  EXPECT_EQ(c.proto().allocated_stats().mp(), 24);
}

TEST_F(LevelUpTest, AdvancingDoesNotBackdateEarlierLevels) {
  // The rate is the one held at the time, so the two Beginner levels below
  // keep the Beginner grant even after the character becomes a Warrior.
  c_.LevelUp();
  c_.LevelUp();
  c_.AdvanceJob(JOB_SWORDMAN);
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

TEST_F(LevelUpTest, FirstJobSpTotalsSixtyAndStopsAtTheBandEnd) {
  CharacterInstance c = MakeCharacter(rng_, /*level=*/10);
  c.AdvanceJob(JOB_SWORDMAN);  // the advancement itself grants nothing
  EXPECT_EQ(c.sp(1), 0);
  for (int i = 0; i < 20; ++i) {
    c.LevelUp();  // levels 11..30, each +3 into stage 1
  }
  EXPECT_EQ(c.proto().level(), 30);
  EXPECT_EQ(c.sp(1), 60);  // 20 * 3, exactly what a 1st-job book costs
  c.LevelUp();             // level 31 crosses into the 2nd-job band
  EXPECT_EQ(c.sp(1), 60);  // no more 1st-job SP
  EXPECT_EQ(c.sp(2), 3);   // 2nd-job SP begins
}

TEST_F(LevelUpTest, EveryFirstJobReachesTheSameSixty) {
  // No job is handed a head start; the pools are identical.
  CharacterInstance c = MakeCharacter(rng_, /*level=*/10);
  c.AdvanceJob(JOB_ARCHER);
  for (int i = 0; i < 20; ++i) {
    c.LevelUp();
  }
  EXPECT_EQ(c.sp(1), 60);
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

TEST_F(AddExpTest, NoOpAtTheLevelCap) {
  CharacterInstance c = MakeCharacter(rng_, /*level=*/kTrialLevelCap);
  c.AddExp(1000000);
  EXPECT_EQ(c.proto().level(), kTrialLevelCap);
  EXPECT_EQ(c.proto().exp(), 0);
}

TEST_F(AddExpTest, StopsAtTheLevelCapAndZeroesExp) {
  CharacterInstance c = MakeCharacter(rng_, /*level=*/kTrialLevelCap - 1);
  // Far more than the last threshold below the cap, and far more than the
  // several after it: none of them are reachable.
  c.AddExp(1000000000LL);
  EXPECT_EQ(c.proto().level(), kTrialLevelCap);
  EXPECT_EQ(c.proto().exp(), 0);
}

// The cap is on what combat pays out, not on what a level is. LevelUp is how
// the debug item grants one, and it still climbs past the cap.
TEST_F(AddExpTest, LevelUpItselfIsNotCapped) {
  CharacterInstance c = MakeCharacter(rng_, /*level=*/kTrialLevelCap);
  c.LevelUp();
  EXPECT_EQ(c.proto().level(), kTrialLevelCap + 1);
}

// --- AdvanceJob ---

TEST_F(AdvanceJobTest, IncrementsStageAndSetsJob) {
  CharacterInstance c = MakeCharacter(rng_, /*level=*/10);
  c.AdvanceJob(JOB_SWORDMAN);
  EXPECT_EQ(c.proto().job_stage(), 1);
  EXPECT_EQ(c.proto().job(), JOB_SWORDMAN);
}

TEST_F(AdvanceJobTest, NoApBonusAtStagesOneAndTwo) {
  CharacterInstance c = MakeCharacter(rng_, /*level=*/10, /*ap=*/0);
  c.AdvanceJob(JOB_SWORDMAN);
  EXPECT_EQ(c.proto().ap(), 0);
  c.AdvanceJob(JOB_SWORDMAN);
  EXPECT_EQ(c.proto().ap(), 0);
}

TEST_F(AdvanceJobTest, ApBonusAtThirdJob) {
  CharacterInstance c =
      MakeCharacter(rng_, /*level=*/60, /*ap=*/0, /*job_stage=*/2);
  c.AdvanceJob(JOB_SWORDMAN);
  EXPECT_EQ(c.proto().job_stage(), 3);
  EXPECT_EQ(c.proto().ap(), 5);
}

TEST_F(AdvanceJobTest, ApBonusAtFourthJob) {
  CharacterInstance c =
      MakeCharacter(rng_, /*level=*/100, /*ap=*/0, /*job_stage=*/3);
  c.AdvanceJob(JOB_SWORDMAN);
  EXPECT_EQ(c.proto().job_stage(), 4);
  EXPECT_EQ(c.proto().ap(), 5);
}

TEST_F(AdvanceJobTest, GrantsNoStartingSp) {
  CharacterInstance c = MakeCharacter(rng_, /*level=*/10);
  c.AdvanceJob(JOB_SWORDMAN);
  EXPECT_EQ(c.sp(1), 0);
}

// --- CanAdvanceJob / JobChoicesForStage ---

TEST_F(AdvanceJobTest, EligibleAtLevelTen) {
  CharacterInstance c = MakeCharacter(rng_, /*level=*/10);
  EXPECT_TRUE(c.CanAdvanceJob());
}

TEST_F(AdvanceJobTest, NotEligibleBelowLevelTen) {
  CharacterInstance c = MakeCharacter(rng_, /*level=*/9);
  EXPECT_FALSE(c.CanAdvanceJob());
}

TEST_F(AdvanceJobTest, NothingPendingOnceAdvanced) {
  CharacterInstance c = MakeCharacter(rng_, /*level=*/10);
  c.AdvanceJob(JOB_SWORDMAN);
  EXPECT_FALSE(c.CanAdvanceJob());
}

// Level 30 opens the 2nd advancement, but no job defines one yet, so there is
// nothing to offer and the character must not be told otherwise.
TEST_F(AdvanceJobTest, NotEligibleForAnAdvancementWithNoJobsBehindIt) {
  CharacterInstance c = MakeCharacter(rng_, /*level=*/30);
  c.AdvanceJob(JOB_SWORDMAN);
  EXPECT_FALSE(c.CanAdvanceJob());
}

// The order is the stat order, not any order the protos happen to be in.
TEST(JobChoicesTest, FirstAdvancementOffersTheFourExplorersInStatOrder) {
  EXPECT_EQ(JobChoicesForStage(1), (std::vector<Job>{JOB_SWORDMAN, JOB_ARCHER,
                                                     JOB_MAGICIAN, JOB_ROGUE}));
}

TEST(JobChoicesTest, LaterAdvancementsHaveNoChoicesYet) {
  EXPECT_TRUE(JobChoicesForStage(2).empty());
  EXPECT_TRUE(JobChoicesForStage(0).empty());
}

// --- ResetStatsForJob ---

// The stats a level-10 Beginner carries, straight from the starting proto.
CharacterInstance MakeBeginnerAtTen(std::mt19937& rng) {
  Character proto;
  proto.set_level(10);
  proto.set_job(JOB_BEGINNER);
  proto.set_ap(45);  // 5 per level over levels 2-10
  proto.mutable_allocated_stats()->set_str(13);
  proto.mutable_allocated_stats()->set_dex(4);
  proto.mutable_allocated_stats()->set_int_(4);
  proto.mutable_allocated_stats()->set_luk(4);
  proto.mutable_allocated_stats()->set_hp(50);
  proto.mutable_allocated_stats()->set_mp(15);
  return CharacterInstance(rng, std::move(proto));
}

TEST_F(AdvanceJobTest, ResetSeatsThePrimaryStatAndRefundsTheRest) {
  CharacterInstance c = MakeBeginnerAtTen(rng_);
  c.ResetStatsForJob(JOB_ROGUE);
  const AllocatedStats& s = c.proto().allocated_stats();
  EXPECT_EQ(s.luk(), 25);  // the Rogue's primary
  EXPECT_EQ(s.str(), 4);   // the Beginner's 13 does not strand here
  EXPECT_EQ(s.dex(), 4);
  EXPECT_EQ(s.int_(), 4);
  // 45 unspent + 9 refunded from STR, less the 21 that seats LUK.
  EXPECT_EQ(c.proto().ap(), 33);
}

// The refund is computed from the stats on hand, so a player who had already
// spent AP lands in exactly the same place as one who had not.
TEST_F(AdvanceJobTest, ResetIgnoresWhatWasAlreadySpent) {
  CharacterInstance c = MakeBeginnerAtTen(rng_);
  ASSERT_TRUE(c.AllocateStat(STAT_FIELD_STR, 30));
  ASSERT_TRUE(c.AllocateStat(STAT_FIELD_INT, 15));
  ASSERT_EQ(c.proto().ap(), 0);
  c.ResetStatsForJob(JOB_MAGICIAN);
  EXPECT_EQ(c.proto().allocated_stats().int_(), 25);
  EXPECT_EQ(c.proto().allocated_stats().str(), 4);
  EXPECT_EQ(c.proto().ap(), 33);
}

// HP and MP sit in the same message but are granted by leveling, so the reset
// must leave them alone.
TEST_F(AdvanceJobTest, ResetLeavesLeveledHpAndMpAlone) {
  CharacterInstance c = MakeBeginnerAtTen(rng_);
  c.ResetStatsForJob(JOB_SWORDMAN);
  EXPECT_EQ(c.proto().allocated_stats().hp(), 50);
  EXPECT_EQ(c.proto().allocated_stats().mp(), 15);
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
  Skill skill =
      MakeSkill("Slash Blast", /*advancement=*/JOB_ADVANCEMENT_SWORDMAN,
                /*max_level=*/20);
  EXPECT_TRUE(c.LearnSkill(skill));
  EXPECT_EQ(c.skill_level(skill), 1);
  EXPECT_EQ(c.sp(1), 4);
}

TEST_F(LearnSkillTest, DefaultAmountIsOne) {
  CharacterInstance c = MakeCharacterWithSp(rng_, /*stage=*/1, /*sp=*/5);
  Skill skill =
      MakeSkill("Slash Blast", /*advancement=*/JOB_ADVANCEMENT_SWORDMAN,
                /*max_level=*/20);
  c.LearnSkill(skill);
  c.LearnSkill(skill);
  EXPECT_EQ(c.skill_level(skill), 2);
  EXPECT_EQ(c.sp(1), 3);
}

TEST_F(LearnSkillTest, MultiPointSpendWorks) {
  CharacterInstance c = MakeCharacterWithSp(rng_, /*stage=*/1, /*sp=*/10);
  Skill skill =
      MakeSkill("Slash Blast", /*advancement=*/JOB_ADVANCEMENT_SWORDMAN,
                /*max_level=*/20);
  EXPECT_TRUE(c.LearnSkill(skill, 7));
  EXPECT_EQ(c.skill_level(skill), 7);
  EXPECT_EQ(c.sp(1), 3);
}

TEST_F(LearnSkillTest, RejectsWhenStageLacksSp) {
  CharacterInstance c = MakeCharacterWithSp(rng_, /*stage=*/1, /*sp=*/2);
  Skill skill =
      MakeSkill("Slash Blast", /*advancement=*/JOB_ADVANCEMENT_SWORDMAN,
                /*max_level=*/20);
  EXPECT_FALSE(c.LearnSkill(skill, 3));
  EXPECT_EQ(c.skill_level(skill), 0);
  EXPECT_EQ(c.sp(1), 2);
}

TEST_F(LearnSkillTest, RejectsRaisingPastMaxLevel) {
  CharacterInstance c = MakeCharacterWithSp(rng_, /*stage=*/1, /*sp=*/10);
  Skill skill = MakeSkill(
      "Slash Blast", /*advancement=*/JOB_ADVANCEMENT_SWORDMAN, /*max_level=*/3);
  EXPECT_TRUE(c.LearnSkill(skill, 3));   // to the cap
  EXPECT_FALSE(c.LearnSkill(skill, 1));  // one past
  EXPECT_EQ(c.skill_level(skill), 3);
  EXPECT_EQ(c.sp(1), 7);
}

TEST_F(LearnSkillTest, RejectsNonPositiveAmount) {
  CharacterInstance c = MakeCharacterWithSp(rng_, /*stage=*/1, /*sp=*/5);
  Skill skill =
      MakeSkill("Slash Blast", /*advancement=*/JOB_ADVANCEMENT_SWORDMAN,
                /*max_level=*/20);
  EXPECT_FALSE(c.LearnSkill(skill, 0));
  EXPECT_FALSE(c.LearnSkill(skill, -2));
  EXPECT_EQ(c.skill_level(skill), 0);
  EXPECT_EQ(c.sp(1), 5);
}

TEST_F(LearnSkillTest, SpendsFromTheAdvancementsStage) {
  // A 1st-job skill draws from stage-1 SP; SP parked in another stage is
  // untouchable to it. (A skill on a stage-2 advancement can't be exercised
  // until such an advancement exists -- see AdvancementForJobStage.)
  Character proto;
  (*proto.mutable_sp_by_stage())[1] = 5;
  (*proto.mutable_sp_by_stage())[2] = 5;
  CharacterInstance c(rng_, std::move(proto));
  Skill skill =
      MakeSkill("Slash Blast", JOB_ADVANCEMENT_SWORDMAN, /*max_level=*/20);
  EXPECT_TRUE(c.LearnSkill(skill, 5));
  EXPECT_EQ(c.sp(1), 0);
  EXPECT_EQ(c.sp(2), 5);  // stage 2 untouched
}

TEST_F(LearnSkillTest, RejectsASkillWithNoAdvancement) {
  // No advancement means stage 0, which holds no SP -- nothing to spend.
  CharacterInstance c = MakeCharacterWithSp(rng_, /*stage=*/1, /*sp=*/5);
  Skill skill = MakeSkill("Nameless", JOB_ADVANCEMENT_UNSPECIFIED, 20);
  EXPECT_FALSE(c.LearnSkill(skill));
}

// --- Advancement mapping ---

TEST(AdvancementMappingTest, FirstStageMapsToEachJobsFirstAdvancement) {
  EXPECT_EQ(AdvancementForJobStage(JOB_SWORDMAN, 1), JOB_ADVANCEMENT_SWORDMAN);
  EXPECT_EQ(AdvancementForJobStage(JOB_ARCHER, 1), JOB_ADVANCEMENT_ARCHER);
  EXPECT_EQ(AdvancementForJobStage(JOB_MAGICIAN, 1), JOB_ADVANCEMENT_MAGICIAN);
  EXPECT_EQ(AdvancementForJobStage(JOB_ROGUE, 1), JOB_ADVANCEMENT_ROGUE);
}

TEST(AdvancementMappingTest, UnreachedStageHasNoAdvancement) {
  // Stage 2 is not defined for any job yet.
  EXPECT_EQ(AdvancementForJobStage(JOB_SWORDMAN, 2),
            JOB_ADVANCEMENT_UNSPECIFIED);
  EXPECT_EQ(AdvancementForJobStage(JOB_BEGINNER, 1),
            JOB_ADVANCEMENT_UNSPECIFIED);
}

TEST(AdvancementMappingTest, FirstAdvancementsBuyFromStageOne) {
  EXPECT_EQ(StageForAdvancement(JOB_ADVANCEMENT_SWORDMAN), 1);
  EXPECT_EQ(StageForAdvancement(JOB_ADVANCEMENT_ARCHER), 1);
  EXPECT_EQ(StageForAdvancement(JOB_ADVANCEMENT_MAGICIAN), 1);
  EXPECT_EQ(StageForAdvancement(JOB_ADVANCEMENT_ROGUE), 1);
  EXPECT_EQ(StageForAdvancement(JOB_ADVANCEMENT_UNSPECIFIED), 0);
}

TEST_F(LearnSkillTest, UnlearnedSkillIsLevelZero) {
  CharacterInstance c = MakeCharacterWithSp(rng_, /*stage=*/1, /*sp=*/5);
  Skill skill =
      MakeSkill("Slash Blast", /*advancement=*/JOB_ADVANCEMENT_SWORDMAN,
                /*max_level=*/20);
  EXPECT_EQ(c.skill_level(skill), 0);
}

// --- CanEquip ---

TEST_F(CanEquipTest, ReturnsTrueWhenLevelAndJobMatch) {
  sword_.set_required_level(1);
  sword_.add_equip_job_categories(EQUIP_JOB_CATEGORY_WARRIOR);
  c_.AdvanceJob(JOB_SWORDMAN);
  EXPECT_TRUE(c_.CanEquip(sword_));
}

TEST_F(CanEquipTest, ReturnsFalseWhenLevelTooLow) {
  sword_.set_required_level(10);
  sword_.add_equip_job_categories(EQUIP_JOB_CATEGORY_WARRIOR);
  c_.AdvanceJob(JOB_SWORDMAN);
  EXPECT_FALSE(c_.CanEquip(sword_));
}

TEST_F(CanEquipTest, ReturnsFalseWhenWrongJob) {
  sword_.set_required_level(1);
  sword_.add_equip_job_categories(EQUIP_JOB_CATEGORY_BOWMAN);
  c_.AdvanceJob(JOB_SWORDMAN);
  EXPECT_FALSE(c_.CanEquip(sword_));
}

TEST_F(CanEquipTest, ReturnsTrueForUniversalItem) {
  sword_.set_required_level(1);
  sword_.add_equip_job_categories(EQUIP_JOB_CATEGORY_UNIVERSAL);
  c_.AdvanceJob(JOB_SWORDMAN);
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
  c_.AdvanceJob(JOB_SWORDMAN);
  EXPECT_TRUE(c_.CanEquip(sword_));
}

TEST_F(CanEquipTest, ReturnsFalseForEmptyJobCategories) {
  sword_.set_required_level(1);
  // No equip_job_categories set; no job can equip it.
  c_.AdvanceJob(JOB_SWORDMAN);
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
  c_.AdvanceJob(JOB_SWORDMAN);
  EXPECT_TRUE(c_.MeetsJob(sword_));
}

TEST_F(MeetsJobTest, TrueForUniversalCategory) {
  sword_.add_equip_job_categories(EQUIP_JOB_CATEGORY_UNIVERSAL);
  c_.AdvanceJob(JOB_SWORDMAN);
  EXPECT_TRUE(c_.MeetsJob(sword_));
}

TEST_F(MeetsJobTest, TrueWhenJobMatches) {
  sword_.add_equip_job_categories(EQUIP_JOB_CATEGORY_WARRIOR);
  c_.AdvanceJob(JOB_SWORDMAN);
  EXPECT_TRUE(c_.MeetsJob(sword_));
}

TEST_F(MeetsJobTest, FalseWhenJobDoesNotMatch) {
  sword_.add_equip_job_categories(EQUIP_JOB_CATEGORY_BOWMAN);
  c_.AdvanceJob(JOB_SWORDMAN);
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

// --- Buy ---

// Fixture providing a 5000-meso weapon and a character who can afford two.
class BuyTest : public CharacterTest {
 protected:
  void SetUp() override {
    sword_.set_name("Long Sword");
    sword_.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
    sword_.set_shop_price(5000);
    c_.AddMeso(11000);
  }

  EquipPrototype sword_;
  CharacterInstance c_ = MakeCharacter(rng_);
};

TEST_F(BuyTest, TakesTheMesoAndGivesTheItem) {
  EXPECT_TRUE(c_.Buy(sword_, 1));
  EXPECT_EQ(c_.meso(), 6000);
  ASSERT_EQ(c_.inventory().size(), 1);
  EXPECT_EQ(c_.inventory()[0].name(), "Long Sword");
}

// Equips do not stack, so two bought at once are two rows, not one row of two.
TEST_F(BuyTest, EachCopyIsItsOwnItem) {
  EXPECT_TRUE(c_.Buy(sword_, 2));
  EXPECT_EQ(c_.meso(), 1000);
  EXPECT_EQ(c_.inventory().size(), 2);
}

// The part it could afford must not go through: a purchase that half-happens
// would take meso for an order the player never placed.
TEST_F(BuyTest, BuysNothingWhenItCannotBuyEverything) {
  EXPECT_FALSE(c_.Buy(sword_, 3));
  EXPECT_EQ(c_.meso(), 11000);
  EXPECT_EQ(c_.inventory().size(), 0);
}

TEST_F(BuyTest, SpendingEverythingIsAllowed) {
  c_.AddMeso(4000);  // exactly 15000
  EXPECT_TRUE(c_.Buy(sword_, 3));
  EXPECT_EQ(c_.meso(), 0);
  EXPECT_EQ(c_.inventory().size(), 3);
}

TEST_F(BuyTest, WillNotSellWhatTheShopDoesNotStock) {
  EquipPrototype unpriced;
  unpriced.set_name("Heirloom");
  EXPECT_FALSE(c_.Buy(unpriced, 1));
  EXPECT_EQ(c_.meso(), 11000);
  EXPECT_EQ(c_.inventory().size(), 0);
}

TEST_F(BuyTest, NonPositiveCountIsNoOp) {
  EXPECT_FALSE(c_.Buy(sword_, 0));
  EXPECT_FALSE(c_.Buy(sword_, -2));
  EXPECT_EQ(c_.meso(), 11000);
  EXPECT_EQ(c_.inventory().size(), 0);
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

// --- Throwing stars ---

// A weapon of `type` carrying `attack`, and a stack of stars for the star slot.
class ThrowingStarTest : public CharacterTest {
 protected:
  EquipPrototype Weapon(EquipType type, int attack) {
    EquipPrototype proto;
    proto.set_name("Weapon");
    proto.set_equip_type(type);
    proto.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
    proto.mutable_base_stats()->set_attack(attack);
    return proto;
  }
  EquipPrototype Stars(int attack) {
    EquipPrototype proto;
    proto.set_name("Subi Throwing-Stars");
    proto.set_equip_type(EQUIP_TYPE_THROWING_STAR);
    proto.set_equip_slot(EQUIP_SLOT_STARS);
    proto.mutable_base_stats()->set_attack(attack);
    return proto;
  }
  CharacterInstance c_ = MakeCharacter(rng_);
};

TEST_F(ThrowingStarTest, ArmsAClaw) {
  c_.PickUp(std::make_unique<EquipInstance>(Weapon(EQUIP_TYPE_CLAW, 10)));
  c_.PickUp(std::make_unique<EquipInstance>(Stars(15)));
  ASSERT_TRUE(c_.Equip(0));
  ASSERT_TRUE(c_.Equip(0));
  EXPECT_EQ(c_.equip_stats().attack(), 25);
}

TEST_F(ThrowingStarTest, ArmsNothingElse) {
  // Worn all the same -- a thief may carry stars while holding a dagger -- but
  // there is no claw to throw them, so their attack does not count.
  c_.PickUp(std::make_unique<EquipInstance>(Weapon(EQUIP_TYPE_DAGGER, 25)));
  c_.PickUp(std::make_unique<EquipInstance>(Stars(15)));
  ASSERT_TRUE(c_.Equip(0));
  ASSERT_TRUE(c_.Equip(0));
  EXPECT_EQ(c_.equip_stats().attack(), 25);
}

TEST_F(ThrowingStarTest, StopsCountingWhenTheClawComesOff) {
  c_.PickUp(std::make_unique<EquipInstance>(Weapon(EQUIP_TYPE_CLAW, 10)));
  c_.PickUp(std::make_unique<EquipInstance>(Stars(15)));
  ASSERT_TRUE(c_.Equip(0));
  ASSERT_TRUE(c_.Equip(0));
  ASSERT_EQ(c_.equip_stats().attack(), 25);

  // Swapping the claw for a dagger has to re-evaluate the stars, not just
  // subtract the claw.
  c_.PickUp(std::make_unique<EquipInstance>(Weapon(EQUIP_TYPE_DAGGER, 25)));
  ASSERT_TRUE(c_.Equip(0));
  EXPECT_EQ(c_.equip_stats().attack(), 25);
}

// --- capacity ---

// A fixture for the 128-slot tab limit, with an Etc item (default max_stack
// 200) and a plain equip to fill tabs with.
class CapacityTest : public CharacterTest {
 protected:
  void SetUp() override {
    shell_.set_name("Green Snail Shell");
    shell_.set_category(ITEM_CATEGORY_ETC);
    other_.set_name("Blue Snail Shell");
    other_.set_category(ITEM_CATEGORY_ETC);
    sword_.set_name("Sword");
    sword_.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
    sword_.set_shop_price(10);
  }
  // Opens `count` distinct Etc stacks, so the tab fills by slots rather than
  // by one item stacking up.
  void OpenDistinctStacks(int count) {
    for (int i = 0; i < count; ++i) {
      ItemPrototype proto;
      proto.set_name("Junk " + std::to_string(i));
      proto.set_category(ITEM_CATEGORY_ETC);
      c_.AddStackable(proto, 1);
    }
  }
  CharacterInstance c_ = MakeCharacter(rng_);
  ItemPrototype shell_;
  ItemPrototype other_;
  EquipPrototype sword_;
};

TEST_F(CapacityTest, TheEquipTabHoldsExactlyTheCapacity) {
  for (int i = 0; i < kTabCapacity; ++i) {
    EXPECT_TRUE(c_.PickUp(std::make_unique<EquipInstance>(sword_)))
        << "refused item " << i;
  }
  EXPECT_EQ(c_.inventory().size(), kTabCapacity);
  EXPECT_FALSE(c_.PickUp(std::make_unique<EquipInstance>(sword_)));
  EXPECT_EQ(c_.inventory().size(), kTabCapacity);
}

TEST_F(CapacityTest, RoomForAnEquipCountsFreeSlots) {
  EXPECT_EQ(c_.RoomFor(sword_), kTabCapacity);
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  EXPECT_EQ(c_.RoomFor(sword_), kTabCapacity - 2);
}

// Traces sit on the equip tab too, so they take slots like anything else.
TEST_F(CapacityTest, RoomForAnEquipCountsTracesAsWell) {
  c_.PickUp(std::make_unique<EquipTrace>(sword_, Equip()));
  EXPECT_EQ(c_.RoomFor(sword_), kTabCapacity - 1);
}

// --- RoomFor(ItemPrototype) ---

TEST_F(CapacityTest, RoomForAStackableOnAnEmptyTabIsEveryStack) {
  // 128 slots of 200 apiece, the Etc default.
  EXPECT_EQ(c_.RoomFor(shell_), kTabCapacity * 200);
}

// The case that motivated the rule: part-full stacks count for what is left in
// them, on top of a whole stack for every slot still free.
TEST_F(CapacityTest, RoomForAStackableAddsPartStacksToFreeSlots) {
  OpenDistinctStacks(kTabCapacity - 11);
  c_.AddStackable(shell_, 100);
  // 118 stacks open, so 10 slots free, and the shell stack has 100 spare.
  ASSERT_EQ(c_.stackables(ITEM_CATEGORY_ETC).size(), kTabCapacity - 10);
  EXPECT_EQ(c_.RoomFor(shell_), 10 * 200 + 100);
}

// Room in someone else's stack is no use.
TEST_F(CapacityTest, RoomForAStackableIgnoresPartStacksOfOtherItems) {
  OpenDistinctStacks(kTabCapacity - 11);
  c_.AddStackable(other_, 100);
  ASSERT_EQ(c_.stackables(ITEM_CATEGORY_ETC).size(), kTabCapacity - 10);
  EXPECT_EQ(c_.RoomFor(shell_), 10 * 200);
}

// With no slot left the only room is what the open stacks of that item can
// still take.
TEST_F(CapacityTest, RoomForAStackableOnAFullTabIsOnlyTheOpenStacks) {
  OpenDistinctStacks(kTabCapacity - 1);
  c_.AddStackable(shell_, 150);
  ASSERT_EQ(c_.stackables(ITEM_CATEGORY_ETC).size(), kTabCapacity);
  EXPECT_EQ(c_.RoomFor(shell_), 50);
}

TEST_F(CapacityTest, RoomForAStackableOnAFullTabOfOtherItemsIsNone) {
  OpenDistinctStacks(kTabCapacity);
  EXPECT_EQ(c_.RoomFor(shell_), 0);
}

// Use items stack far deeper than Etc ones, and the room follows the item
// rather than a fixed number.
TEST_F(CapacityTest, RoomForAStackableFollowsTheItemsOwnStackSize) {
  ItemPrototype potion;
  potion.set_name("Red Potion");
  potion.set_category(ITEM_CATEGORY_USE);
  EXPECT_EQ(c_.RoomFor(potion), kTabCapacity * 9999);
  ItemPrototype tiny;
  tiny.set_name("Odd Thing");
  tiny.set_category(ITEM_CATEGORY_ETC);
  tiny.set_max_stack(5);
  EXPECT_EQ(c_.RoomFor(tiny), kTabCapacity * 5);
}

// --- AddStackable against the limit ---

TEST_F(CapacityTest, AddStackableReportsWhatItTook) {
  EXPECT_EQ(c_.AddStackable(shell_, 250), 250);
}

// A drop that does not fit is taken as far as it goes and the rest is lost.
TEST_F(CapacityTest, AddStackableTakesWhatFitsAndLosesTheRest) {
  OpenDistinctStacks(kTabCapacity - 1);
  c_.AddStackable(shell_, 150);
  ASSERT_EQ(c_.RoomFor(shell_), 50);
  EXPECT_EQ(c_.AddStackable(shell_, 500), 50);
  EXPECT_EQ(c_.RoomFor(shell_), 0);
  // The tab did not grow past its limit to hold the overflow.
  EXPECT_EQ(c_.stackables(ITEM_CATEGORY_ETC).size(), kTabCapacity);
}

// Topping up an open stack costs no slot, so a full tab still takes some.
TEST_F(CapacityTest, AddStackableStillTopsUpOnAFullTab) {
  OpenDistinctStacks(kTabCapacity - 1);
  c_.AddStackable(shell_, 10);
  ASSERT_EQ(c_.stackables(ITEM_CATEGORY_ETC).size(), kTabCapacity);
  EXPECT_EQ(c_.AddStackable(shell_, 30), 30);
}

// --- Buy against the limit ---

TEST_F(CapacityTest, BuyRefusesWhatTheBagCannotHold) {
  c_.AddMeso(1000000);
  for (int i = 0; i < kTabCapacity - 2; ++i) {
    c_.PickUp(std::make_unique<EquipInstance>(sword_));
  }
  int64_t before = c_.meso();
  EXPECT_FALSE(c_.Buy(sword_, 3));
  EXPECT_EQ(c_.meso(), before) << "a refused purchase still took the meso";
  EXPECT_EQ(c_.inventory().size(), kTabCapacity - 2);
  // Exactly filling it is fine.
  EXPECT_TRUE(c_.Buy(sword_, 2));
  EXPECT_EQ(c_.inventory().size(), kTabCapacity);
}

// --- UseStackable ---

ItemPrototype LevelUpItem() {
  ItemPrototype item;
  item.set_name("Level-Up");
  item.set_category(ITEM_CATEGORY_USE);
  item.set_effect(ITEM_EFFECT_LEVEL_UP);
  return item;
}

TEST_F(CharacterTest, UsingALevelUpItemLevelsAndSpendsOne) {
  CharacterInstance c = MakeCharacter(rng_);
  c.AddStackable(LevelUpItem(), 3);
  int before = c.proto().level();

  EXPECT_TRUE(c.UseStackable(ITEM_CATEGORY_USE, 0));
  EXPECT_EQ(c.proto().level(), before + 1);
  EXPECT_EQ(c.stackables(ITEM_CATEGORY_USE)[0].count(), 2);
}

// The whole point of the item: it is the level itself, not the EXP for one.
TEST_F(CharacterTest, ALevelUpItemGrantsTheLevelWhateverTheExp) {
  CharacterInstance c = MakeCharacter(rng_);
  c.AddStackable(LevelUpItem(), 1);
  ASSERT_EQ(c.proto().exp(), 0);
  EXPECT_TRUE(c.UseStackable(ITEM_CATEGORY_USE, 0));
  EXPECT_EQ(c.proto().level(), 2);
}

TEST_F(CharacterTest, TheLastCopyTakesTheStackWithIt) {
  CharacterInstance c = MakeCharacter(rng_);
  c.AddStackable(LevelUpItem(), 1);
  EXPECT_TRUE(c.UseStackable(ITEM_CATEGORY_USE, 0));
  EXPECT_TRUE(c.stackables(ITEM_CATEGORY_USE).empty());
}

// An item that does nothing is not spent doing it.
TEST_F(CharacterTest, UsingAnItemWithNoEffectConsumesNothing) {
  ItemPrototype inert;
  inert.set_name("Odd Pebble");
  inert.set_category(ITEM_CATEGORY_USE);
  CharacterInstance c = MakeCharacter(rng_);
  c.AddStackable(inert, 5);
  int before = c.proto().level();

  EXPECT_FALSE(c.UseStackable(ITEM_CATEGORY_USE, 0));
  EXPECT_EQ(c.stackables(ITEM_CATEGORY_USE)[0].count(), 5);
  EXPECT_EQ(c.proto().level(), before);
}

TEST_F(CharacterTest, UsingAStackThatIsNotThereIsANoOp) {
  CharacterInstance c = MakeCharacter(rng_);
  EXPECT_FALSE(c.UseStackable(ITEM_CATEGORY_USE, 0));
  c.AddStackable(LevelUpItem(), 1);
  EXPECT_FALSE(c.UseStackable(ITEM_CATEGORY_USE, 7));
  EXPECT_FALSE(c.UseStackable(ITEM_CATEGORY_USE, -1));
  EXPECT_EQ(c.stackables(ITEM_CATEGORY_USE)[0].count(), 1);
}

}  // namespace
}  // namespace ms
