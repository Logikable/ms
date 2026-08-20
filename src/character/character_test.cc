#include "src/character/character.h"

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "src/character/exp_table.h"
#include "src/item/equip_instance.h"
#include "src/item/inventory.h"
#include "src/item/item.h"
#include "src/item/star_force_cost.h"
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

// A purse no amount of farming would fill. Star forcing is priced now, and one
// attempt on a level 138 item runs to nine figures -- a test that rolls until
// the item explodes has to be able to pay for every roll.
CharacterInstance MakeRichCharacter(std::mt19937& rng) {
  CharacterInstance c = MakeCharacter(rng);
  c.AddMeso(1'000'000'000'000);
  return c;
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

// A character carrying `sp` skill points in `stage` and nothing else -- a
// Swordman, because the points are only spendable on a book the character's
// own job has, and the stage alone does not say whose book that is.
CharacterInstance MakeCharacterWithSp(std::mt19937& rng, int stage, int sp,
                                      Job job = JOB_SWORDMAN) {
  Character proto;
  proto.set_job(job);
  proto.set_job_stage(stage);
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

// Every band pays exactly what its book costs: 60, 90, 120, and 200 for the
// 4th job, which is the one band that pays five a level rather than three.
TEST_F(LevelUpTest, EachBandPaysExactlyWhatItsBookCosts) {
  CharacterInstance c = MakeCharacter(rng_, /*level=*/10);
  for (int i = 0; i < 90; ++i) {
    c.LevelUp();  // levels 11..100
  }
  EXPECT_EQ(c.sp(1), 60);
  EXPECT_EQ(c.sp(2), 90);
  EXPECT_EQ(c.sp(3), 120);
  EXPECT_EQ(c.sp(4), 0) << "level 100 is the last of the 3rd job's band";

  for (int i = 0; i < 40; ++i) {
    c.LevelUp();  // levels 101..140, the cap
  }
  EXPECT_EQ(c.proto().level(), 140);
  EXPECT_EQ(c.sp(4), 200) << "40 levels at five, and the book costs the lot";
}

TEST_F(LevelUpTest, TheFourthJobPaysFiveALevel) {
  CharacterInstance c = MakeCharacter(rng_, /*level=*/100);
  c.LevelUp();
  EXPECT_EQ(c.sp(4), 5);
  c.LevelUp();
  EXPECT_EQ(c.sp(4), 10);
  EXPECT_EQ(c.sp(3), 0) << "the 3rd job's band is closed behind them";
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

// --- GainsForLevels ---

class GainsForLevelsTest : public CharacterTest {
 protected:
  // What levelling from `from` to `to` really hands over, read off a character
  // that actually climbed it. Sums SP across every stage, since a span can
  // cross a band and the totals are what the caller is after.
  LevelGains Actual(int from, int to) {
    CharacterInstance c = MakeCharacter(rng_, from);
    int ap_before = c.proto().ap();
    int sp_before = 0;
    for (const std::pair<const int32_t, int32_t>& pool :
         c.proto().sp_by_stage()) {
      sp_before += pool.second;
    }
    for (int level = from; level < to; ++level) {
      c.LevelUp();
    }
    int sp_after = 0;
    for (const std::pair<const int32_t, int32_t>& pool :
         c.proto().sp_by_stage()) {
      sp_after += pool.second;
    }
    return {c.proto().ap() - ap_before, sp_after - sp_before};
  }
};

TEST_F(GainsForLevelsTest, ASingleLevelGrantsFiveAp) {
  LevelGains gains = GainsForLevels(1, 2);
  EXPECT_EQ(gains.ap, 5);
  EXPECT_EQ(gains.sp, 0) << "below the level-11 start of 1st-job SP";
}

TEST_F(GainsForLevelsTest, TotalsEveryLevelInTheSpan) {
  LevelGains gains = GainsForLevels(1, 5);
  EXPECT_EQ(gains.ap, 20) << "four levels at five AP each";
}

TEST_F(GainsForLevelsTest, CountsSpOnlyForTheLevelsThatGrantIt) {
  // 10 -> 12 arrives at 11 and 12; SP starts at 11, so both pay.
  EXPECT_EQ(GainsForLevels(10, 12).sp, 6);
  // 8 -> 10 arrives at 9 and 10, both below the band.
  EXPECT_EQ(GainsForLevels(8, 10).sp, 0);
}

// A span crossing a job band still totals what was earned, even though
// LevelUp put the two halves in different stage pools.
TEST_F(GainsForLevelsTest, TotalsSpAcrossAJobBandBoundary) {
  LevelGains gains = GainsForLevels(29, 32);
  EXPECT_EQ(gains.sp, 9) << "levels 30, 31 and 32, three SP each";
}

TEST_F(GainsForLevelsTest, ASpanThatGoesNowhereGrantsNothing) {
  EXPECT_EQ(GainsForLevels(7, 7).ap, 0);
  EXPECT_EQ(GainsForLevels(7, 7).sp, 0);
  EXPECT_EQ(GainsForLevels(9, 4).ap, 0) << "backwards is not a windfall";
  EXPECT_EQ(GainsForLevels(9, 4).sp, 0);
}

// The point of the helper is to answer for a span what LevelUp answers for one
// level at a time. These hold it against a character that really climbed, so a
// change to either that misses the other fails here rather than showing a
// player the wrong number.
TEST_F(GainsForLevelsTest, AgreesWithLevellingUpForReal) {
  const std::pair<int, int> spans[] = {
      {1, 2},   {1, 10}, {10, 11},  {10, 30},
      {29, 32}, {1, 40}, {99, 104}, {100, 140},
  };
  for (const std::pair<int, int>& span : spans) {
    LevelGains predicted = GainsForLevels(span.first, span.second);
    LevelGains actual = Actual(span.first, span.second);
    EXPECT_EQ(predicted.ap, actual.ap)
        << "AP for levels " << span.first << " -> " << span.second;
    EXPECT_EQ(predicted.sp, actual.sp)
        << "SP for levels " << span.first << " -> " << span.second;
  }
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

// A stage whose branches are unwritten has nothing to offer, and the character
// must not be told otherwise. Every branch is written down to its 4th job now,
// so what this asks about is the 5th, which nothing reaches.
TEST_F(AdvanceJobTest, NoAdvancementWithNoJobsBehindIt) {
  CharacterInstance c = MakeCharacter(rng_, /*level=*/100);
  c.AdvanceJob(JOB_ROGUE);
  c.AdvanceJob(JOB_BANDIT);
  c.AdvanceJob(JOB_CHIEF_BANDIT);
  c.AdvanceJob(JOB_SHADOWER);
  EXPECT_FALSE(c.CanAdvanceJob());
}

TEST_F(AdvanceJobTest, ASwordmanAtThirtyIsOfferedTheirSecondJob) {
  CharacterInstance c = MakeCharacter(rng_, /*level=*/29);
  c.AdvanceJob(JOB_SWORDMAN);
  EXPECT_FALSE(c.CanAdvanceJob());  // the level, not the job, is what is short
  c.LevelUp();
  EXPECT_TRUE(c.CanAdvanceJob());
}

TEST_F(AdvanceJobTest, SecondJobPutsTheCharacterAtStageTwo) {
  CharacterInstance c = MakeCharacter(rng_, /*level=*/30);
  c.AdvanceJob(JOB_SWORDMAN);
  c.AdvanceJob(JOB_SPEARMAN);
  EXPECT_EQ(c.proto().job(), JOB_SPEARMAN);
  EXPECT_EQ(c.proto().job_stage(), 2);
}

// The order is the stat order, not any order the protos happen to be in.
TEST(JobChoicesTest, OffersTheFourExplorersInStatOrder) {
  EXPECT_EQ(
      JobChoicesForStage(JOB_BEGINNER, 1),
      (std::vector<Job>{JOB_SWORDMAN, JOB_ARCHER, JOB_MAGICIAN, JOB_ROGUE}));
}

// All three, in the order the Job enum names them. The Page was held back
// while its book was empty; it has one now.
TEST(JobChoicesTest, ASwordmanIsOfferedEveryWarriorBranch) {
  EXPECT_EQ(JobChoicesForStage(JOB_SWORDMAN, 2),
            (std::vector<Job>{JOB_FIGHTER, JOB_PAGE, JOB_SPEARMAN}));
}

TEST(JobChoicesTest, AnArcherIsOfferedBothBowmanBranches) {
  EXPECT_EQ(JobChoicesForStage(JOB_ARCHER, 2),
            (std::vector<Job>{JOB_HUNTER, JOB_CROSSBOWMAN}));
}

TEST(JobChoicesTest, AMagicianIsOfferedAllThreeBranches) {
  EXPECT_EQ(JobChoicesForStage(JOB_MAGICIAN, 2),
            (std::vector<Job>{JOB_ICE_LIGHTNING_WIZARD, JOB_FIRE_POISON_WIZARD,
                              JOB_CLERIC}));
}

TEST(JobChoicesTest, ARogueIsOfferedBothThiefBranches) {
  EXPECT_EQ(JobChoicesForStage(JOB_ROGUE, 2),
            (std::vector<Job>{JOB_ASSASSIN, JOB_BANDIT}));
}

// The 3rd advancement narrows rather than forking, so it offers one job and
// not a set. Every warrior and archer branch has one; no other class does.
TEST(JobChoicesTest, AThirdAdvancementOffersOneJob) {
  EXPECT_EQ(JobChoicesForStage(JOB_SPEARMAN, 3),
            (std::vector<Job>{JOB_BERSERKER}));
  EXPECT_EQ(JobChoicesForStage(JOB_FIGHTER, 3),
            (std::vector<Job>{JOB_CRUSADER}));
  EXPECT_EQ(JobChoicesForStage(JOB_PAGE, 3),
            (std::vector<Job>{JOB_WHITE_KNIGHT}));
  EXPECT_EQ(JobChoicesForStage(JOB_HUNTER, 3), (std::vector<Job>{JOB_RANGER}));
  EXPECT_EQ(JobChoicesForStage(JOB_CROSSBOWMAN, 3),
            (std::vector<Job>{JOB_SNIPER}));
  EXPECT_EQ(JobChoicesForStage(JOB_ICE_LIGHTNING_WIZARD, 3),
            (std::vector<Job>{JOB_ICE_LIGHTNING_MAGE}));
  EXPECT_EQ(JobChoicesForStage(JOB_FIRE_POISON_WIZARD, 3),
            (std::vector<Job>{JOB_FIRE_POISON_MAGE}));
  EXPECT_EQ(JobChoicesForStage(JOB_CLERIC, 3), (std::vector<Job>{JOB_PRIEST}));
  EXPECT_EQ(JobChoicesForStage(JOB_ASSASSIN, 3),
            (std::vector<Job>{JOB_HERMIT}));
  EXPECT_EQ(JobChoicesForStage(JOB_BANDIT, 3),
            (std::vector<Job>{JOB_CHIEF_BANDIT}));
  // The 4th narrows no further either, and all ten are written now. Nothing
  // is written past one, so a 4th job is offered nothing at all.
  EXPECT_EQ(JobChoicesForStage(JOB_BERSERKER, 4),
            (std::vector<Job>{JOB_DARK_KNIGHT}));
  EXPECT_EQ(JobChoicesForStage(JOB_WHITE_KNIGHT, 4),
            (std::vector<Job>{JOB_PALADIN}));
  EXPECT_EQ(JobChoicesForStage(JOB_CRUSADER, 4), (std::vector<Job>{JOB_HERO}));
  EXPECT_EQ(JobChoicesForStage(JOB_RANGER, 4),
            (std::vector<Job>{JOB_BOW_MASTER}));
  EXPECT_EQ(JobChoicesForStage(JOB_SNIPER, 4),
            (std::vector<Job>{JOB_MARKSMAN}));
  EXPECT_EQ(JobChoicesForStage(JOB_ICE_LIGHTNING_MAGE, 4),
            (std::vector<Job>{JOB_ICE_LIGHTNING_ARCH_MAGE}));
  EXPECT_EQ(JobChoicesForStage(JOB_HERMIT, 4),
            (std::vector<Job>{JOB_NIGHT_LORD}));
  EXPECT_EQ(JobChoicesForStage(JOB_CHIEF_BANDIT, 4),
            (std::vector<Job>{JOB_SHADOWER}));
  EXPECT_TRUE(JobChoicesForStage(JOB_DARK_KNIGHT, 5).empty());
  EXPECT_TRUE(JobChoicesForStage(JOB_PALADIN, 5).empty());
  EXPECT_TRUE(JobChoicesForStage(JOB_BEGINNER, 0).empty());
}

// A character keeps every book they bought on the way up: each page has to
// stay on the skills tab, and each SP pool has to stay spendable.
TEST(JobChoicesTest, ABerserkerKeepsEveryBookBelowTheirOwn) {
  EXPECT_EQ(AdvancementForJobStage(JOB_SPEARMAN, 1), JOB_ADVANCEMENT_SWORDMAN);
  EXPECT_EQ(AdvancementForJobStage(JOB_SPEARMAN, 2), JOB_ADVANCEMENT_SPEARMAN);
  EXPECT_EQ(AdvancementForJobStage(JOB_BERSERKER, 1), JOB_ADVANCEMENT_SWORDMAN);
  EXPECT_EQ(AdvancementForJobStage(JOB_BERSERKER, 2), JOB_ADVANCEMENT_SPEARMAN);
  EXPECT_EQ(AdvancementForJobStage(JOB_BERSERKER, 3),
            JOB_ADVANCEMENT_BERSERKER);
  EXPECT_EQ(StageForAdvancement(JOB_ADVANCEMENT_BERSERKER), 3);
}

// A Berserker is a warrior in every table that reads a job: the stat they
// spend AP into, the HP a level grants them, and the gear they may wear.
TEST(JobChoicesTest, ABerserkerCountsAsAWarriorThroughout) {
  EXPECT_EQ(PrimaryStatField(JOB_BERSERKER), STAT_FIELD_STR);
  std::mt19937 rng(1);
  CharacterInstance c = MakeCharacter(rng);
  c.AdvanceJob(JOB_SWORDMAN);
  c.AdvanceJob(JOB_SPEARMAN);
  c.AdvanceJob(JOB_BERSERKER);
  int before = c.proto().allocated_stats().hp();
  c.LevelUp();
  EXPECT_EQ(c.proto().allocated_stats().hp() - before, 48) << "warrior HP rate";
  EquipPrototype spear;
  spear.set_required_level(1);
  spear.add_equip_job_categories(EQUIP_JOB_CATEGORY_WARRIOR);
  EXPECT_TRUE(c.CanEquip(spear));
}

// Every advancement names exactly the job that takes it, and that job answers
// with the advancement again at the stage it belongs to. A branch wired into
// one direction and not the other would send the workbench to the wrong job.
TEST(JobChoicesTest, EveryAdvancementRoundTripsToItsJob) {
  for (int i = 1; i <= JobAdvancement_MAX; ++i) {
    JobAdvancement advancement = static_cast<JobAdvancement>(i);
    Job job = JobForAdvancement(advancement);
    ASSERT_NE(job, JOB_UNSPECIFIED) << JobAdvancement_Name(advancement);
    EXPECT_EQ(AdvancementForJobStage(job, StageForAdvancement(advancement)),
              advancement);
  }
}

TEST(JobChoicesTest, NoJobTakesAnUnspecifiedAdvancement) {
  EXPECT_EQ(JobForAdvancement(JOB_ADVANCEMENT_UNSPECIFIED), JOB_UNSPECIFIED);
}

// The thresholds LevelUp offers an advancement at, read back out: a 1st job
// spans up to 30 and a 2nd up to 60, which is what puts a character asking to
// start at the top of one where they belong.
TEST(JobChoicesTest, EachStageEndsAtItsNextAdvancement) {
  EXPECT_EQ(NextAdvancementLevel(0), 10);
  EXPECT_EQ(NextAdvancementLevel(1), 30);
  EXPECT_EQ(NextAdvancementLevel(2), 60);
  EXPECT_EQ(NextAdvancementLevel(-1), 0);
  EXPECT_EQ(NextAdvancementLevel(99), 0);
}

// --- skill requirements ---

// Hyper Body's shape: it wants three points in Iron Wall first.
Skill MakeGatedSkill() {
  Skill skill;
  skill.set_name("Hyper Body");
  skill.set_kind(SKILL_KIND_PASSIVE);
  skill.set_job_advancement(JOB_ADVANCEMENT_SPEARMAN);
  skill.set_max_level(10);
  skill.mutable_required_skill()->set_skill_name("Iron Wall");
  skill.mutable_required_skill()->set_level(3);
  return skill;
}

Skill MakeGateSkill() {
  Skill skill;
  skill.set_name("Iron Wall");
  skill.set_kind(SKILL_KIND_PASSIVE);
  skill.set_job_advancement(JOB_ADVANCEMENT_SPEARMAN);
  skill.set_max_level(10);
  return skill;
}

// A Spearman with SP to spend in their second-job pool.
CharacterInstance MakeSpearman(std::mt19937& rng, int sp) {
  Character proto;
  proto.set_level(60);
  proto.set_job(JOB_SPEARMAN);
  proto.set_job_stage(2);
  (*proto.mutable_sp_by_stage())[2] = sp;
  return CharacterInstance(rng, std::move(proto));
}

TEST_F(CharacterTest, ASkillWaitingOnAnotherCannotBeLearned) {
  CharacterInstance c = MakeSpearman(rng_, 20);
  EXPECT_FALSE(c.MeetsSkillRequirement(MakeGatedSkill()));
  EXPECT_FALSE(c.LearnSkill(MakeGatedSkill()));
  EXPECT_EQ(c.skill_level(MakeGatedSkill()), 0);
  EXPECT_EQ(c.sp(2), 20);  // and the point is not taken either
}

TEST_F(CharacterTest, PartOfTheRequirementIsStillNotEnough) {
  CharacterInstance c = MakeSpearman(rng_, 20);
  ASSERT_TRUE(c.LearnSkill(MakeGateSkill(), 2));
  EXPECT_FALSE(c.MeetsSkillRequirement(MakeGatedSkill()));
  EXPECT_FALSE(c.LearnSkill(MakeGatedSkill()));
}

TEST_F(CharacterTest, MeetingTheRequirementOpensTheSkill) {
  CharacterInstance c = MakeSpearman(rng_, 20);
  ASSERT_TRUE(c.LearnSkill(MakeGateSkill(), 3));
  EXPECT_TRUE(c.MeetsSkillRequirement(MakeGatedSkill()));
  EXPECT_TRUE(c.LearnSkill(MakeGatedSkill()));
  EXPECT_EQ(c.skill_level(MakeGatedSkill()), 1);
}

// Most skills demand nothing, and must not be held up by the check.
TEST_F(CharacterTest, ASkillDemandingNothingIsAlwaysOpen) {
  CharacterInstance c = MakeSpearman(rng_, 20);
  EXPECT_TRUE(c.MeetsSkillRequirement(MakeGateSkill()));
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

TEST_F(AllocateStatTest, SpendsTheApAndRaisesTheStat) {
  CharacterInstance c = MakeCharacter(rng_, /*level=*/1, /*ap=*/10);
  EXPECT_TRUE(c.AllocateStat(STAT_FIELD_STR));
  EXPECT_EQ(c.proto().allocated_stats().str(), 1) << "one by default";
  EXPECT_EQ(c.proto().ap(), 9);
  EXPECT_TRUE(c.AllocateStat(STAT_FIELD_LUK, 7));
  EXPECT_EQ(c.proto().allocated_stats().luk(), 7);
  EXPECT_EQ(c.proto().ap(), 2);
}

// A refused call spends nothing, so a player who asks for more than they have
// is left where they were rather than part-way.
TEST_F(AllocateStatTest, RefusesWhatItCannotPayForInFull) {
  CharacterInstance c = MakeCharacter(rng_, /*level=*/1, /*ap=*/2);
  EXPECT_FALSE(c.AllocateStat(STAT_FIELD_STR, 3));
  EXPECT_EQ(c.proto().allocated_stats().str(), 0);
  EXPECT_FALSE(c.AllocateStat(STAT_FIELD_UNSPECIFIED));
  EXPECT_EQ(c.proto().ap(), 2);
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
  // Each book draws on its own stage's points, and a Spearman holds two.
  Character proto;
  proto.set_job(JOB_SPEARMAN);
  proto.set_job_stage(2);
  (*proto.mutable_sp_by_stage())[1] = 5;
  (*proto.mutable_sp_by_stage())[2] = 5;
  CharacterInstance c(rng_, std::move(proto));

  Skill first =
      MakeSkill("Slash Blast", JOB_ADVANCEMENT_SWORDMAN, /*max_level=*/20);
  EXPECT_TRUE(c.LearnSkill(first, 5));
  EXPECT_EQ(c.sp(1), 0);
  EXPECT_EQ(c.sp(2), 5);  // stage 2 untouched

  Skill second =
      MakeSkill("Spear Sweep", JOB_ADVANCEMENT_SPEARMAN, /*max_level=*/20);
  EXPECT_TRUE(c.LearnSkill(second, 5));
  EXPECT_EQ(c.sp(2), 0);
}

// Every first job's skills sit at stage 1, so the stage alone does not say
// whose book a skill is from. Without the check, a Swordman's points would
// buy an Archer's skills.
TEST_F(LearnSkillTest, RejectsAnotherJobsBook) {
  CharacterInstance c = MakeCharacterWithSp(rng_, /*stage=*/1, /*sp=*/5);
  Skill skill =
      MakeSkill("Arrow Blow", JOB_ADVANCEMENT_ARCHER, /*max_level=*/20);
  EXPECT_FALSE(c.HasAdvancement(JOB_ADVANCEMENT_ARCHER));
  EXPECT_FALSE(c.LearnSkill(skill));
  EXPECT_EQ(c.sp(1), 5);
}

// The second book is not open to a character who has not taken the second
// advancement, however many points they are holding.
TEST_F(LearnSkillTest, RejectsABookFromAnAdvancementNotYetTaken) {
  Character proto;
  proto.set_job(JOB_SWORDMAN);
  proto.set_job_stage(1);
  (*proto.mutable_sp_by_stage())[2] = 5;  // points they cannot have earned yet
  CharacterInstance c(rng_, std::move(proto));
  Skill skill =
      MakeSkill("Spear Sweep", JOB_ADVANCEMENT_SPEARMAN, /*max_level=*/20);
  EXPECT_FALSE(c.HasAdvancement(JOB_ADVANCEMENT_SPEARMAN));
  EXPECT_FALSE(c.LearnSkill(skill));
}

TEST_F(LearnSkillTest, ASpearmanStillHoldsTheirSwordmanBook) {
  CharacterInstance c =
      MakeCharacterWithSp(rng_, /*stage=*/2, /*sp=*/5, JOB_SPEARMAN);
  EXPECT_TRUE(c.HasAdvancement(JOB_ADVANCEMENT_SWORDMAN));
  EXPECT_TRUE(c.HasAdvancement(JOB_ADVANCEMENT_SPEARMAN));
  EXPECT_FALSE(c.HasAdvancement(JOB_ADVANCEMENT_ROGUE));
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

TEST_F(CanEquipTest, AWarriorWearsAWarriorSwordOfTheirLevel) {
  sword_.set_required_level(1);
  sword_.add_equip_job_categories(EQUIP_JOB_CATEGORY_WARRIOR);
  c_.AdvanceJob(JOB_SWORDMAN);
  EXPECT_TRUE(c_.CanEquip(sword_));
  sword_.set_required_level(10);
  EXPECT_FALSE(c_.CanEquip(sword_)) << "a level 1 in a level 10 sword";
}

// The category has to name the job, and an item naming nobody is worn by
// nobody -- CanEquip reads empty categories as "no job", where MeetsJob reads
// them as "any job".
TEST_F(CanEquipTest, TheCategoryHasToNameTheJob) {
  sword_.set_required_level(1);
  c_.AdvanceJob(JOB_SWORDMAN);
  EXPECT_FALSE(c_.CanEquip(sword_)) << "no category at all";
  sword_.add_equip_job_categories(EQUIP_JOB_CATEGORY_BOWMAN);
  EXPECT_FALSE(c_.CanEquip(sword_));
  sword_.add_equip_job_categories(EQUIP_JOB_CATEGORY_UNIVERSAL);
  EXPECT_TRUE(c_.CanEquip(sword_));
}

// A beginner is a job like any other here, and a character who has not advanced
// at all is no job, so nothing fits them.
TEST_F(CanEquipTest, ABeginnerWearsWhatNamesThemAndNothingElse) {
  sword_.set_required_level(1);
  sword_.add_equip_job_categories(EQUIP_JOB_CATEGORY_WARRIOR);
  EXPECT_FALSE(c_.CanEquip(sword_)) << "job unspecified";
  c_.AdvanceJob(JOB_BEGINNER);
  EXPECT_FALSE(c_.CanEquip(sword_));
  sword_.clear_equip_job_categories();
  sword_.add_equip_job_categories(EQUIP_JOB_CATEGORY_BEGINNER);
  EXPECT_TRUE(c_.CanEquip(sword_));
  sword_.clear_equip_job_categories();
  sword_.add_equip_job_categories(EQUIP_JOB_CATEGORY_UNIVERSAL);
  EXPECT_TRUE(c_.CanEquip(sword_));
}

// --- MeetsLevel ---

TEST_F(MeetsLevelTest, PassesAtTheLevelAskedForAndAbove) {
  EXPECT_TRUE(c_.MeetsLevel(sword_)) << "no level asked for";
  sword_.set_required_level(1);
  EXPECT_TRUE(c_.MeetsLevel(sword_));
  sword_.set_required_level(10);
  EXPECT_FALSE(c_.MeetsLevel(sword_));
  CharacterInstance c = MakeCharacter(rng_, /*level=*/10);
  EXPECT_TRUE(c.MeetsLevel(sword_));
}

// --- MeetsJob ---

// Empty categories mean any job at all, unlike CanEquip: MeetsJob answers only
// the question it is asked, and an item with no demand makes none.
TEST_F(MeetsJobTest, PassesOnAMatchOrOnNoDemandAtAll) {
  c_.AdvanceJob(JOB_SWORDMAN);
  EXPECT_TRUE(c_.MeetsJob(sword_)) << "no categories";
  sword_.add_equip_job_categories(EQUIP_JOB_CATEGORY_BOWMAN);
  EXPECT_FALSE(c_.MeetsJob(sword_));
  sword_.add_equip_job_categories(EQUIP_JOB_CATEGORY_WARRIOR);
  EXPECT_TRUE(c_.MeetsJob(sword_));
  sword_.clear_equip_job_categories();
  sword_.add_equip_job_categories(EQUIP_JOB_CATEGORY_UNIVERSAL);
  EXPECT_TRUE(c_.MeetsJob(sword_));
}

// An unadvanced character matches nothing that names a job.
TEST_F(MeetsJobTest, FalseWhenJobUnspecified) {
  sword_.add_equip_job_categories(EQUIP_JOB_CATEGORY_WARRIOR);
  EXPECT_FALSE(c_.MeetsJob(sword_));
}

// An off-hand answers to its own branch of the category, not the category. All
// three of the warrior's are EQUIP_JOB_CATEGORY_WARRIOR and carry the same
// stats, so the branch is the only thing keeping them apart.
TEST_F(MeetsJobTest, ASecondaryAsksForTheBranchThatCarriesIt) {
  EquipPrototype rosary;
  rosary.set_equip_slot(EQUIP_SLOT_SECONDARY);
  rosary.set_equip_type(EQUIP_TYPE_ROSARY);
  rosary.add_equip_job_categories(EQUIP_JOB_CATEGORY_WARRIOR);
  c_.AdvanceJob(JOB_SWORDMAN);
  EXPECT_FALSE(c_.MeetsJob(rosary)) << "a 1st job has no off-hand yet";
  c_.AdvanceJob(JOB_FIGHTER);
  EXPECT_FALSE(c_.MeetsJob(rosary)) << "a Fighter is holding a Page's rosary";
  CharacterInstance page = MakeCharacter(rng_);
  page.AdvanceJob(JOB_SWORDMAN);
  page.AdvanceJob(JOB_PAGE);
  EXPECT_TRUE(page.MeetsJob(rosary));
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

// --- BuyWithToken ---

class BuyWithTokenTest : public CharacterTest {
 protected:
  void SetUp() override {
    polearm_.set_name("Frozen Polearm");
    polearm_.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
    polearm_.set_token_item("frozen_weapon_token");
    polearm_.set_token_price(1);
    token_.set_name("Frozen Weapon Token");
    token_.set_category(ITEM_CATEGORY_ETC);
    token_.set_currency_mark("●");
    c_.AddStackable(token_, 2);
  }

  int TokensLeft() {
    return c_.CountStackable(token_);
  }

  EquipPrototype polearm_;
  ItemPrototype token_;
  CharacterInstance c_ = MakeCharacter(rng_);
};

TEST_F(BuyWithTokenTest, TakesTheTokenAndGivesTheItem) {
  EXPECT_TRUE(c_.BuyWithToken(polearm_, token_, 1));
  EXPECT_EQ(TokensLeft(), 1);
  ASSERT_EQ(c_.inventory().size(), 1);
  EXPECT_EQ(c_.inventory()[0].name(), "Frozen Polearm");
}

// The whole order or none of it, as the meso shelf does it: a player left
// short a token would have paid for something they never received.
TEST_F(BuyWithTokenTest, BuysNothingWhenItCannotBuyEverything) {
  EXPECT_FALSE(c_.BuyWithToken(polearm_, token_, 3));
  EXPECT_EQ(TokensLeft(), 2);
  EXPECT_EQ(c_.inventory().size(), 0);

  EXPECT_TRUE(c_.BuyWithToken(polearm_, token_, 2));
  EXPECT_EQ(TokensLeft(), 0);
  EXPECT_EQ(c_.inventory().size(), 2) << "each copy is its own item";
}

TEST_F(BuyWithTokenTest, RefusesWhatNoTokenBuys) {
  EquipPrototype meso_item;
  meso_item.set_name("Zedbug");
  meso_item.set_shop_price(400000);
  EXPECT_FALSE(c_.BuyWithToken(meso_item, token_, 1));
  EXPECT_FALSE(c_.BuyWithToken(polearm_, token_, 0));
  EXPECT_FALSE(c_.BuyWithToken(polearm_, token_, -1));
  EXPECT_EQ(TokensLeft(), 2);
  EXPECT_EQ(c_.inventory().size(), 0);
}

// An Etc item is not a currency for having been passed as one. The mark is
// what says the shop asks prices in it.
TEST_F(BuyWithTokenTest, RefusesAnItemThatIsNotACurrency) {
  ItemPrototype horn;
  horn.set_name("Beetle's Horn");
  horn.set_category(ITEM_CATEGORY_ETC);
  c_.AddStackable(horn, 50);

  EXPECT_FALSE(c_.BuyWithToken(polearm_, horn, 1));
  EXPECT_EQ(c_.CountStackable(horn), 50);
  EXPECT_EQ(c_.inventory().size(), 0);
}

// --- Buy, stackable ---

class BuyStackableTest : public CharacterTest {
 protected:
  void SetUp() override {
    trace_.set_name("Spell Trace");
    trace_.set_category(ITEM_CATEGORY_ETC);
    trace_.set_max_stack(30000);
    trace_.set_shop_price(5000);
    c_.AddMeso(50000);
  }

  ItemPrototype trace_;
  CharacterInstance c_ = MakeCharacter(rng_);
};

// Ten of a stackable is one row of ten, not ten rows -- the opposite of what
// buying ten swords does.
TEST_F(BuyStackableTest, TakesTheMesoAndStacksTheCopies) {
  EXPECT_TRUE(c_.Buy(trace_, 10));
  EXPECT_EQ(c_.meso(), 0);
  ASSERT_EQ(c_.stackables(ITEM_CATEGORY_ETC).size(), 1);
  EXPECT_EQ(c_.stackables(ITEM_CATEGORY_ETC)[0].count(), 10);
  EXPECT_EQ(c_.stackables(ITEM_CATEGORY_ETC)[0].name(), "Spell Trace");
}

TEST_F(BuyStackableTest, BuysNothingWhenItCannotBuyEverything) {
  EXPECT_FALSE(c_.Buy(trace_, 11));
  EXPECT_EQ(c_.meso(), 50000);
  EXPECT_TRUE(c_.stackables(ITEM_CATEGORY_ETC).empty());
}

TEST_F(BuyStackableTest, WillNotSellWhatTheShopDoesNotStock) {
  ItemPrototype unpriced;
  unpriced.set_name("Snail Shell");
  unpriced.set_category(ITEM_CATEGORY_ETC);
  EXPECT_FALSE(c_.Buy(unpriced, 1));
  EXPECT_EQ(c_.meso(), 50000);
  EXPECT_TRUE(c_.stackables(ITEM_CATEGORY_ETC).empty());
}

TEST_F(BuyStackableTest, NonPositiveCountIsNoOp) {
  EXPECT_FALSE(c_.Buy(trace_, 0));
  EXPECT_FALSE(c_.Buy(trace_, -2));
  EXPECT_EQ(c_.meso(), 50000);
}

// The bag's room is checked before the meso, so an order too big for it takes
// nothing -- the same promise the equip shelf makes.
TEST_F(BuyStackableTest, BuysNothingWhenTheBagCannotHoldItAll) {
  // Far past what a full bag of traces costs, so the bag is what refuses and
  // not the purse.
  c_.AddMeso(1000000000000LL);
  int room = c_.RoomFor(trace_);
  EXPECT_FALSE(c_.Buy(trace_, room + 1));
  EXPECT_TRUE(c_.stackables(ITEM_CATEGORY_ETC).empty());
  EXPECT_TRUE(c_.Buy(trace_, room));
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

// --- SellEquip ---

// Fixture providing a sword worth 900 meso and a worthless one, so the two
// halves of "zero is not a refusal here" can be told apart.
class SellEquipTest : public CharacterEquipFixture {
 protected:
  void SetUp() override {
    CharacterEquipFixture::SetUp();
    sword_.set_sell_price(900);
    starter_.set_name("Starter Sword");
    starter_.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);  // sell_price 0
  }
  EquipPrototype starter_;
};

TEST_F(SellEquipTest, SellsItemAndCreditsMeso) {
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  EXPECT_EQ(c_.SellEquip(0), 900);
  EXPECT_EQ(c_.inventory().size(), 0);
  EXPECT_EQ(c_.meso(), 900);
}

// The difference from a stackable, and the reason the starter sword can leave
// the bag at all: a price of zero sells, it does not refuse.
TEST_F(SellEquipTest, WorthlessItemStillGoes) {
  c_.PickUp(std::make_unique<EquipInstance>(starter_));
  EXPECT_EQ(c_.SellEquip(0), 0);
  EXPECT_EQ(c_.inventory().size(), 0);
  EXPECT_EQ(c_.meso(), 0);
}

// Scrolls and stars are poured in, never poured back out. Selling a scrolled
// item for more than the base one would make spell traces -- which cost meso
// -- a way of printing it.
TEST_F(SellEquipTest, UpgradesAddNothingToWhatItPays) {
  Equip upgraded;
  upgraded.set_equip_name(sword_.name());
  upgraded.set_stars(12);
  upgraded.set_scroll_successes(7);
  upgraded.mutable_scroll_stats()->set_attack(35);
  c_.PickUp(std::make_unique<EquipInstance>(sword_, upgraded));
  EXPECT_EQ(c_.SellEquip(0), 900);
  EXPECT_EQ(c_.meso(), 900);
}

// A trace is the record of a destroyed item, not a copy of it, so it pays what
// the record is worth however dear the item behind it was.
TEST_F(SellEquipTest, TracePaysNothing) {
  c_.PickUp(std::make_unique<EquipTrace>(sword_, Equip()));
  EXPECT_EQ(c_.SellEquip(0), 0);
  EXPECT_EQ(c_.inventory().size(), 0);
  EXPECT_EQ(c_.meso(), 0);
}

TEST_F(SellEquipTest, SellsTheItemAtTheIndexAsked) {
  c_.PickUp(std::make_unique<EquipInstance>(starter_));
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  EXPECT_EQ(c_.SellEquip(1), 900);
  ASSERT_EQ(c_.inventory().size(), 1);
  EXPECT_EQ(c_.inventory()[0].name(), "Starter Sword");
}

TEST_F(SellEquipTest, OutOfRangeIndexIsNoOp) {
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  EXPECT_EQ(c_.SellEquip(1), 0);
  EXPECT_EQ(c_.SellEquip(-1), 0);
  EXPECT_EQ(c_.inventory().size(), 1);
  EXPECT_EQ(c_.meso(), 0);
}

// --- BuyBack ---

// The shelf hands items back, so it needs the catalogs a save needs, keyed by
// data-file stem rather than display name for the same reason.
class BuyBackTest : public CharacterTest {
 protected:
  void SetUp() override {
    sword_.set_name("Sword");
    sword_.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
    sword_.set_upgrade_slots(7);
    sword_.set_sell_price(900);
    equips_["sword"] = sword_;
    shell_.set_name("Green Snail Shell");
    shell_.set_category(ITEM_CATEGORY_ETC);
    shell_.set_sell_price(7);
    items_["green_snail_shell"] = shell_;
  }
  bool BuyBack(int index, int count = 1) {
    return c_.BuyBack(index, count, equips_, items_);
  }
  CharacterInstance c_ = MakeCharacter(rng_);
  EquipPrototype sword_;
  ItemPrototype shell_;
  std::map<std::string, EquipPrototype> equips_;
  std::map<std::string, ItemPrototype> items_;
};

TEST_F(BuyBackTest, ASoldEquipLandsOnTheShelfAtWhatItPaid) {
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  c_.SellEquip(0);
  ASSERT_EQ(c_.buy_backs().size(), 1);
  EXPECT_TRUE(c_.buy_backs().Get(0).has_equip());
  EXPECT_EQ(c_.buy_backs().Get(0).equip().equip_name(), "Sword");
  EXPECT_EQ(c_.buy_backs().Get(0).unit_price(), 900);
}

TEST_F(BuyBackTest, ASoldStackLandsOnTheShelfAtItsUnitPrice) {
  c_.AddStackable(shell_, 10);
  c_.SellStackable(ITEM_CATEGORY_ETC, 0, 4);
  ASSERT_EQ(c_.buy_backs().size(), 1);
  EXPECT_TRUE(c_.buy_backs().Get(0).has_stack());
  EXPECT_EQ(c_.buy_backs().Get(0).stack().name(), "Green Snail Shell");
  EXPECT_EQ(c_.buy_backs().Get(0).stack().count(), 4);
  EXPECT_EQ(c_.buy_backs().Get(0).unit_price(), 7);
}

// The point of the whole shelf: the price never paid for the stars, so buying
// the item back must not have to buy them again.
TEST_F(BuyBackTest, AnEquipComesBackAsTheItemThatLeft) {
  Equip upgraded;
  upgraded.set_equip_name("Sword");
  upgraded.set_stars(12);
  upgraded.set_scroll_successes(7);
  upgraded.set_remaining_upgrade_slots(0);
  upgraded.mutable_scroll_stats()->set_attack(35);
  c_.PickUp(std::make_unique<EquipInstance>(sword_, upgraded));
  c_.SellEquip(0);
  ASSERT_EQ(c_.meso(), 900);

  ASSERT_TRUE(BuyBack(0));
  EXPECT_EQ(c_.meso(), 0) << "bought back at exactly what it sold for";
  EXPECT_TRUE(c_.buy_backs().empty());
  ASSERT_EQ(c_.inventory().size(), 1);
  const EquipInstance* item = c_.inventory().equip_instance(0);
  ASSERT_NE(item, nullptr);
  EXPECT_EQ(item->stars(), 12);
  EXPECT_EQ(item->equip_state().scroll_stats().attack(), 35);
  EXPECT_EQ(item->equip_state().remaining_upgrade_slots(), 0);
}

// A trace is worth nothing both ways, and has to come back a trace -- coming
// back alive would undo a star force boom for free.
TEST_F(BuyBackTest, ATraceComesBackATraceForNothing) {
  Equip destroyed;
  destroyed.set_equip_name("Sword");
  destroyed.set_stars(19);
  c_.PickUp(std::make_unique<EquipTrace>(sword_, destroyed));
  c_.SellEquip(0);
  ASSERT_EQ(c_.buy_backs().Get(0).unit_price(), 0);

  ASSERT_TRUE(BuyBack(0));
  EXPECT_EQ(c_.meso(), 0);
  ASSERT_EQ(c_.inventory().size(), 1);
  EXPECT_EQ(c_.inventory().equip_instance(0), nullptr) << "came back alive";
}

TEST_F(BuyBackTest, PartOfAStackLeavesTheRestWhereItWas) {
  c_.AddStackable(shell_, 100);
  c_.SellStackable(ITEM_CATEGORY_ETC, 0, 100);
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  c_.SellEquip(0);  // a newer row, so the shelf has an order to keep
  ASSERT_EQ(c_.buy_backs().size(), 2);

  ASSERT_TRUE(BuyBack(1, 30));
  EXPECT_EQ(c_.CountStackable(shell_), 30);
  ASSERT_EQ(c_.buy_backs().size(), 2) << "the row stays until it empties";
  EXPECT_EQ(c_.buy_backs().Get(1).stack().count(), 70);
  EXPECT_TRUE(c_.buy_backs().Get(0).has_equip()) << "and stays where it was";

  ASSERT_TRUE(BuyBack(1, 70));
  EXPECT_EQ(c_.CountStackable(shell_), 100);
  EXPECT_EQ(c_.buy_backs().size(), 1);
}

// One row per sale, not per item: two sales of the same thing are two rows,
// and the newer of them is the one on top.
TEST_F(BuyBackTest, EachSaleIsItsOwnRowNewestFirst) {
  c_.AddStackable(shell_, 300);
  c_.SellStackable(ITEM_CATEGORY_ETC, 0, 200);
  c_.SellStackable(ITEM_CATEGORY_ETC, 0, 100);
  ASSERT_EQ(c_.buy_backs().size(), 2);
  EXPECT_EQ(c_.buy_backs().Get(0).stack().count(), 100)
      << "the later sale on top";
  EXPECT_EQ(c_.buy_backs().Get(1).stack().count(), 200);
}

TEST_F(BuyBackTest, TheOldestRowFallsOffAFullShelf) {
  c_.AddStackable(shell_, 1000);
  for (int i = 0; i < kBuyBackSlots + 3; ++i) {
    c_.AddStackable(shell_, i + 1);
    c_.SellStackable(ITEM_CATEGORY_ETC, 0, i + 1);
  }
  ASSERT_EQ(c_.buy_backs().size(), kBuyBackSlots);
  // The last sale on top, and the three oldest gone from the bottom.
  EXPECT_EQ(c_.buy_backs().Get(0).stack().count(), kBuyBackSlots + 3);
  EXPECT_EQ(c_.buy_backs().Get(kBuyBackSlots - 1).stack().count(), 4);
}

TEST_F(BuyBackTest, RefusesWhatTheCharacterCannotPayFor) {
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  c_.SellEquip(0);
  // Spent since, which is the case the shelf outlives: the row is still there
  // and the money for it is not.
  shell_.set_shop_price(1);
  ASSERT_TRUE(c_.Buy(shell_, 900));
  ASSERT_EQ(c_.meso(), 0);

  EXPECT_FALSE(BuyBack(0));
  EXPECT_EQ(c_.meso(), 0);
  EXPECT_EQ(c_.inventory().size(), 0);
  EXPECT_EQ(c_.buy_backs().size(), 1) << "still there to come back for";
}

TEST_F(BuyBackTest, RefusesAStackTheBagHasNoRoomFor) {
  c_.AddStackable(shell_, 10);
  c_.SellStackable(ITEM_CATEGORY_ETC, 0, 10);
  c_.AddMeso(1000);
  int64_t meso = c_.meso();
  // Every Etc slot filled with something else, so topping up cannot help.
  ItemPrototype filler;
  filler.set_category(ITEM_CATEGORY_ETC);
  for (int i = 0; i < kTabCapacity; ++i) {
    filler.set_name("Filler" + std::to_string(i));
    c_.AddStackable(filler, 1);
  }
  ASSERT_EQ(c_.RoomFor(shell_), 0);

  EXPECT_FALSE(BuyBack(0, 10));
  EXPECT_EQ(c_.meso(), meso);
  EXPECT_EQ(c_.buy_backs().size(), 1);
}

TEST_F(BuyBackTest, RefusesAnItemTheCatalogNoLongerHas) {
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  c_.SellEquip(0);
  equips_.clear();

  EXPECT_FALSE(BuyBack(0));
  EXPECT_EQ(c_.meso(), 900) << "not charged for what it cannot hand over";
  EXPECT_EQ(c_.buy_backs().size(), 1);
}

TEST_F(BuyBackTest, OutOfRangeIndexIsNoOp) {
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  c_.SellEquip(0);
  EXPECT_FALSE(BuyBack(1));
  EXPECT_FALSE(BuyBack(-1));
  EXPECT_EQ(c_.buy_backs().size(), 1);
  EXPECT_EQ(c_.meso(), 900);
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

// Armour is four more slots, not a fifth weapon: each piece lands in its own
// and none of them displaces another.
TEST_F(EquipTest, ArmourWearsFourPiecesAtOnce) {
  const EquipSlot slots[] = {EQUIP_SLOT_HAT, EQUIP_SLOT_TOP, EQUIP_SLOT_BOTTOM,
                             EQUIP_SLOT_CAPE};
  for (EquipSlot slot : slots) {
    EquipPrototype piece;
    piece.set_name("Frozen " + std::to_string(static_cast<int>(slot)));
    piece.set_equip_slot(slot);
    piece.mutable_base_stats()->set_str(10);
    c_.PickUp(std::make_unique<EquipInstance>(piece));
    ASSERT_TRUE(c_.Equip(0)) << "slot " << static_cast<int>(slot);
  }
  EXPECT_EQ(c_.equipped().size(), 4);
  EXPECT_EQ(c_.inventory().size(), 0);
  EXPECT_EQ(c_.equip_stats().str(), 40) << "every piece counts";
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

// --- StarForce prices ---

// GMS charges for the roll and not for the star, which is the whole reason
// the top of the ladder is out of reach. Both entry points take the price, and
// neither rolls without it.
class StarForcePriceTest : public CharacterTest {
 protected:
  EquipPrototype Sword() {
    EquipPrototype proto;
    proto.set_name("Sword");
    proto.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
    proto.set_required_level(138);
    return proto;
  }
  int64_t FirstStar() {
    return StarForceCost(138, 0);
  }
};

TEST_F(StarForcePriceTest, AnAttemptTakesItsPrice) {
  CharacterInstance c = MakeCharacter(rng_);
  c.AddMeso(4 * FirstStar());
  c.PickUp(std::make_unique<EquipInstance>(Sword()));
  EXPECT_NE(c.StarForceInventory(0), kStarForceNoMeso);
  EXPECT_EQ(c.meso(), 3 * FirstStar());
  c.Equip(0);
  EXPECT_NE(c.StarForceEquipped(EQUIP_SLOT_PRIMARY_WEAPON), kStarForceNoMeso);
  EXPECT_LT(c.meso(), 3 * FirstStar()) << "the equipped path is free";
}

TEST_F(StarForcePriceTest, AnAttemptItCannotAffordNeverHappens) {
  CharacterInstance c = MakeCharacter(rng_);
  c.AddMeso(FirstStar() - 1);
  c.PickUp(std::make_unique<EquipInstance>(Sword()));
  EXPECT_EQ(c.StarForceInventory(0), kStarForceNoMeso);
  EXPECT_EQ(c.meso(), FirstStar() - 1) << "a refused attempt still charged";
  EXPECT_EQ(c.inventory()[0].stars(), 0) << "a refused attempt still rolled";
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
  CharacterInstance c = MakeRichCharacter(rng_);
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
  CharacterInstance c = MakeRichCharacter(rng_);
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
  CharacterInstance c = MakeRichCharacter(rng_);
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
  CharacterInstance c = MakeRichCharacter(rng_);
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
  CharacterInstance c = MakeRichCharacter(rng_);
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

// --- Projectiles ---

// A 10-attack weapon and a 15-attack projectile, so 25 means the ammunition
// counted and 10 means it did not.
class ProjectileTest : public CharacterTest {
 protected:
  EquipPrototype Weapon(EquipType type) {
    EquipPrototype proto;
    proto.set_name("Weapon");
    proto.set_equip_type(type);
    proto.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
    proto.mutable_base_stats()->set_attack(10);
    return proto;
  }
  EquipPrototype Ammo(EquipType type) {
    EquipPrototype proto;
    proto.set_name("Ammo");
    proto.set_equip_type(type);
    proto.set_equip_slot(EQUIP_SLOT_PROJECTILE);
    proto.mutable_base_stats()->set_attack(15);
    return proto;
  }
  // A fresh character holding both, since what is under test is the pairing
  // and not what one character does over time.
  int AttackWith(EquipType weapon, EquipType ammo) {
    CharacterInstance c = MakeCharacter(rng_);
    c.PickUp(std::make_unique<EquipInstance>(Weapon(weapon)));
    c.PickUp(std::make_unique<EquipInstance>(Ammo(ammo)));
    EXPECT_TRUE(c.Equip(0));
    EXPECT_TRUE(c.Equip(0));
    return c.equip_stats().attack();
  }
  CharacterInstance c_ = MakeCharacter(rng_);
};

TEST_F(ProjectileTest, CountsOnlyForTheWeaponThatDrawsIt) {
  EXPECT_EQ(AttackWith(EQUIP_TYPE_CLAW, EQUIP_TYPE_THROWING_STAR), 25);
  EXPECT_EQ(AttackWith(EQUIP_TYPE_BOW, EQUIP_TYPE_ARROW_FOR_BOW), 25);
  EXPECT_EQ(AttackWith(EQUIP_TYPE_CROSSBOW, EQUIP_TYPE_ARROW_FOR_CROSSBOW), 25);

  // Worn all the same -- a thief may carry stars while holding a dagger -- but
  // nothing in hand draws them, so their attack does not count. The two arrows
  // are as unrelated to each other as a star is to either.
  EXPECT_EQ(AttackWith(EQUIP_TYPE_DAGGER, EQUIP_TYPE_THROWING_STAR), 10);
  EXPECT_EQ(AttackWith(EQUIP_TYPE_BOW, EQUIP_TYPE_ARROW_FOR_CROSSBOW), 10);
  EXPECT_EQ(AttackWith(EQUIP_TYPE_CROSSBOW, EQUIP_TYPE_ARROW_FOR_BOW), 10);
}

TEST_F(ProjectileTest, StopsCountingWhenTheWeaponComesOff) {
  c_.PickUp(std::make_unique<EquipInstance>(Weapon(EQUIP_TYPE_CLAW)));
  c_.PickUp(std::make_unique<EquipInstance>(Ammo(EQUIP_TYPE_THROWING_STAR)));
  ASSERT_TRUE(c_.Equip(0));
  ASSERT_TRUE(c_.Equip(0));
  ASSERT_EQ(c_.equip_stats().attack(), 25);

  // Swapping the claw for a dagger has to re-evaluate the stars, not just
  // subtract the claw.
  c_.PickUp(std::make_unique<EquipInstance>(Weapon(EQUIP_TYPE_DAGGER)));
  ASSERT_TRUE(c_.Equip(0));
  EXPECT_EQ(c_.equip_stats().attack(), 10);
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

// --- CountOwned ---

// Worn is still owned. A player looking at the shop's second Sword has one
// already, whether it is in the bag or on their back.
TEST_F(CapacityTest, CountOwnedCountsEveryCopyBagAndBack) {
  EXPECT_EQ(c_.CountOwned(sword_), 0) << "never picked one up";
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  EXPECT_EQ(c_.CountOwned(sword_), 2);
  c_.Equip(0);
  ASSERT_EQ(c_.inventory().size(), 1) << "moved out of the bag, not copied";
  EXPECT_EQ(c_.CountOwned(sword_), 2);
}

TEST_F(CapacityTest, CountOwnedIgnoresOtherItems) {
  EquipPrototype axe;
  axe.set_name("Axe");
  axe.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  c_.PickUp(std::make_unique<EquipInstance>(axe));
  EXPECT_EQ(c_.CountOwned(sword_), 0);
}

// A trace is the record of an item that was destroyed, not a copy of it.
// Somebody deciding whether to buy another has none of the thing itself.
//
// This passes whichever of the two guards is doing the work -- the nullptr
// filter, or the suffix on EquipTrace's display name -- so it pins the
// behaviour rather than the implementation. Removing both is what breaks it.
TEST_F(CapacityTest, CountOwnedDoesNotCountTraces) {
  c_.PickUp(std::make_unique<EquipTrace>(sword_, Equip()));
  ASSERT_EQ(c_.inventory().size(), 1) << "it is in the bag, taking a slot";
  EXPECT_EQ(c_.CountOwned(sword_), 0);
}

// --- RoomFor(ItemPrototype) ---

TEST_F(CapacityTest, RoomForAStackableOnAnEmptyTabIsEveryStack) {
  // 128 slots of 200 apiece, the Etc default.
  EXPECT_EQ(c_.RoomFor(shell_), kTabCapacity * 200);
}

// The case that motivated the rule: part-full stacks count for what is left in
// them, on top of a whole stack for every slot still free.
TEST_F(CapacityTest, RoomCountsPartStacksAndFreeSlots) {
  OpenDistinctStacks(kTabCapacity - 11);
  c_.AddStackable(shell_, 100);
  // 118 stacks open, so 10 slots free, and the shell stack has 100 spare.
  ASSERT_EQ(c_.stackables(ITEM_CATEGORY_ETC).size(), kTabCapacity - 10);
  EXPECT_EQ(c_.RoomFor(shell_), 10 * 200 + 100);
}

// Room in someone else's stack is no use.
TEST_F(CapacityTest, RoomIgnoresOtherItemsPartStacks) {
  OpenDistinctStacks(kTabCapacity - 11);
  c_.AddStackable(other_, 100);
  ASSERT_EQ(c_.stackables(ITEM_CATEGORY_ETC).size(), kTabCapacity - 10);
  EXPECT_EQ(c_.RoomFor(shell_), 10 * 200);
}

// With no slot left the only room is what the open stacks of that item can
// still take.
TEST_F(CapacityTest, RoomOnAFullTabIsTheOpenStacks) {
  OpenDistinctStacks(kTabCapacity - 1);
  c_.AddStackable(shell_, 150);
  ASSERT_EQ(c_.stackables(ITEM_CATEGORY_ETC).size(), kTabCapacity);
  EXPECT_EQ(c_.RoomFor(shell_), 50);
}

TEST_F(CapacityTest, RoomOnAFullTabOfOtherItemsIsNone) {
  OpenDistinctStacks(kTabCapacity);
  EXPECT_EQ(c_.RoomFor(shell_), 0);
}

// Use items stack far deeper than Etc ones, and the room follows the item
// rather than a fixed number.
TEST_F(CapacityTest, RoomFollowsTheItemsStackSize) {
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

// --- ToProto / RestoreFrom ---

// A save carries catalog keys, not item definitions, so a round trip needs the
// catalogs back. These stand in for what the game loads from data/.
class SaveRoundTripTest : public CharacterTest {
 protected:
  void SetUp() override {
    sword_.set_name("Sword");
    sword_.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
    sword_.set_upgrade_slots(7);
    sword_.set_required_level(138);
    // Keyed by data-file stem, the way the real catalogs are loaded, and
    // deliberately NOT the item's display name: a save names items the way the
    // player sees them, so a fixture whose key matches the name would hide a
    // lookup done against the wrong one of the two.
    equips_["sword"] = sword_;

    ItemPrototype shell;
    shell.set_name("Green Snail Shell");
    shell.set_category(ITEM_CATEGORY_ETC);
    items_["green_snail_shell"] = shell;
    ItemPrototype potion;
    potion.set_name("Red Potion");
    potion.set_category(ITEM_CATEGORY_USE);
    items_["red_potion"] = potion;
  }

  // A character rebuilt from `saved`, as a fresh launch would do it.
  CharacterInstance Reload(const Character& saved) {
    CharacterInstance loaded(rng_, Character{});
    loaded.RestoreFrom(saved, equips_, items_);
    return loaded;
  }

  EquipPrototype sword_;
  std::map<std::string, EquipPrototype> equips_;
  std::map<std::string, ItemPrototype> items_;
};

TEST_F(SaveRoundTripTest, CarriesTheCharacterSheetAcross) {
  CharacterInstance c = MakeCharacter(rng_);
  c.LevelUp();
  c.LevelUp();
  c.AdvanceJob(JOB_SWORDMAN);
  c.AllocateStat(STAT_FIELD_STR, 3);
  c.AddMeso(4321);

  CharacterInstance loaded = Reload(c.ToProto());
  EXPECT_EQ(loaded.proto().level(), c.proto().level());
  EXPECT_EQ(loaded.proto().exp(), c.proto().exp());
  EXPECT_EQ(loaded.proto().job(), JOB_SWORDMAN);
  EXPECT_EQ(loaded.proto().job_stage(), c.proto().job_stage());
  EXPECT_EQ(loaded.proto().ap(), c.proto().ap());
  EXPECT_EQ(loaded.proto().allocated_stats().str(),
            c.proto().allocated_stats().str());
  EXPECT_EQ(loaded.meso(), 4321);
}

// The equip tab is a vector of C++ objects that the proto never mirrors until
// ToProto is asked, so this is the half a save would most easily lose.
TEST_F(SaveRoundTripTest, CarriesTheEquipTabAcross) {
  CharacterInstance c = MakeCharacter(rng_);
  Equip scrolled;
  scrolled.set_equip_name("Sword");
  scrolled.set_remaining_upgrade_slots(4);
  scrolled.set_scroll_successes(3);
  scrolled.mutable_scroll_stats()->set_attack(15);
  scrolled.set_stars(6);
  c.PickUp(std::make_unique<EquipInstance>(sword_, scrolled));
  int attack_before = c.inventory().equip_instance(0)->stats().attack();
  ASSERT_GT(attack_before, 0) << "the scroll and stars have to add something";

  CharacterInstance loaded = Reload(c.ToProto());
  ASSERT_EQ(loaded.inventory().size(), 1);
  const EquipInstance* item = loaded.inventory().equip_instance(0);
  ASSERT_NE(item, nullptr);
  EXPECT_EQ(item->prototype().name(), "Sword");
  EXPECT_EQ(item->equip_state().remaining_upgrade_slots(), 4);
  EXPECT_EQ(item->equip_state().scroll_successes(), 3);
  EXPECT_EQ(item->stars(), 6);
  // The stats have to be rebuilt from the prototype plus the saved state, not
  // just the state: the base attack lives in the catalog.
  EXPECT_EQ(item->stats().attack(), attack_before);
}

// A trace and a live item differ by one flag, and only the flag decides which
// type comes back. Getting this wrong turns a destroyed item into a wearable
// one on the next launch.
//
// The flag is NOT set here. It used to be, and that was the whole of what made
// this pass: nothing in the game ever set it, so every trace saved as a live
// item and came back as one, with its stars.
TEST_F(SaveRoundTripTest, ATraceComesBackATrace) {
  CharacterInstance c = MakeCharacter(rng_);
  Equip destroyed;
  destroyed.set_equip_name("Sword");
  destroyed.set_stars(19);
  c.PickUp(std::make_unique<EquipTrace>(sword_, destroyed));

  CharacterInstance loaded = Reload(c.ToProto());
  ASSERT_EQ(loaded.inventory().size(), 1);
  EXPECT_EQ(loaded.inventory().equip_instance(0), nullptr)
      << "a trace is not an EquipInstance";
  EXPECT_EQ(loaded.traces().size(), 1u);
}

// The same thing by the road the player takes to it, since a trace built by
// hand is a trace whose flags the test chose.
TEST_F(SaveRoundTripTest, ATraceLeftByARealBoomComesBackATrace) {
  CharacterInstance c = MakeRichCharacter(rng_);
  Equip state;
  state.set_equip_name("Sword");
  state.set_stars(19);
  c.PickUp(std::make_unique<EquipInstance>(sword_, state));
  bool boomed = false;
  for (int i = 0; i < 200 && !boomed; ++i) {
    boomed = c.StarForceInventory(0) == kStarForceDestroy;
  }
  ASSERT_TRUE(boomed);
  ASSERT_EQ(c.inventory().equip_instance(0), nullptr);

  CharacterInstance loaded = Reload(c.ToProto());
  ASSERT_EQ(loaded.inventory().size(), 1);
  EXPECT_EQ(loaded.inventory().equip_instance(0), nullptr)
      << "the boom was undone by saving and loading";
}

// Recovery copies the trace's state onto the item that replaces it, so the
// flag must not ride along -- or the recovered weapon saves as another trace.
TEST_F(SaveRoundTripTest, ARecoveredItemComesBackAlive) {
  CharacterInstance c = MakeCharacter(rng_);
  Equip destroyed;
  destroyed.set_equip_name("Sword");
  destroyed.set_stars(19);
  c.PickUp(std::make_unique<EquipTrace>(sword_, destroyed));
  c.PickUp(std::make_unique<EquipInstance>(sword_));
  ASSERT_GT(c.RecoverTrace(0, 1), 0);
  ASSERT_NE(c.inventory().equip_instance(0), nullptr);

  CharacterInstance loaded = Reload(c.ToProto());
  ASSERT_EQ(loaded.inventory().size(), 1);
  EXPECT_NE(loaded.inventory().equip_instance(0), nullptr)
      << "the recovered item saved as a trace";
}

// The shelf is the shop's memory of what this character sold, so it has to
// outlive the session the sale happened in.
TEST_F(SaveRoundTripTest, CarriesTheBuyBackShelfAcross) {
  CharacterInstance c = MakeCharacter(rng_);
  Equip starred;
  starred.set_equip_name("Sword");
  starred.set_stars(9);
  c.PickUp(std::make_unique<EquipInstance>(sword_, starred));
  c.SellEquip(0);
  ItemPrototype shell = items_["green_snail_shell"];
  shell.set_sell_price(7);  // the fixture's copy is unsellable
  c.AddStackable(shell, 6);
  ASSERT_GT(c.SellStackable(ITEM_CATEGORY_ETC, 0, 6), 0);

  CharacterInstance loaded = Reload(c.ToProto());
  ASSERT_EQ(loaded.buy_backs().size(), 2);
  EXPECT_EQ(loaded.buy_backs().Get(0).stack().count(), 6) << "newest first";
  EXPECT_EQ(loaded.buy_backs().Get(1).equip().stars(), 9);
}

TEST_F(SaveRoundTripTest, CarriesWornItemsInTheirOwnSlots) {
  CharacterInstance c = MakeCharacter(rng_);
  c.PickUp(std::make_unique<EquipInstance>(sword_));
  c.Equip(0);
  ASSERT_TRUE(c.equipped().count(EQUIP_SLOT_PRIMARY_WEAPON));

  CharacterInstance loaded = Reload(c.ToProto());
  ASSERT_TRUE(loaded.equipped().count(EQUIP_SLOT_PRIMARY_WEAPON));
  EXPECT_EQ(loaded.equipped().at(EQUIP_SLOT_PRIMARY_WEAPON).prototype().name(),
            "Sword");
  EXPECT_TRUE(loaded.inventory().empty()) << "worn, not in the bag";
  // Rebuilt from what came back, not carried over: a loaded character has to
  // hit as hard as the one that was saved.
  EXPECT_EQ(loaded.equip_stats().attack(), c.equip_stats().attack());
}

TEST_F(SaveRoundTripTest, CarriesBothStackableTabsAcross) {
  CharacterInstance c = MakeCharacter(rng_);
  c.AddStackable(items_["green_snail_shell"], 47);
  c.AddStackable(items_["red_potion"], 3);

  CharacterInstance loaded = Reload(c.ToProto());
  ASSERT_EQ(loaded.stackables(ITEM_CATEGORY_ETC).size(), 1u);
  EXPECT_EQ(loaded.stackables(ITEM_CATEGORY_ETC)[0].name(),
            "Green Snail Shell");
  EXPECT_EQ(loaded.stackables(ITEM_CATEGORY_ETC)[0].count(), 47);
  ASSERT_EQ(loaded.stackables(ITEM_CATEGORY_USE).size(), 1u);
  EXPECT_EQ(loaded.stackables(ITEM_CATEGORY_USE)[0].name(), "Red Potion");
  EXPECT_EQ(loaded.stackables(ITEM_CATEGORY_USE)[0].count(), 3);
}

// Skills are keyed by name, so they survive without the skill catalog.
TEST_F(SaveRoundTripTest, CarriesLearnedSkillsAndSp) {
  CharacterInstance c = MakeCharacter(rng_);
  Skill slash = MakeSkill("Slash Blast", JOB_ADVANCEMENT_SWORDMAN, 20);
  c.AdvanceJob(JOB_SWORDMAN);
  for (int i = 0; i < 15; ++i) {
    c.LevelUp();
  }
  ASSERT_TRUE(c.LearnSkill(slash, 2));

  CharacterInstance loaded = Reload(c.ToProto());
  EXPECT_EQ(loaded.skill_level(slash), 2);
  EXPECT_EQ(loaded.sp(1), c.sp(1));
}

// Data files outlive saves. An item deleted from data/ costs the player that
// item, and must not take the character down with it.
TEST_F(SaveRoundTripTest, DropsItemsTheCatalogsNoLongerName) {
  CharacterInstance c = MakeCharacter(rng_);
  c.PickUp(std::make_unique<EquipInstance>(sword_));
  c.AddStackable(items_["green_snail_shell"], 5);
  c.AddMeso(99);
  Character saved = c.ToProto();

  equips_.clear();
  items_.clear();
  CharacterInstance loaded = Reload(saved);
  EXPECT_TRUE(loaded.inventory().empty());
  EXPECT_TRUE(loaded.stackables(ITEM_CATEGORY_ETC).empty());
  EXPECT_EQ(loaded.meso(), 99) << "the character survives its lost items";
}

// Restoring replaces rather than merges: loading over a character mid-session
// must not leave that character's items behind in the bag.
TEST_F(SaveRoundTripTest, ReplacesWhateverWasThereBefore) {
  CharacterInstance c = MakeCharacter(rng_);
  c.PickUp(std::make_unique<EquipInstance>(sword_));
  c.AddStackable(items_["red_potion"], 9);

  c.RestoreFrom(Character{}, equips_, items_);
  EXPECT_TRUE(c.inventory().empty());
  EXPECT_TRUE(c.stackables(ITEM_CATEGORY_USE).empty());
}

// Taking something off has to empty the slot in the NEXT save too. A proto map
// overwrites by key, so a stale worn item can never be a duplicate -- it can
// only be one that was never cleared, which is the harder bug to see.
TEST_F(SaveRoundTripTest, AnEmptiedSlotIsNotSaved) {
  CharacterInstance c = MakeCharacter(rng_);
  c.PickUp(std::make_unique<EquipInstance>(sword_));
  c.Equip(0);
  CharacterInstance loaded = Reload(c.ToProto());
  ASSERT_EQ(loaded.ToProto().equipped_size(), 1);

  ASSERT_TRUE(loaded.Unequip(EQUIP_SLOT_PRIMARY_WEAPON));
  EXPECT_EQ(loaded.ToProto().equipped_size(), 0);
  EXPECT_EQ(loaded.ToProto().inventory().equip_tab_size(), 1)
      << "back in the bag";
}

// The round trip has to be idempotent: what a loaded character saves must be
// what it was loaded from. Catches an entry duplicated, dropped or left stale
// on either side, which comparing one direction alone would not.
TEST_F(SaveRoundTripTest, ReSavingALoadedCharacterGivesTheSameSave) {
  CharacterInstance c = MakeCharacter(rng_);
  c.PickUp(std::make_unique<EquipInstance>(sword_));
  c.PickUp(std::make_unique<EquipInstance>(sword_));
  c.Equip(0);
  c.AddStackable(items_["green_snail_shell"], 12);
  c.AddStackable(items_["red_potion"], 2);
  c.AddMeso(500);

  Character first = c.ToProto();
  Character second = Reload(first).ToProto();
  EXPECT_EQ(second.inventory().equip_tab_size(),
            first.inventory().equip_tab_size());
  EXPECT_EQ(second.equipped_size(), first.equipped_size());
  EXPECT_EQ(second.stacks_size(), first.stacks_size());
  EXPECT_EQ(second.meso(), first.meso());
  EXPECT_EQ(second.level(), first.level());
}

// --- seen tabs ---

class SeenTabsTest : public CharacterTest {
 protected:
  CharacterInstance c_ = MakeCharacter(rng_);
};

TEST_F(SeenTabsTest, NothingHasBeenSeenToBeginWith) {
  EXPECT_FALSE(c_.TabSeen("shop"));
}

TEST_F(SeenTabsTest, MarkingATabIsRemembered) {
  c_.MarkTabSeen("shop");
  EXPECT_TRUE(c_.TabSeen("shop"));
  EXPECT_FALSE(c_.TabSeen("skills")) << "and only that tab";
}

// Marking is idempotent. Every arrow key onto the tab runs through here, so a
// list that grew an entry each time would fill the save with duplicates.
TEST_F(SeenTabsTest, MarkingTwiceRecordsItOnce) {
  c_.MarkTabSeen("shop");
  c_.MarkTabSeen("shop");
  EXPECT_EQ(c_.proto().seen_tabs_size(), 1);
}

// The record rides the character proto, which is what the save round-trips --
// otherwise every tab would go gold again on the next launch.
TEST_F(SeenTabsTest, SurvivesARoundTripThroughTheProto) {
  c_.MarkTabSeen("skills");
  c_.MarkTabSeen("advance:2");

  CharacterInstance restored = MakeCharacter(rng_);
  restored.RestoreFrom(c_.ToProto(), {}, {});
  EXPECT_TRUE(restored.TabSeen("skills"));
  EXPECT_TRUE(restored.TabSeen("advance:2"));
  EXPECT_FALSE(restored.TabSeen("advance:1"))
      << "each advancement is its own key";
}

}  // namespace
}  // namespace ms
