#include "src/character/character.h"

#include <gtest/gtest.h>

#include <climits>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "src/character/arcane_force.h"
#include "src/character/equip_presets.h"
#include "src/character/exp_table.h"
#include "src/character/hyper_stats.h"
#include "src/character/inner_ability.h"
#include "src/character/skill_placement.h"
#include "src/character/v_matrix.h"
#include "src/item/equip_instance.h"
#include "src/item/inventory.h"
#include "src/item/item.h"
#include "src/item/potential.h"
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

// A character with more meso than farming could earn. Star force costs meso,
// and one attempt on a level 138 item costs nine figures, so a test that rolls
// until the item is destroyed has to afford every roll.
CharacterInstance MakeRichCharacter(std::mt19937& rng) {
  CharacterInstance c = MakeCharacter(rng);
  c.AddMeso(1'000'000'000'000);
  return c;
}

// Base fixture with a deterministic RNG. Every character test fixture derives
// from this, so no test needs its own std::mt19937.
class CharacterTest : public testing::Test {
 protected:
  std::mt19937 rng_{0};
};

// Fixture for LevelUp tests, with a default level-1 character.
class LevelUpTest : public CharacterTest {
 protected:
  CharacterInstance c_ = MakeCharacter(rng_);
};

class AddExpTest : public CharacterTest {};

// Fixture for AdvanceJob tests. Each test needs a different starting level and
// job stage, so each builds its own character with rng_.
class AdvanceJobTest : public CharacterTest {};

// Fixture for LearnSkill tests. Each test sets its own stage SP, so there is no
// shared character.
class LearnSkillTest : public CharacterTest {};

// A character with `sp` skill points in `stage` and nothing else. It is a
// Swordman, because points can only be spent in a book of the character's own
// job, and the stage alone doesn't say whose book that is.
CharacterInstance MakeCharacterWithSp(std::mt19937& rng, int stage, int sp,
                                      Job job = JOB_SWORDMAN) {
  Character proto;
  proto.set_job(job);
  proto.set_job_stage(stage);
  (*proto.mutable_sp_by_stage())[stage] = sp;
  return CharacterInstance(rng, std::move(proto));
}

// A minimal skill with only the fields LearnSkill reads. The advancement sets
// the SP stage; JOB_ADVANCEMENT_SWORDMAN is a 1st-job (stage 1) advancement.
Skill MakeSkill(const std::string& name, JobAdvancement advancement,
                int max_level) {
  Skill skill;
  skill.set_name(name);
  PlaceIn(skill, advancement);
  skill.set_max_level(max_level);
  return skill;
}

// The skill the LearnSkill tests spend points on.
Skill SlashBlast(int max_level = 20) {
  return MakeSkill("Slash Blast", JOB_ADVANCEMENT_SWORDMAN, max_level);
}

// Fixture for AllocateStat tests. Each test needs a different AP value, so each
// builds its own character with rng_.
class AllocateStatTest : public CharacterTest {};

// Fixture for Hyper Stat tests. Each test picks the level, since the level
// decides the points.
class HyperStatTest : public CharacterTest {};

TEST_F(HyperStatTest, PointsArriveWithTheLevel) {
  CharacterInstance below = MakeCharacter(rng_, /*level=*/139);
  EXPECT_EQ(below.hyper_stat_points(), 0);
  EXPECT_FALSE(
      below.AllocateHyperStat(HYPER_STAT_FIELD_STR, StatPreset::kFirst));

  CharacterInstance c = MakeCharacter(rng_, /*level=*/140);
  EXPECT_EQ(c.hyper_stat_points(), 3);
  EXPECT_EQ(c.hyper_stat_points_left(StatPreset::kFirst), 3);
  EXPECT_TRUE(c.AllocateHyperStat(HYPER_STAT_FIELD_STR, StatPreset::kFirst));
  EXPECT_TRUE(c.AllocateHyperStat(HYPER_STAT_FIELD_STR, StatPreset::kFirst));
  EXPECT_EQ(c.hyper_stat_level(HYPER_STAT_FIELD_STR), 2);
  EXPECT_EQ(c.hyper_stat_points_left(), 0) << "level 2 costs the other two";
  EXPECT_DOUBLE_EQ(c.hyper_stat_bonus(HYPER_STAT_FIELD_STR), 60.0);
  EXPECT_FALSE(c.AllocateHyperStat(HYPER_STAT_FIELD_DEX, StatPreset::kFirst));
}

TEST_F(HyperStatTest, RaisingSeveralLevelsIsAllOrNothing) {
  CharacterInstance c = MakeCharacter(rng_, /*level=*/150);
  EXPECT_EQ(c.hyper_stat_points(), 34);
  EXPECT_FALSE(
      c.AllocateHyperStat(HYPER_STAT_FIELD_DAMAGE, StatPreset::kFirst, 6))
      << "level 6 costs 40 altogether";
  EXPECT_EQ(c.hyper_stat_level(HYPER_STAT_FIELD_DAMAGE), 0);
  EXPECT_TRUE(
      c.AllocateHyperStat(HYPER_STAT_FIELD_DAMAGE, StatPreset::kFirst, 5));
  EXPECT_EQ(c.hyper_stat_points_left(), 9);
}

// Fixture for Inner Ability tests, whose character needs a level and some
// honor.
class InnerAbilityTest : public CharacterTest {};

TEST_F(InnerAbilityTest, BothPresetsStartOnTheDefaultLines) {
  CharacterInstance c = MakeCharacter(rng_, /*level=*/160);
  for (StatPreset preset : {StatPreset::kFirst, StatPreset::kSecond}) {
    const AbilityPreset& lines = c.ability(preset);
    EXPECT_EQ(lines.rank(), ABILITY_RANK_RARE);
    ASSERT_EQ(lines.lines_size(), kAbilityLines);
    for (const AbilityLine& line : lines.lines()) {
      EXPECT_EQ(line.type(), ABILITY_LINE_TYPE_ALL_STATS);
    }
  }
  EXPECT_EQ(c.honor(), 0);
  EXPECT_EQ(c.ability_reset_cost(), 100);
}

// A save from before Inner Ability existed loads with the default lines, even
// though RestoreFrom overwrites what the constructor set up.
TEST_F(InnerAbilityTest, RestoringAnOldSaveSeedsTheLines) {
  CharacterInstance c = MakeCharacter(rng_, /*level=*/160);
  Character old;
  old.set_level(160);
  c.RestoreFrom(old, {}, {});
  for (StatPreset preset : {StatPreset::kFirst, StatPreset::kSecond}) {
    const AbilityPreset& lines = c.ability(preset);
    EXPECT_EQ(lines.rank(), ABILITY_RANK_RARE);
    EXPECT_EQ(lines.lines_size(), kAbilityLines);
  }
  EXPECT_EQ(c.ability_reset_cost(), 100);
}

// Lines without a rank have no reset price, which would leave the preset stuck
// forever.
TEST_F(InnerAbilityTest, RanklessPresetKeepsItsLines) {
  Character proto;
  proto.set_level(160);
  AbilityPreset& lines =
      PresetOf(*proto.mutable_inner_ability(), StatPreset::kFirst);
  lines.add_lines()->set_type(ABILITY_LINE_TYPE_MESO);
  CharacterInstance c(rng_, std::move(proto));

  EXPECT_EQ(c.ability().rank(), ABILITY_RANK_RARE);
  ASSERT_EQ(c.ability().lines_size(), 1);
  EXPECT_EQ(c.ability().lines(0).type(), ABILITY_LINE_TYPE_MESO);
  EXPECT_EQ(c.ability_reset_cost(), 100);
}

TEST_F(InnerAbilityTest, ResetNeedsTheLevelAndTheHonor) {
  CharacterInstance below = MakeCharacter(rng_, /*level=*/159);
  below.AddHonor(1000);
  EXPECT_FALSE(below.inner_ability_unlocked());
  EXPECT_FALSE(below.ResetAbility());
  EXPECT_EQ(below.honor(), 1000);

  CharacterInstance c = MakeCharacter(rng_, /*level=*/160);
  EXPECT_TRUE(c.inner_ability_unlocked());
  EXPECT_FALSE(c.ResetAbility()) << "an empty purse buys nothing";
  c.AddHonor(250);
  const int64_t cost = c.ability_reset_cost();
  EXPECT_TRUE(c.ResetAbility());
  EXPECT_EQ(c.honor(), 250 - cost);
  // Whatever rank it reached, resets stop once the character can't afford the
  // next one.
  while (c.honor() >= c.ability_reset_cost()) {
    EXPECT_TRUE(c.ResetAbility());
  }
  EXPECT_FALSE(c.ResetAbility());
}

// The two presets are rolled separately but paid for from the same honor.
TEST_F(InnerAbilityTest, PresetsAreSeparateAndTheHonorIsNot) {
  CharacterInstance c = MakeCharacter(rng_, /*level=*/160);
  c.AddHonor(1000);
  EXPECT_TRUE(c.ResetAbility(StatPreset::kSecond));
  EXPECT_EQ(c.honor(), 900);
  EXPECT_EQ(c.ability(StatPreset::kFirst).lines(0).type(),
            ABILITY_LINE_TYPE_ALL_STATS)
      << "rolling one preset leaves the other alone";
  EXPECT_TRUE(c.ResetAbility(StatPreset::kFirst));
  EXPECT_EQ(c.honor(), 800);
}

// Locking lines raises the reset price, whatever the locked lines' ranks.
TEST_F(InnerAbilityTest, LockingRaisesTheResetPrice) {
  Character proto;
  proto.set_level(160);
  AbilityPreset& lines =
      PresetOf(*proto.mutable_inner_ability(), StatPreset::kFirst);
  lines.set_rank(ABILITY_RANK_LEGENDARY);
  for (AbilityLineType type :
       {ABILITY_LINE_TYPE_BOSS_DAMAGE, ABILITY_LINE_TYPE_ATTACK,
        ABILITY_LINE_TYPE_MESO}) {
    AbilityLine& line = *lines.add_lines();
    line.set_type(type);
    line.set_rank(type == ABILITY_LINE_TYPE_BOSS_DAMAGE
                      ? ABILITY_RANK_LEGENDARY
                      : (type == ABILITY_LINE_TYPE_ATTACK ? ABILITY_RANK_UNIQUE
                                                          : ABILITY_RANK_EPIC));
  }
  CharacterInstance c(rng_, std::move(proto));

  EXPECT_EQ(c.ability_reset_cost(), 8000);
  EXPECT_TRUE(c.LockAbilityLine(0, true));
  EXPECT_EQ(c.ability_reset_cost(), 11000);
  EXPECT_TRUE(c.LockAbilityLine(1, true));
  EXPECT_EQ(c.ability_reset_cost(), 16000);
  EXPECT_FALSE(c.LockAbilityLine(2, true)) << "two lines are the most";

  c.AddHonor(16000);
  EXPECT_TRUE(c.ResetAbility());
  EXPECT_EQ(c.honor(), 0);
  EXPECT_EQ(c.ability().lines(0).type(), ABILITY_LINE_TYPE_BOSS_DAMAGE);
  EXPECT_EQ(c.ability().lines(1).type(), ABILITY_LINE_TYPE_ATTACK);
}

// A save from before Inner Ability existed loads with every character's
// starting lines.
TEST_F(InnerAbilityTest, AnOldSaveIsSeededOnLoad) {
  Character proto;
  proto.set_level(200);
  CharacterInstance c(rng_, std::move(proto));
  EXPECT_EQ(c.ability().lines_size(), kAbilityLines);
  EXPECT_EQ(c.ability(StatPreset::kSecond).lines_size(), kAbilityLines);
}

TEST_F(HyperStatTest, StatsStopAtTheCapAndArcaneForceAtLevel200) {
  CharacterInstance c = MakeCharacter(rng_, /*level=*/199);
  EXPECT_EQ(c.max_hyper_stat_level(), 10) << "no character takes a 5th job";
  EXPECT_TRUE(
      c.AllocateHyperStat(HYPER_STAT_FIELD_CRIT_RATE, StatPreset::kSecond, 10));
  EXPECT_FALSE(
      c.AllocateHyperStat(HYPER_STAT_FIELD_CRIT_RATE, StatPreset::kSecond));
  EXPECT_DOUBLE_EQ(
      c.hyper_stat_bonus(HYPER_STAT_FIELD_CRIT_RATE, StatPreset::kSecond),
      15.0);
  EXPECT_FALSE(
      c.AllocateHyperStat(HYPER_STAT_FIELD_ARCANE_FORCE, StatPreset::kSecond));
  c.LevelUp();
  EXPECT_TRUE(
      c.AllocateHyperStat(HYPER_STAT_FIELD_ARCANE_FORCE, StatPreset::kSecond));
}

// Each preset spends the same pool separately, and a reset refunds everything.
TEST_F(HyperStatTest, PresetsSpendApartAndResetFree) {
  CharacterInstance c = MakeCharacter(rng_, /*level=*/160);
  ASSERT_TRUE(c.AllocateHyperStat(HYPER_STAT_FIELD_EXP, StatPreset::kFirst, 5));
  ASSERT_TRUE(c.AllocateHyperStat(HYPER_STAT_FIELD_BOSS_DAMAGE,
                                  StatPreset::kSecond, 5));
  EXPECT_EQ(c.hyper_stat_points_left(StatPreset::kFirst),
            c.hyper_stat_points_left(StatPreset::kSecond));
  EXPECT_EQ(c.hyper_stat_level(HYPER_STAT_FIELD_EXP, StatPreset::kSecond), 0);

  c.ResetHyperStats(StatPreset::kFirst);
  EXPECT_EQ(c.hyper_stat_points_left(StatPreset::kFirst),
            c.hyper_stat_points());
  EXPECT_EQ(
      c.hyper_stat_level(HYPER_STAT_FIELD_BOSS_DAMAGE, StatPreset::kSecond), 5)
      << "the other allocation is untouched";
}

// A refund returns exactly what the level cost, and stops at zero.
TEST_F(HyperStatTest, RefundUndoesOneLevelAtATime) {
  CharacterInstance c = MakeCharacter(rng_, /*level=*/160);
  int pool = c.hyper_stat_points();
  ASSERT_TRUE(
      c.AllocateHyperStat(HYPER_STAT_FIELD_DAMAGE, StatPreset::kFirst, 4));
  ASSERT_TRUE(c.RefundHyperStat(HYPER_STAT_FIELD_DAMAGE, StatPreset::kFirst));
  EXPECT_EQ(c.hyper_stat_level(HYPER_STAT_FIELD_DAMAGE, StatPreset::kFirst), 3);
  // 1 + 2 + 4 buys three levels, whatever order they were bought and refunded
  // in.
  EXPECT_EQ(c.hyper_stat_points_left(StatPreset::kFirst), pool - 7);

  ASSERT_TRUE(
      c.RefundHyperStat(HYPER_STAT_FIELD_DAMAGE, StatPreset::kFirst, 3));
  EXPECT_EQ(c.hyper_stat_points_left(StatPreset::kFirst), pool);
  EXPECT_FALSE(c.RefundHyperStat(HYPER_STAT_FIELD_DAMAGE, StatPreset::kFirst))
      << "nothing left to give back";
}

// All or nothing, and it applies to the allocation it was given.
TEST_F(HyperStatTest, RefundPastWhatIsSpentChangesNothing) {
  CharacterInstance c = MakeCharacter(rng_, /*level=*/160);
  ASSERT_TRUE(
      c.AllocateHyperStat(HYPER_STAT_FIELD_LUK, StatPreset::kSecond, 2));
  EXPECT_FALSE(c.RefundHyperStat(HYPER_STAT_FIELD_LUK, StatPreset::kSecond, 3));
  EXPECT_EQ(c.hyper_stat_level(HYPER_STAT_FIELD_LUK, StatPreset::kSecond), 2);
  EXPECT_FALSE(c.RefundHyperStat(HYPER_STAT_FIELD_LUK, StatPreset::kFirst))
      << "the other allocation spent nothing on it";
  EXPECT_EQ(c.hyper_stat_level(HYPER_STAT_FIELD_LUK, StatPreset::kSecond), 2);
}

// A save from older rules: a stat past the cap, one the level has locked, and
// an allocation that spends more than the pool.
TEST_F(HyperStatTest, ReconcileTrimsAnAllocationBackToThePool) {
  Character proto;
  proto.set_level(150);
  HyperStatPreset& farming =
      PresetOf(*proto.mutable_hyper_stats(), StatPreset::kFirst);
  (*farming.mutable_levels())[HYPER_STAT_FIELD_STR] = 14;
  (*farming.mutable_levels())[HYPER_STAT_FIELD_ARCANE_FORCE] = 3;
  (*farming.mutable_levels())[HYPER_STAT_FIELD_DAMAGE] = 8;
  CharacterInstance c(rng_, std::move(proto));

  EXPECT_GT(c.ReconcileHyperStats(), 0);
  EXPECT_EQ(c.hyper_stat_level(HYPER_STAT_FIELD_ARCANE_FORCE), 0)
      << "level 150 has not opened it";
  EXPECT_LE(c.hyper_stat_level(HYPER_STAT_FIELD_STR), 10);
  EXPECT_GE(c.hyper_stat_points_left(), 0);
  EXPECT_EQ(c.ReconcileHyperStats(), 0) << "a balanced book stays put";
}

// Shared fixture for tests on a character with a sword prototype. Provides c_
// (a new level-1 character) and sword_ (named "Sword", primary weapon slot, 7
// upgrade slots). Tests pick up and equip as needed.
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
class SortTabTest : public CharacterEquipFixture {};

// The equip sort's first key is whether the character can wear the item, so a
// piece above their level sorts below one they can wear, however good it is.
TEST_F(SortTabTest, EquipTabPutsWhatCanBeWornOnTop) {
  sword_.add_equip_job_categories(EQUIP_JOB_CATEGORY_UNIVERSAL);
  c_.AdvanceJob(JOB_BEGINNER);
  EquipPrototype gated = sword_;
  gated.set_name("Gated Sword");
  gated.set_required_level(100);
  Equip starred;
  starred.set_stars(15);
  ASSERT_TRUE(c_.PickUp(std::make_unique<EquipInstance>(gated, starred)));
  ASSERT_TRUE(c_.PickUp(std::make_unique<EquipInstance>(sword_)));
  c_.SortEquipTab();
  EXPECT_EQ(c_.inventory()[0].name(), "Sword");
  EXPECT_EQ(c_.inventory()[1].name(), "Gated Sword");
}

// The Etc tab holds only drops, so the biggest stack comes first. A currency
// added alongside them isn't on the tab at all.
TEST_F(SortTabTest, StackTabFilesByCount) {
  ItemPrototype trace;
  trace.set_name("Spell Trace");
  trace.set_kind(ITEM_KIND_SPELL_TRACE);
  ItemPrototype horn;
  horn.set_name("Broken Horn");
  ItemPrototype shell;
  shell.set_name("Egg Shell");
  c_.AddItem(shell, 5);
  c_.AddItem(horn, 50);
  c_.AddItem(trace, 3);
  c_.SortStackTab();
  ASSERT_EQ(c_.stackables().size(), 2u);
  EXPECT_EQ(c_.stackables()[0].name(), "Broken Horn");
  EXPECT_EQ(c_.stackables()[1].name(), "Egg Shell");
}

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
  // A Beginner is neither warrior nor mage, so it gets the middle rate.
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

// Only warriors and mages have their own rates; everyone else shares the middle
// one, so a rogue levels exactly like an archer.
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
  // The rate is the one at the time of levelling, so the two Beginner levels
  // below keep the Beginner rate even after the character becomes a Warrior.
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

// Every band pays exactly what its book costs: 60, 90, 120, and 200 for the 4th
// job, the only band that pays five a level instead of three.
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
    c.LevelUp();  // levels 101..140
  }
  EXPECT_EQ(c.proto().level(), 140);
  EXPECT_EQ(c.sp(4), 200) << "40 levels at five, and the book costs the lot";

  for (int i = 0; i < 60; ++i) {
    c.LevelUp();  // levels 141..200
  }
  EXPECT_EQ(c.proto().level(), 200);
  EXPECT_EQ(c.sp(4), 200)
      << "the book is bought out; the levels above pay none";
  EXPECT_EQ(c.proto().ap(), 5 * 190) << "AP keeps coming either way";
}

TEST_F(LevelUpTest, TheFourthJobPaysFiveALevel) {
  CharacterInstance c = MakeCharacter(rng_, /*level=*/100);
  c.LevelUp();
  EXPECT_EQ(c.sp(4), 5);
  c.LevelUp();
  EXPECT_EQ(c.sp(4), 10);
  EXPECT_EQ(c.sp(3), 0) << "the 3rd job's band is closed behind them";
}

// The Hyper SP ladder: one point at 140 and every fifth level to 195, and none
// before or after. Twelve in all, one per Hyper Skill.
TEST_F(LevelUpTest, PaysOneHyperSpEveryFifthLevelFrom140To195) {
  CharacterInstance c = MakeCharacter(rng_, /*level=*/139);
  c.LevelUp();  // 140, the first rung
  EXPECT_EQ(c.hyper_sp(), 1);
  c.LevelUp();  // 141, between rungs
  EXPECT_EQ(c.hyper_sp(), 1);
  for (int i = 0; i < 4; ++i) {
    c.LevelUp();  // 142..145, the second rung
  }
  EXPECT_EQ(c.hyper_sp(), 2);
  while (c.proto().level() < 195) {
    c.LevelUp();
  }
  EXPECT_EQ(c.hyper_sp(), 12) << "one for each of a job's twelve Hyper Skills";
  while (c.proto().level() < 200) {
    c.LevelUp();
  }
  EXPECT_EQ(c.hyper_sp(), 12) << "195 is the last rung";
}

TEST_F(LevelUpTest, PaysNoHyperSpBelowOneForty) {
  CharacterInstance c = MakeCharacter(rng_, /*level=*/134);
  for (int i = 0; i < 5; ++i) {
    c.LevelUp();  // 135..139, one of them a multiple of five
  }
  EXPECT_EQ(c.hyper_sp(), 0);
}

TEST_F(LevelUpTest, EveryFirstJobReachesTheSameSixty) {
  // No job gets a head start; the pools are identical.
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
  // What levelling from `from` to `to` really grants, read from a character
  // that actually levelled. SP is summed across every stage, since a range can
  // cross a band and the caller wants the total.
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
    return {c.proto().ap() - ap_before, sp_after - sp_before, c.hyper_sp()};
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
  // 10 -> 12 reaches 11 and 12; SP starts at 11, so both pay.
  EXPECT_EQ(GainsForLevels(10, 12).sp, 6);
  // 8 -> 10 reaches 9 and 10, both below the band.
  EXPECT_EQ(GainsForLevels(8, 10).sp, 0);
}

// A range that crosses a job band still totals what was earned, even though
// LevelUp put the two halves into different stage pools.
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

// The helper should give for a range what LevelUp gives one level at a time.
// These tests check it against a character that really levelled, so a change to
// one that misses the other fails here rather than showing a player the wrong
// number.
TEST_F(GainsForLevelsTest, AgreesWithLevellingUpForReal) {
  const std::pair<int, int> spans[] = {
      {1, 2},  {1, 10},   {10, 11},   {10, 30},   {29, 32},
      {1, 40}, {99, 104}, {100, 140}, {139, 146}, {140, 200},
  };
  for (const std::pair<int, int>& span : spans) {
    LevelGains predicted = GainsForLevels(span.first, span.second);
    LevelGains actual = Actual(span.first, span.second);
    EXPECT_EQ(predicted.ap, actual.ap)
        << "AP for levels " << span.first << " -> " << span.second;
    EXPECT_EQ(predicted.sp, actual.sp)
        << "SP for levels " << span.first << " -> " << span.second;
    EXPECT_EQ(predicted.hyper_sp, actual.hyper_sp)
        << "Hyper SP for levels " << span.first << " -> " << span.second;
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
  // Far more than the 243B that level 259 needs. The rest is thrown away at the
  // cap.
  c.AddExp(500000000000LL);
  EXPECT_EQ(c.proto().level(), kTrialLevelCap);
  EXPECT_EQ(c.proto().exp(), 0);
}

// The cap limits what combat pays, not what a level is. The debug item grants a
// level through LevelUp, which still goes past the cap.
TEST_F(AddExpTest, LevelUpItselfIsNotCapped) {
  CharacterInstance c = MakeCharacter(rng_, /*level=*/kTrialLevelCap);
  c.LevelUp();
  EXPECT_EQ(c.proto().level(), kTrialLevelCap + 1);
}

// --- Burning ---

class BurningTest : public CharacterTest {};

TEST_F(BurningTest, OneLevelWithNobodyAhead) {
  EXPECT_EQ(LevelAfterBurning(50, {}), 51);
  EXPECT_EQ(LevelAfterBurning(50, {40, 30}), 51);
}

TEST_F(BurningTest, TwoThreeAndFiveAsTheAccountFillsUp) {
  EXPECT_EQ(LevelAfterBurning(50, {120}), 52);
  EXPECT_EQ(LevelAfterBurning(50, {120, 110, 100}), 53);
  EXPECT_EQ(LevelAfterBurning(50, std::vector<int>(10, 100)), 55);
  // Only characters above this one count, so an account with enough characters
  // for a tier still gets the lower tier when they aren't all ahead.
  std::vector<int> two_ahead = {120, 110, 10, 10, 10, 10, 10, 10, 10, 10};
  EXPECT_EQ(LevelAfterBurning(50, two_ahead), 52);
}

// The tier that goes furthest wins, not the fastest: five levels at a time
// would pass the tenth highest, so the three-level tier takes the character
// further.
TEST_F(BurningTest, ATierStopsAtItsOwnCeiling) {
  std::vector<int> levels(9, 150);
  levels.push_back(100);
  EXPECT_EQ(LevelAfterBurning(98, levels), 101);
  EXPECT_EQ(LevelAfterBurning(99, levels), 102);
  // Being level with the lowest character a tier counts drops to the next tier:
  // the third highest is still ahead, but the tenth is not.
  EXPECT_EQ(LevelAfterBurning(100, levels), 103);
}

TEST_F(BurningTest, NeverPastTheBurningLevel) {
  std::vector<int> ten(10, kTrialLevelCap);
  EXPECT_EQ(LevelAfterBurning(kBurningLevel - 4, ten), kBurningLevel);
  EXPECT_EQ(LevelAfterBurning(kBurningLevel - 1, ten), kBurningLevel);
  EXPECT_EQ(LevelAfterBurning(kBurningLevel, ten), kBurningLevel + 1);
}

// One threshold gives every level Burning grants, with each level's gains, and
// the leftover EXP carries to the new level.
TEST_F(AddExpTest, BurningPaysOneThresholdForSeveralLevels) {
  CharacterInstance c = MakeCharacter(rng_, /*level=*/1);
  c.AddExp(20, {100});  // level 1 costs 15, leaving 5
  EXPECT_EQ(c.proto().level(), 3);
  EXPECT_EQ(c.proto().exp(), 5);
  EXPECT_EQ(c.proto().ap(), 10);
}

// Every threshold the EXP crosses triggers Burning again, and the EXP left
// after each is measured against the new level.
TEST_F(AddExpTest, BurningAgainForEveryThresholdCrossed) {
  CharacterInstance c = MakeCharacter(rng_, /*level=*/1);
  // 15 buys levels 2 and 3, and 57 buys 4 and 5. The 12 left over is short of
  // level 5's threshold.
  c.AddExp(15 + 57 + 12, {100});
  EXPECT_EQ(c.proto().level(), 5);
  EXPECT_EQ(c.proto().exp(), 12);
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

TEST_F(AdvanceJobTest, OpensAtLevelTenAndNotBefore) {
  EXPECT_FALSE(MakeCharacter(rng_, /*level=*/9).CanAdvanceJob());
  EXPECT_TRUE(MakeCharacter(rng_, /*level=*/10).CanAdvanceJob());
}

TEST_F(AdvanceJobTest, NothingPendingOnceAdvanced) {
  CharacterInstance c = MakeCharacter(rng_, /*level=*/10);
  c.AdvanceJob(JOB_SWORDMAN);
  EXPECT_FALSE(c.CanAdvanceJob());
}

// A Shadower at level 100 has no advancement to take.
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
  EXPECT_FALSE(c.CanAdvanceJob());  // the level, not the job, is what's short
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

// The order is the stat order, not the order the protos happen to list them in.
TEST(JobChoicesTest, OffersTheFourExplorersInStatOrder) {
  EXPECT_EQ(
      JobChoicesForStage(JOB_BEGINNER, 1),
      (std::vector<Job>{JOB_SWORDMAN, JOB_ARCHER, JOB_MAGICIAN, JOB_ROGUE}));
}

// All three, in the Job enum's order.
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

// The 3rd advancement offers one job, not a choice.
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
  // The 4th also offers just one.
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
  // The 5th offers the job already held: it opens a book without changing the
  // job, so there is nothing to choose.
  EXPECT_EQ(JobChoicesForStage(JOB_DARK_KNIGHT, 5),
            (std::vector<Job>{JOB_DARK_KNIGHT}));
  EXPECT_EQ(JobChoicesForStage(JOB_PALADIN, 5),
            (std::vector<Job>{JOB_PALADIN}));
  EXPECT_TRUE(JobChoicesForStage(JOB_DARK_KNIGHT, 6).empty());
  EXPECT_TRUE(JobChoicesForStage(JOB_BEGINNER, 0).empty());
}

// A character keeps every book they bought along the way: each page stays on
// the skills tab, and each SP pool stays spendable.
TEST(JobChoicesTest, ABerserkerKeepsEveryBookBelowTheirOwn) {
  EXPECT_EQ(AdvancementForJobStage(JOB_SPEARMAN, 1), JOB_ADVANCEMENT_SWORDMAN);
  EXPECT_EQ(AdvancementForJobStage(JOB_SPEARMAN, 2), JOB_ADVANCEMENT_SPEARMAN);
  EXPECT_EQ(AdvancementForJobStage(JOB_BERSERKER, 1), JOB_ADVANCEMENT_SWORDMAN);
  EXPECT_EQ(AdvancementForJobStage(JOB_BERSERKER, 2), JOB_ADVANCEMENT_SPEARMAN);
  EXPECT_EQ(AdvancementForJobStage(JOB_BERSERKER, 3),
            JOB_ADVANCEMENT_BERSERKER);
  EXPECT_EQ(StageForAdvancement(JOB_ADVANCEMENT_BERSERKER), 3);
}

// A Berserker counts as a warrior in every table keyed by job: the stat they
// spend AP on, the HP a level grants, and the gear they may wear.
TEST(JobChoicesTest, ABerserkerCountsAsAWarriorThroughout) {
  EXPECT_EQ(PrimaryStatField(JOB_BERSERKER), STAT_FIELD_STR);
  EXPECT_EQ(SecondaryStatField(JOB_BERSERKER), STAT_FIELD_DEX);
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

// The pair of stats each branch uses, as paired in the damage formula: four
// branches, and a job outside them has neither.
TEST(JobChoicesTest, EveryBranchPairsAPrimaryStatWithASecondary) {
  EXPECT_EQ(SecondaryStatField(JOB_BEGINNER), STAT_FIELD_DEX);
  EXPECT_EQ(SecondaryStatField(JOB_BOW_MASTER), STAT_FIELD_STR);
  EXPECT_EQ(SecondaryStatField(JOB_BISHOP), STAT_FIELD_LUK);
  EXPECT_EQ(SecondaryStatField(JOB_SHADOWER), STAT_FIELD_DEX);
  EXPECT_EQ(SecondaryStatField(JOB_UNSPECIFIED), STAT_FIELD_UNSPECIFIED);
  for (int i = 0; i <= Job_MAX; ++i) {
    Job job = static_cast<Job>(i);
    if (!Job_IsValid(i) || PrimaryStatField(job) == STAT_FIELD_UNSPECIFIED) {
      continue;
    }
    EXPECT_NE(SecondaryStatField(job), PrimaryStatField(job)) << Job_Name(job);
    EXPECT_NE(SecondaryStatField(job), STAT_FIELD_UNSPECIFIED) << Job_Name(job);
  }
}

// Every advancement names exactly the job that takes it, and that job maps back
// to the advancement at its stage. A branch wired in only one direction would
// send the workbench to the wrong job.
TEST(JobChoicesTest, EveryAdvancementRoundTripsToItsJob) {
  for (int i = 1; i <= JobAdvancement_MAX; ++i) {
    JobAdvancement advancement = static_cast<JobAdvancement>(i);
    // None of these three (common nodes, the beginner book, link skills) is an
    // advancement anyone takes, so none names a job.
    if (advancement == JOB_ADVANCEMENT_COMMON ||
        advancement == JOB_ADVANCEMENT_BEGINNER ||
        advancement == JOB_ADVANCEMENT_LINK) {
      EXPECT_EQ(JobForAdvancement(advancement), JOB_UNSPECIFIED);
      continue;
    }
    Job job = JobForAdvancement(advancement);
    ASSERT_NE(job, JOB_UNSPECIFIED) << JobAdvancement_Name(advancement);
    EXPECT_EQ(AdvancementForJobStage(job, StageForAdvancement(advancement)),
              advancement);
  }
}

TEST(JobChoicesTest, NoJobTakesAnUnspecifiedAdvancement) {
  EXPECT_EQ(JobForAdvancement(JOB_ADVANCEMENT_UNSPECIFIED), JOB_UNSPECIFIED);
}

// The levels LevelUp offers each advancement at: a 1st job lasts until 30 and a
// 2nd until 60. That is what places a character who starts at the top of a
// stage in the right one.
TEST(JobChoicesTest, EachStageEndsAtItsNextAdvancement) {
  EXPECT_EQ(NextAdvancementLevel(0), 10);
  EXPECT_EQ(NextAdvancementLevel(1), 30);
  EXPECT_EQ(NextAdvancementLevel(2), 60);
  EXPECT_EQ(NextAdvancementLevel(-1), 0);
  EXPECT_EQ(NextAdvancementLevel(99), 0);
}

// --- skill requirements ---

// Shaped like Hyper Body: it needs three points in Iron Wall first.
Skill MakeGatedSkill() {
  Skill skill;
  skill.set_name("Hyper Body");
  skill.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(skill, JOB_ADVANCEMENT_SPEARMAN);
  skill.set_max_level(10);
  skill.mutable_required_skill()->set_skill_name("Iron Wall");
  skill.mutable_required_skill()->set_level(3);
  return skill;
}

Skill MakeGateSkill() {
  Skill skill;
  skill.set_name("Iron Wall");
  skill.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(skill, JOB_ADVANCEMENT_SPEARMAN);
  skill.set_max_level(10);
  return skill;
}

// A Spearman with SP to spend in their 2nd-job pool.
CharacterInstance MakeSpearman(std::mt19937& rng, int sp) {
  Character proto;
  proto.set_level(60);
  proto.set_job(JOB_SPEARMAN);
  proto.set_job_stage(2);
  (*proto.mutable_sp_by_stage())[2] = sp;
  return CharacterInstance(rng, std::move(proto));
}

// The skill stays locked until the requirement is fully met: first not at all,
// then partly, then at the required level.
TEST_F(CharacterTest, ASkillOpensOnlyOnceItsRequirementIsMet) {
  CharacterInstance c = MakeSpearman(rng_, 20);
  EXPECT_FALSE(c.MeetsSkillRequirement(MakeGatedSkill()));
  EXPECT_FALSE(c.LearnSkill(MakeGatedSkill()));
  EXPECT_EQ(c.skill_level(MakeGatedSkill()), 0);
  EXPECT_EQ(c.sp(2), 20);  // and the point is not taken either

  ASSERT_TRUE(c.LearnSkill(MakeGateSkill(), 2));
  EXPECT_FALSE(c.MeetsSkillRequirement(MakeGatedSkill()));
  EXPECT_FALSE(c.LearnSkill(MakeGatedSkill()));

  ASSERT_TRUE(c.LearnSkill(MakeGateSkill(), 1));
  EXPECT_TRUE(c.MeetsSkillRequirement(MakeGatedSkill()));
  EXPECT_TRUE(c.LearnSkill(MakeGatedSkill()));
  EXPECT_EQ(c.skill_level(MakeGatedSkill()), 1);
}

// Most skills require nothing, and the check must not block them.
TEST_F(CharacterTest, ASkillDemandingNothingIsAlwaysOpen) {
  CharacterInstance c = MakeSpearman(rng_, 20);
  EXPECT_TRUE(c.MeetsSkillRequirement(MakeGateSkill()));
}

// --- Hyper Skills ---

// A Dark Knight's Hyper Skill, at the level GMS unlocks it: one point, from its
// own pool, and not before the skill's required level.
Skill MakeHyperSkill(int required_level = 150) {
  Skill skill;
  skill.set_name("Gungnir's Descent - Reinforce");
  skill.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(skill, JOB_ADVANCEMENT_DARK_KNIGHT);
  skill.set_max_level(1);
  skill.set_hyper(true);
  skill.set_required_level(required_level);
  return skill;
}

// A Dark Knight at `level` with `hyper_sp` and no stage SP at all: a Hyper
// Skill must not be buyable with the 4th job's SP.
CharacterInstance MakeDarkKnight(std::mt19937& rng, int level, int hyper_sp) {
  Character proto;
  proto.set_level(level);
  proto.set_job(JOB_DARK_KNIGHT);
  proto.set_job_stage(4);
  proto.set_hyper_sp(hyper_sp);
  return CharacterInstance(rng, std::move(proto));
}

// A common node, which every matrix can hold.
Skill MakeCommonNode() {
  Skill skill;
  skill.set_name("Rope Lift");
  skill.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(skill, JOB_ADVANCEMENT_COMMON);
  skill.set_v_node(V_NODE_KIND_COMMON);
  skill.set_max_level(MaxVNodeLevel(V_NODE_KIND_COMMON));
  return skill;
}

// A 5th job with `v_points` and `hyper_sp`, and no stage SP at all.
CharacterInstance MakeFifthJob(std::mt19937& rng, int64_t v_points,
                               int hyper_sp = 0) {
  Character proto;
  proto.set_level(200);
  proto.set_job(JOB_DARK_KNIGHT);
  proto.set_job_stage(5);
  proto.set_v_points(v_points);
  proto.set_hyper_sp(hyper_sp);
  return CharacterInstance(rng, std::move(proto));
}

// A node is bought with V Points by its kind's ladder rather than one point per
// level, so the first level of a common node costs seven.
TEST_F(LearnSkillTest, ANodeSpendsVPointsByItsLadder) {
  CharacterInstance c = MakeFifthJob(rng_, /*v_points=*/20);
  const Skill node = MakeCommonNode();
  ASSERT_TRUE(c.LearnSkill(node));
  EXPECT_EQ(c.skill_level(node), 1);
  EXPECT_EQ(c.v_points(), 13) << "seven for the first level";
  EXPECT_EQ(c.sp(5), 0) << "no stage paid for it";

  // Three more at four each, and then the pool is one point short.
  ASSERT_TRUE(c.LearnSkill(node, 3));
  EXPECT_EQ(c.skill_level(node), 4);
  EXPECT_EQ(c.v_points(), 1);
  EXPECT_FALSE(c.LearnSkill(node));
  EXPECT_EQ(c.v_points(), 1) << "and nothing is taken for the refusal";
}

// The pool buys levels, not points: how many depends on the node's current
// level, so the pool is counted up the ladder rather than divided.
TEST_F(LearnSkillTest, ANodeOffersOnlyTheLevelsItsPoolReaches) {
  const Skill node = MakeCommonNode();
  // Seven for the first level and four for each after: ten points buys one.
  CharacterInstance thin = MakeFifthJob(rng_, /*v_points=*/10);
  EXPECT_EQ(thin.LevelsAffordable(node), 1);
  CharacterInstance fuller = MakeFifthJob(rng_, /*v_points=*/15);
  EXPECT_EQ(fuller.LevelsAffordable(node), 3) << "7 + 4 + 4";
  CharacterInstance broke = MakeFifthJob(rng_, /*v_points=*/6);
  EXPECT_EQ(broke.LevelsAffordable(node), 0);
  // Never more than the node has left.
  CharacterInstance rich = MakeFifthJob(rng_, /*v_points=*/100000);
  EXPECT_EQ(rich.LevelsAffordable(node), node.max_level());
  ASSERT_TRUE(rich.LearnSkill(node, node.max_level()));
  EXPECT_EQ(rich.LevelsAffordable(node), 0);
}

// A node needs a matrix, so a character without one buys nothing however many
// points they have.
TEST_F(LearnSkillTest, ANodeNeedsAMatrixAndTheRightMatrix) {
  Character proto;
  proto.set_level(200);
  proto.set_job(JOB_DARK_KNIGHT);
  proto.set_job_stage(4);
  proto.set_v_points(1000);
  CharacterInstance fourth(rng_, std::move(proto));
  EXPECT_FALSE(fourth.LearnSkill(MakeCommonNode()));
  EXPECT_EQ(fourth.v_points(), 1000);

  // A job's own node names its 5th advancement, and only that job can hold it.
  Skill job_node = MakeCommonNode();
  job_node.set_name("Radiant Evil");
  job_node.set_v_node(V_NODE_KIND_JOB);
  job_node.clear_placement();
  PlaceIn(job_node, JOB_ADVANCEMENT_PALADIN_V);
  CharacterInstance knight = MakeFifthJob(rng_, /*v_points=*/1000);
  EXPECT_FALSE(knight.LearnSkill(job_node));
  job_node.clear_placement();
  PlaceIn(job_node, JOB_ADVANCEMENT_DARK_KNIGHT_V);
  EXPECT_TRUE(knight.LearnSkill(job_node));
  EXPECT_EQ(knight.v_points(), 1000) << "a job node's first level is free";
}

// The reset empties the matrix and refunds every point, leaving the SP books
// and their skills unchanged.
TEST_F(LearnSkillTest, ResetVMatrixRefundsEveryNode) {
  CharacterInstance c = MakeFifthJob(rng_, /*v_points=*/1000, /*hyper_sp=*/1);
  const Skill common = MakeCommonNode();
  Skill job_node = MakeCommonNode();
  job_node.set_name("Radiant Evil");
  job_node.set_v_node(V_NODE_KIND_JOB);
  job_node.clear_placement();
  PlaceIn(job_node, JOB_ADVANCEMENT_DARK_KNIGHT_V);
  const Skill hyper = MakeHyperSkill();
  ASSERT_TRUE(c.LearnSkill(common, 5));
  ASSERT_TRUE(c.LearnSkill(job_node, 5));
  ASSERT_LT(c.v_points(), 1000);
  // A Hyper Skill on the same character, to show the reset leaves it alone.
  ASSERT_TRUE(c.LearnSkill(hyper));

  std::map<std::string, Skill> catalog = {
      {"rope_lift", common}, {"radiant_evil", job_node}, {"hyper", hyper}};
  c.ResetVMatrix(catalog);
  EXPECT_EQ(c.v_points(), 1000) << "every point back in the pool";
  EXPECT_EQ(c.skill_level(common), 0);
  EXPECT_EQ(c.skill_level(job_node), 0);
  EXPECT_EQ(c.skill_level(hyper), 1) << "a hyper is not a node";
  EXPECT_EQ(c.hyper_sp(), 0) << "and its pool is not what was handed back";
  // Resetting an empty matrix takes nothing and gives nothing.
  c.ResetVMatrix(catalog);
  EXPECT_EQ(c.v_points(), 1000);
}

TEST_F(LearnSkillTest, AHyperSkillSpendsTheHyperPool) {
  CharacterInstance c = MakeDarkKnight(rng_, /*level=*/150, /*hyper_sp=*/2);
  ASSERT_TRUE(c.LearnSkill(MakeHyperSkill()));
  EXPECT_EQ(c.skill_level(MakeHyperSkill()), 1);
  EXPECT_EQ(c.hyper_sp(), 1);
  EXPECT_EQ(c.sp(4), 0) << "no stage paid for it";
  // One point is all it takes: max_level is 1.
  EXPECT_FALSE(c.LearnSkill(MakeHyperSkill()));
  EXPECT_EQ(c.hyper_sp(), 1);
}

TEST_F(LearnSkillTest, AHyperSkillIsShutBelowItsLevel) {
  CharacterInstance c = MakeDarkKnight(rng_, /*level=*/149, /*hyper_sp=*/2);
  EXPECT_FALSE(c.LearnSkill(MakeHyperSkill()));
  EXPECT_EQ(c.hyper_sp(), 2) << "and the point is not taken either";
}

TEST_F(LearnSkillTest, AHyperSkillNeedsAPointOfItsOwnKind) {
  Character proto;
  proto.set_level(150);
  proto.set_job(JOB_DARK_KNIGHT);
  proto.set_job_stage(4);
  (*proto.mutable_sp_by_stage())[4] = 200;
  CharacterInstance c(rng_, std::move(proto));
  EXPECT_FALSE(c.LearnSkill(MakeHyperSkill()))
      << "the 4th job's book cannot buy a hyper";
}

// A Hyper Skill's advancement is the book it belongs to, and it gates the skill
// the same way as any other skill's.
TEST_F(LearnSkillTest, AHyperSkillBelongsToItsOwnJob) {
  Character proto;
  proto.set_level(150);
  proto.set_job(JOB_PALADIN);
  proto.set_job_stage(4);
  proto.set_hyper_sp(2);
  CharacterInstance paladin(rng_, std::move(proto));
  EXPECT_FALSE(paladin.LearnSkill(MakeHyperSkill()));
}

// --- Toggle skills ---

// The Bishop's toggle. The four forms it switches to are keyed by its display
// name.
Skill MakeToggleSkill() {
  Skill skill;
  skill.set_name("Righteously Indignant");
  skill.set_kind(SKILL_KIND_ACTIVE);
  PlaceIn(skill, JOB_ADVANCEMENT_BISHOP);
  skill.set_max_level(1);
  skill.set_hyper(true);
  skill.set_required_level(140);
  skill.set_toggle(true);
  return skill;
}

// The form that replaces Heal while the toggle above is on.
Skill MakeVengeanceForm() {
  Skill skill;
  skill.set_name("Angelic Wrath");
  skill.set_kind(SKILL_KIND_ATTACK);
  PlaceIn(skill, JOB_ADVANCEMENT_CLERIC);
  skill.set_max_level(10);
  skill.set_replaces_skill_name("Heal");
  skill.set_toggle_skill_name("Righteously Indignant");
  return skill;
}

CharacterInstance MakeBishop(std::mt19937& rng, int hyper_sp) {
  Character proto;
  proto.set_level(140);
  proto.set_job(JOB_BISHOP);
  proto.set_job_stage(4);
  proto.set_hyper_sp(hyper_sp);
  return CharacterInstance(rng, std::move(proto));
}

class ToggleSkillTest : public CharacterTest {};

TEST_F(ToggleSkillTest, SwitchesOnAndBackOff) {
  CharacterInstance c = MakeBishop(rng_, /*hyper_sp=*/1);
  ASSERT_TRUE(c.LearnSkill(MakeToggleSkill()));
  EXPECT_FALSE(c.SkillToggledOn("Righteously Indignant"))
      << "learning it does not switch it on";
  EXPECT_TRUE(c.ToggleSkill(MakeToggleSkill()));
  EXPECT_TRUE(c.SkillToggledOn("Righteously Indignant"));
  EXPECT_FALSE(c.ToggleSkill(MakeToggleSkill()));
  EXPECT_FALSE(c.SkillToggledOn("Righteously Indignant"));
}

TEST_F(ToggleSkillTest, RefusesWhatWasNeverBoughtOrIsNoToggle) {
  CharacterInstance c = MakeBishop(rng_, /*hyper_sp=*/1);
  EXPECT_FALSE(c.ToggleSkill(MakeToggleSkill()));
  EXPECT_FALSE(c.SkillToggledOn("Righteously Indignant"));
  Skill plain = MakeToggleSkill();
  plain.set_toggle(false);
  ASSERT_TRUE(c.LearnSkill(plain));
  EXPECT_FALSE(c.ToggleSkill(plain));
  EXPECT_FALSE(c.SkillToggledOn("Righteously Indignant"));
}

// One row of the book shares one level: the form reads the level of the skill
// it replaces, and can never be bought itself.
TEST_F(ToggleSkillTest, AFormReadsTheLevelOfWhatItReplaces) {
  CharacterInstance c =
      MakeCharacterWithSp(rng_, /*stage=*/2, /*sp=*/10, JOB_CLERIC);
  Skill heal = MakeSkill("Heal", JOB_ADVANCEMENT_CLERIC, 10);
  ASSERT_TRUE(c.LearnSkill(heal, 4));
  EXPECT_EQ(c.skill_level(MakeVengeanceForm()), 4);
  EXPECT_FALSE(c.LearnSkill(MakeVengeanceForm()));
  EXPECT_EQ(c.skill_level(heal), 4) << "and it spent nothing doing so";
  EXPECT_EQ(c.sp(2), 6);
}

// --- ResetStatsForJob ---

// A level-10 Beginner's stats, straight from the starting proto.
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
  // 45 unspent + 9 refunded from STR, minus the 21 that raise LUK to 25.
  EXPECT_EQ(c.proto().ap(), 33);
}

// The refund is computed from the current stats, so a player who already spent
// AP ends up in the same place as one who hadn't.
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

// HP and MP are in the same message but come from levelling, so the reset must
// leave them alone.
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
// isn't left partly allocated.
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

// One point per press, from the stage's own pool. A skill nobody has bought is
// level zero rather than absent.
TEST_F(LearnSkillTest, SpendsOnePointAPressAndRaisesTheLevel) {
  CharacterInstance c = MakeCharacterWithSp(rng_, /*stage=*/1, /*sp=*/5);
  Skill skill = SlashBlast();
  EXPECT_EQ(c.skill_level(skill), 0);

  EXPECT_TRUE(c.LearnSkill(skill));
  EXPECT_EQ(c.skill_level(skill), 1);
  EXPECT_EQ(c.sp(1), 4);

  c.LearnSkill(skill);
  EXPECT_EQ(c.skill_level(skill), 2);
  EXPECT_EQ(c.sp(1), 3);
}

TEST_F(LearnSkillTest, MultiPointSpendWorks) {
  CharacterInstance c = MakeCharacterWithSp(rng_, /*stage=*/1, /*sp=*/10);
  Skill skill = SlashBlast();
  EXPECT_TRUE(c.LearnSkill(skill, 7));
  EXPECT_EQ(c.skill_level(skill), 7);
  EXPECT_EQ(c.sp(1), 3);
}

TEST_F(LearnSkillTest, RejectsWhenStageLacksSp) {
  CharacterInstance c = MakeCharacterWithSp(rng_, /*stage=*/1, /*sp=*/2);
  Skill skill = SlashBlast();
  EXPECT_FALSE(c.LearnSkill(skill, 3));
  EXPECT_EQ(c.skill_level(skill), 0);
  EXPECT_EQ(c.sp(1), 2);
}

TEST_F(LearnSkillTest, RejectsRaisingPastMaxLevel) {
  CharacterInstance c = MakeCharacterWithSp(rng_, /*stage=*/1, /*sp=*/10);
  Skill skill = SlashBlast(/*max_level=*/3);
  EXPECT_TRUE(c.LearnSkill(skill, 3));   // to the cap
  EXPECT_FALSE(c.LearnSkill(skill, 1));  // one past
  EXPECT_EQ(c.skill_level(skill), 3);
  EXPECT_EQ(c.sp(1), 7);
}

TEST_F(LearnSkillTest, RejectsNonPositiveAmount) {
  CharacterInstance c = MakeCharacterWithSp(rng_, /*stage=*/1, /*sp=*/5);
  Skill skill = SlashBlast();
  EXPECT_FALSE(c.LearnSkill(skill, 0));
  EXPECT_FALSE(c.LearnSkill(skill, -2));
  EXPECT_EQ(c.skill_level(skill), 0);
  EXPECT_EQ(c.sp(1), 5);
}

TEST_F(LearnSkillTest, SpendsFromTheAdvancementsStage) {
  // Each book uses its own stage's points, and a Spearman has two.
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

// Every 1st job's skills are at stage 1, so the stage alone doesn't say whose
// book a skill is from. Without this check, a Swordman's points could buy an
// Archer's skills.
TEST_F(LearnSkillTest, RejectsAnotherJobsBook) {
  CharacterInstance c = MakeCharacterWithSp(rng_, /*stage=*/1, /*sp=*/5);
  Skill skill =
      MakeSkill("Arrow Blow", JOB_ADVANCEMENT_ARCHER, /*max_level=*/20);
  EXPECT_FALSE(c.HasAdvancement(JOB_ADVANCEMENT_ARCHER));
  EXPECT_FALSE(c.LearnSkill(skill));
  EXPECT_EQ(c.sp(1), 5);
}

// The 2nd job book isn't open to a character who hasn't taken the 2nd
// advancement, however many points they have.
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
  // With no advancement the character is at stage 0, which has no SP to spend.
  CharacterInstance c = MakeCharacterWithSp(rng_, /*stage=*/1, /*sp=*/5);
  Skill skill = MakeSkill("Nameless", JOB_ADVANCEMENT_UNSPECIFIED, 20);
  EXPECT_FALSE(c.LearnSkill(skill));
}

// --- A skill nobody buys ---

// Shaped like Blessing of the Fairy: no max_level of its own, and a level read
// from the account instead of bought with points.
Skill FairyBlessing() {
  Skill skill;
  skill.set_name("Blessing of the Fairy");
  skill.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(skill, JOB_ADVANCEMENT_BEGINNER);
  skill.set_account_levels_per_level(10);
  skill.mutable_base()->set_attack(1);
  skill.mutable_per_level()->set_attack(1);
  return skill;
}

TEST_F(CharacterTest, ADerivedSkillCapsWhereTheLevelCapDoes) {
  EXPECT_EQ(SkillMaxLevel(FairyBlessing()), kMaxLevel / 10);
  EXPECT_EQ(SkillMaxLevel(SlashBlast()), SlashBlast().max_level())
      << "every other skill keeps the maximum its data states";
}

// The level is the account's highest level divided by ten, floored. The
// character's own level counts, since the account's record is only written on
// save.
TEST_F(CharacterTest, ADerivedSkillReadsTheAccountsClimb) {
  Skill fairy = FairyBlessing();
  EXPECT_EQ(MakeCharacter(rng_).skill_level(fairy), 0)
      << "a level 1 account has not earned one";
  EXPECT_EQ(MakeCharacter(rng_, NextAdvancementLevel(0)).skill_level(fairy), 1)
      << "the Skills tab opens with the first job, and it is there at 1";
  EXPECT_EQ(MakeCharacter(rng_, /*level=*/19).skill_level(fairy), 1);

  CharacterInstance c = MakeCharacter(rng_, /*level=*/19);
  c.set_account_max_level(150);
  EXPECT_EQ(c.skill_level(fairy), 15) << "somebody else's climb pays too";

  CharacterInstance climber = MakeCharacter(rng_, /*level=*/200);
  climber.set_account_max_level(150);
  EXPECT_EQ(climber.skill_level(fairy), 20) << "and their own outruns it";
}

// Nobody buys it and no book charges for it: every character has the beginner's
// page, whatever job they took.
TEST_F(CharacterTest, ADerivedSkillCostsNothingAndIsHeldByEverybody) {
  Skill fairy = FairyBlessing();
  CharacterInstance c = MakeCharacterWithSp(rng_, /*stage=*/1, /*sp=*/5);
  EXPECT_TRUE(c.HoldsSkillFrom(fairy));
  EXPECT_EQ(c.SpFor(fairy), 0);
  EXPECT_FALSE(c.LearnSkill(fairy));
}

// --- Link skills ---

// A link skill: in no book any character holds, with a level read from how far
// the whole account has levelled one job line.
Skill LinkSkill(const std::string& name, Job line) {
  Skill skill;
  skill.set_name(name);
  skill.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(skill, JOB_ADVANCEMENT_LINK);
  skill.set_link_line(line);
  skill.set_max_level(9);
  skill.mutable_base()->set_attack(1);
  return skill;
}

// A character of a given job and level, which is all a link skill reads from
// them.
CharacterInstance MakeLinked(std::mt19937& rng, Job job, int level) {
  Character proto;
  proto.set_job(job);
  proto.set_level(level);
  return CharacterInstance(rng, std::move(proto));
}

// The character's own line's link skill is free and at level 1 from the start.
// Another line's waits until someone takes that line to 70.
TEST_F(CharacterTest, TheirOwnLineIsFreeFromTheStart) {
  Skill warrior = LinkSkill("Invincible Belief", JOB_SWORDMAN);
  Skill rogue = LinkSkill("Thief's Cunning", JOB_ROGUE);

  CharacterInstance novice = MakeLinked(rng_, JOB_SWORDMAN, 10);
  EXPECT_TRUE(novice.HoldsSkillFrom(warrior));
  EXPECT_EQ(novice.skill_level(warrior), 1) << "before any rung is paid";

  CharacterInstance beginner = MakeLinked(rng_, JOB_BEGINNER, 10);
  ASSERT_TRUE(beginner.EquipLinkSkill(rogue.name(), StatPreset::kFirst));
  EXPECT_EQ(beginner.skill_level(rogue), 0) << "no rogue has reached 70";
  LinkTally tally;
  tally.Record(JOB_ASSASSIN, 70);
  beginner.set_link_tally(tally);
  EXPECT_EQ(beginner.skill_level(rogue), 1) << "a Beginner can wear one";
  EXPECT_EQ(beginner.skill_level(warrior), 0) << "and has no line of their own";

  CharacterInstance hero = MakeLinked(rng_, JOB_HERO, 210);
  EXPECT_TRUE(hero.HoldsSkillFrom(warrior)) << "their own line, unequipped";
  EXPECT_EQ(hero.skill_level(warrior), 3);
  EXPECT_FALSE(hero.HoldsSkillFrom(rogue)) << "another line's has to be worn";
}

// The rule from the user's own example: a line counts once however many
// characters play it, and the lines of a branch add up.
TEST_F(CharacterTest, ALinkSkillReadsTheWholeRosterAndCapsAtItsMaximum) {
  Skill warrior = LinkSkill("Invincible Belief", JOB_SWORDMAN);
  CharacterInstance hero = MakeLinked(rng_, JOB_HERO, 120);

  LinkTally tally;
  tally.Record(JOB_DARK_KNIGHT, 70);
  tally.Record(JOB_DARK_KNIGHT, 210);
  hero.set_link_tally(tally);
  hero.set_account_max_level(210);
  EXPECT_EQ(hero.skill_level(warrior), 5);

  // Never past the maximum in the data, whatever the roster adds up to.
  Skill shallow = LinkSkill("Invincible Belief", JOB_SWORDMAN);
  shallow.set_max_level(4);
  EXPECT_EQ(hero.LinkSkillLevel(shallow), 4);
}

// Nobody buys them, and reconciling fills the list with every link skill the
// character doesn't already have for free.
TEST_F(CharacterTest, LinkSkillsCostNothingAndAreWornByDefault) {
  std::map<std::string, Skill> skills = {
      {"invincible_belief", LinkSkill("Invincible Belief", JOB_SWORDMAN)},
      {"thiefs_cunning", LinkSkill("Thief's Cunning", JOB_ROGUE)},
      {"slash_blast", SlashBlast()},
  };
  CharacterInstance hero =
      MakeCharacterWithSp(rng_, /*stage=*/1, /*sp=*/5, JOB_HERO);
  hero.set_account_max_level(210);

  // Every preset is filled: what the account unlocked is carried by whichever
  // preset is in use until the player changes it.
  EXPECT_EQ(hero.ReconcileLinkSkills(skills), kNumStatPresets)
      << "their own is not in it";
  for (int i = 0; i < kNumStatPresets; ++i) {
    ASSERT_EQ(hero.link_skills(StatPresetAt(i)).size(), 1);
    EXPECT_EQ(hero.link_skills(StatPresetAt(i)).at(0), "Thief's Cunning");
  }
  EXPECT_TRUE(hero.HoldsSkillFrom(skills.at("thiefs_cunning")));
  EXPECT_TRUE(
      hero.HoldsSkillFrom(skills.at("thiefs_cunning"), Activity::kBossing));
  EXPECT_EQ(hero.SpFor(skills.at("invincible_belief")), 0);
  EXPECT_FALSE(hero.LearnSkill(skills.at("invincible_belief")));
  // Idempotent: a second pass has nothing left to add.
  EXPECT_EQ(hero.ReconcileLinkSkills(skills), 0);
}

TEST_F(CharacterTest, TwelveIsAsManyLinkSkillsAsOnePresetCarries) {
  CharacterInstance c = MakeCharacter(rng_);
  for (int i = 0; i < kMaxEquippedLinkSkills; ++i) {
    EXPECT_TRUE(
        c.EquipLinkSkill("Link " + std::to_string(i), StatPreset::kFirst));
  }
  EXPECT_FALSE(c.EquipLinkSkill("Link 12", StatPreset::kFirst))
      << "the preset is full";
  EXPECT_FALSE(c.EquipLinkSkill("Link 0", StatPreset::kFirst))
      << "and never holds one twice";
  EXPECT_TRUE(c.UnequipLinkSkill("Link 0", StatPreset::kFirst));
  EXPECT_FALSE(c.UnequipLinkSkill("Link 0", StatPreset::kFirst));
  EXPECT_TRUE(c.EquipLinkSkill("Link 12", StatPreset::kFirst));
  // The presets are separate: filling the first leaves the second empty.
  EXPECT_TRUE(c.link_skills(StatPreset::kSecond).empty());
}

// The point of the preset row: which link skills a character has depends on
// what they are doing.
TEST_F(CharacterTest, EachLinkPresetCarriesItsOwnSkills) {
  Skill rogue = LinkSkill("Thief's Cunning", JOB_ROGUE);
  Skill magician = LinkSkill("Empirical Knowledge", JOB_MAGICIAN);
  CharacterInstance hero = MakeLinked(rng_, JOB_HERO, 210);
  LinkTally tally;
  tally.Record(JOB_BISHOP, 210);
  hero.set_link_tally(tally);
  hero.set_autoswap_presets(true);
  ASSERT_TRUE(hero.EquipLinkSkill(rogue.name(), StatPreset::kFirst));
  ASSERT_TRUE(hero.EquipLinkSkill(magician.name(), StatPreset::kSecond));

  EXPECT_TRUE(hero.HoldsLinkSkill(rogue, Activity::kFarming));
  EXPECT_FALSE(hero.HoldsLinkSkill(rogue, Activity::kBossing));
  EXPECT_TRUE(hero.HoldsLinkSkill(magician, Activity::kBossing));
  EXPECT_EQ(hero.skill_level(magician, Activity::kFarming), 0);
  EXPECT_EQ(hero.skill_level(magician, Activity::kBossing), 3);
  EXPECT_EQ(hero.LinkSkillLevelOffered(magician), 3)
      << "what the screen offering it shows";

  // With autoswap off, the selected slot is used whatever the character is
  // doing.
  hero.set_autoswap_presets(false);
  hero.SetSlotInUse(PresetKind::kLinkSkills, StatPreset::kSecond);
  EXPECT_TRUE(hero.HoldsLinkSkill(magician, Activity::kFarming));
  EXPECT_FALSE(hero.HoldsLinkSkill(rogue, Activity::kFarming));
}

// A swap moves the in-use marker with it, whichever end it was on, and creates
// a list the character never opened. Gear presets are never swapped.
TEST(SwapPresetsTest, TheSlotInUseMovesWithWhatItHolds) {
  std::mt19937 rng(1);
  CharacterInstance c = MakeCharacter(rng);
  c.SetSlotInUse(PresetKind::kLinkSkills, StatPreset::kFirst);
  c.SwapPresets(PresetKind::kLinkSkills, StatPreset::kFirst,
                StatPreset::kThird);
  EXPECT_EQ(c.proto().link_skills().presets_size(), kNumStatPresets);
  EXPECT_EQ(c.SlotInUse(PresetKind::kLinkSkills), StatPreset::kThird);

  c.SetSlotInUse(PresetKind::kInnerAbility, StatPreset::kSecond);
  c.SwapPresets(PresetKind::kInnerAbility, StatPreset::kFirst,
                StatPreset::kSecond);
  EXPECT_EQ(c.proto().inner_ability().presets_size(), kNumStatPresets);
  EXPECT_EQ(c.SlotInUse(PresetKind::kInnerAbility), StatPreset::kFirst);
  // If neither end is in use, the marker doesn't move.
  c.SwapPresets(PresetKind::kInnerAbility, StatPreset::kSecond,
                StatPreset::kThird);
  EXPECT_EQ(c.SlotInUse(PresetKind::kInnerAbility), StatPreset::kFirst);

  c.SetSlotInUse(PresetKind::kEquip, StatPreset::kFirst);
  c.SwapPresets(PresetKind::kEquip, StatPreset::kFirst, StatPreset::kSecond);
  EXPECT_EQ(c.SlotInUse(PresetKind::kEquip), StatPreset::kFirst);
}

// --- Advancement mapping ---

TEST(UsernameTest, StartsOnTheInvitationAndTakesAName) {
  std::mt19937 rng(1);
  CharacterInstance c = MakeCharacter(rng);
  EXPECT_EQ(c.username(), kDefaultUsername);
  c.SetUsername("Logikable");
  EXPECT_EQ(c.username(), "Logikable");
}

// Nothing should be able to clear a character's name: the panel treats an empty
// entry as "no change", and so does this.
TEST(UsernameTest, AnEmptyNameIsIgnored) {
  std::mt19937 rng(1);
  CharacterInstance c = MakeCharacter(rng);
  c.SetUsername("Logikable");
  c.SetUsername("");
  EXPECT_EQ(c.username(), "Logikable");
}

TEST(AdvancementMappingTest, FirstStageMapsToEachJobsFirstAdvancement) {
  EXPECT_EQ(AdvancementForJobStage(JOB_SWORDMAN, 1), JOB_ADVANCEMENT_SWORDMAN);
  EXPECT_EQ(AdvancementForJobStage(JOB_ARCHER, 1), JOB_ADVANCEMENT_ARCHER);
  EXPECT_EQ(AdvancementForJobStage(JOB_MAGICIAN, 1), JOB_ADVANCEMENT_MAGICIAN);
  EXPECT_EQ(AdvancementForJobStage(JOB_ROGUE, 1), JOB_ADVANCEMENT_ROGUE);
}

TEST(AdvancementMappingTest, UnreachedStageHasNoAdvancement) {
  // A Swordman is a 1st job, so it has no stage-2 advancement of its own; a
  // Beginner has none at stage 1.
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

// --- CanEquip ---

TEST_F(CanEquipTest, AWarriorWearsAWarriorSwordOfTheirLevel) {
  sword_.set_required_level(1);
  sword_.add_equip_job_categories(EQUIP_JOB_CATEGORY_WARRIOR);
  c_.AdvanceJob(JOB_SWORDMAN);
  EXPECT_TRUE(c_.CanEquip(sword_));
  sword_.set_required_level(10);
  EXPECT_FALSE(c_.CanEquip(sword_)) << "a level 1 in a level 10 sword";
}

// The category has to name the job, and an item naming no job fits nobody.
// CanEquip treats empty categories as "no job", while MeetsJob treats them as
// "any job".
TEST_F(CanEquipTest, TheCategoryHasToNameTheJob) {
  sword_.set_required_level(1);
  c_.AdvanceJob(JOB_SWORDMAN);
  EXPECT_FALSE(c_.CanEquip(sword_)) << "no category at all";
  sword_.add_equip_job_categories(EQUIP_JOB_CATEGORY_BOWMAN);
  EXPECT_FALSE(c_.CanEquip(sword_));
  sword_.add_equip_job_categories(EQUIP_JOB_CATEGORY_UNIVERSAL);
  EXPECT_TRUE(c_.CanEquip(sword_));
}

// A beginner is a job like any other here. A character who hasn't advanced has
// no job, so nothing fits them.
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

// Empty categories mean any job, unlike CanEquip. MeetsJob only answers the
// question asked, and an item with no requirement requires nothing.
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

// A secondary belongs to its own branch, not just the category. All three
// warrior secondaries are EQUIP_JOB_CATEGORY_WARRIOR with the same stats, so
// only the branch tells them apart.
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

// --- AddItem ---

// Fixture for AddItem tests. Provides c_, two Etc item prototypes (default
// max_stack 200) and a currency, to test both destinations.
class AddItemTest : public CharacterTest {
 protected:
  void SetUp() override {
    shell_.set_name("Green Snail Shell");
    other_.set_name("Blue Snail Shell");
    trace_.set_name(kSpellTraceName);
    trace_.set_kind(ITEM_KIND_SPELL_TRACE);
  }
  CharacterInstance c_ = MakeCharacter(rng_);
  ItemPrototype shell_;
  ItemPrototype other_;
  ItemPrototype trace_;
};

// A drop opens a stack, adds to that same stack next time, and leaves another
// item's stack alone.
TEST_F(AddItemTest, ADropStacksByName) {
  c_.AddItem(shell_, 5);
  c_.AddItem(shell_, 3);
  c_.AddItem(other_, 3);
  ASSERT_EQ(c_.stackables().size(), 2);
  EXPECT_EQ(c_.stackables()[0].name(), "Green Snail Shell");
  EXPECT_EQ(c_.stackables()[0].count(), 8);
  EXPECT_EQ(c_.stackables()[1].name(), "Blue Snail Shell");
  EXPECT_EQ(c_.stackables()[1].count(), 3);
}

TEST_F(AddItemTest, SplitsOverflowAtMaxStack) {
  c_.AddItem(shell_, 250);
  ASSERT_EQ(c_.stackables().size(), 2);
  EXPECT_EQ(c_.stackables()[0].count(), 200);
  EXPECT_EQ(c_.stackables()[1].count(), 50);
}

TEST_F(AddItemTest, NonPositiveCountIsNoOp) {
  c_.AddItem(shell_, 0);
  c_.AddItem(shell_, -4);
  EXPECT_TRUE(c_.stackables().empty());
}

// A currency goes to the purse rather than the bag, and has no cap: one
// balance, far above what a stack could hold, and no row on the Etc tab.
TEST_F(AddItemTest, ACurrencyIsBankedRatherThanCarried) {
  EXPECT_EQ(c_.AddItem(trace_, 1000000), 1000000);
  EXPECT_TRUE(c_.stackables().empty());
  ASSERT_EQ(c_.currencies().entries().size(), 1u);
  EXPECT_EQ(c_.currencies().Count(kSpellTraceName), 1000000);
  EXPECT_EQ(c_.CountItem(trace_), 1000000);
}

// Both kinds support counting and spending the same way, and no name is both.
TEST_F(AddItemTest, CountingAndSpendingReachEitherSide) {
  c_.AddItem(shell_, 10);
  c_.AddItem(trace_, 10);
  EXPECT_EQ(c_.CountItem("Green Snail Shell"), 10);
  EXPECT_EQ(c_.CountItem(kSpellTraceName), 10);
  EXPECT_EQ(c_.CountItem("Nothing Owned"), 0);

  EXPECT_FALSE(c_.SpendItem(kSpellTraceName, 11)) << "all or nothing";
  EXPECT_TRUE(c_.SpendItem(kSpellTraceName, 10));
  EXPECT_EQ(c_.CountItem(kSpellTraceName), 0);
  EXPECT_TRUE(c_.currencies().entries().empty()) << "no row of nothing";
  EXPECT_FALSE(c_.SpendItem(kSpellTraceName, 1)) << "and none left to spend";

  EXPECT_TRUE(c_.SpendItem("Green Snail Shell", 10));
  EXPECT_TRUE(c_.stackables().empty());
}

// Room is a limit of the bag, and currencies aren't in the bag: a full Etc tab
// still takes every spell trace offered.
TEST_F(AddItemTest, ACurrencyNeverRunsOutOfRoom) {
  for (int i = 0; i < kTabCapacity; ++i) {
    ItemPrototype filler;
    filler.set_name("Filler " + std::to_string(i));
    c_.AddItem(filler, 1);
  }
  ASSERT_EQ(c_.RoomFor(shell_), 0);
  EXPECT_EQ(c_.RoomFor(trace_), INT_MAX);
  EXPECT_EQ(c_.AddItem(trace_, 5000), 5000);
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

// Fixture with a 5000-meso weapon and a character who can afford two.
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

// Equips don't stack, so buying two at once gives two rows, not one row of two.
TEST_F(BuyTest, EachCopyIsItsOwnItem) {
  EXPECT_TRUE(c_.Buy(sword_, 2));
  EXPECT_EQ(c_.meso(), 1000);
  EXPECT_EQ(c_.inventory().size(), 2);
}

// The affordable part must not go through: a half-completed purchase would
// charge for an order the player never placed.
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

// A price of zero is different from no price: the shop gives the Master
// Adventurer medal away free, and a purchase that costs nothing still goes
// through.
TEST_F(BuyTest, SellsAFreeItemForNothing) {
  EquipPrototype medal;
  medal.set_name("Master Adventurer");
  medal.set_equip_slot(EQUIP_SLOT_MEDAL);
  medal.set_shop_price(0);
  EXPECT_TRUE(c_.Buy(medal, 1));
  EXPECT_EQ(c_.meso(), 11000);
  ASSERT_EQ(c_.inventory().size(), 1);
  EXPECT_EQ(c_.inventory()[0].name(), "Master Adventurer");
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
    token_.set_currency_mark("●");
    c_.AddItem(token_, 2);
  }

  int TokensLeft() {
    return c_.CountItem(token_);
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

// The whole order or none of it, as with meso: otherwise a player a token short
// would pay for something they never got.
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

// An Etc item isn't a currency just because it was passed as one. The currency
// mark is what lets the shop price in it.
TEST_F(BuyWithTokenTest, RefusesAnItemThatIsNotACurrency) {
  ItemPrototype horn;
  horn.set_name("Beetle's Horn");
  c_.AddItem(horn, 50);

  EXPECT_FALSE(c_.BuyWithToken(polearm_, horn, 1));
  EXPECT_EQ(c_.CountItem(horn), 50);
  EXPECT_EQ(c_.inventory().size(), 0);
}

// --- Buy, stackable ---

class BuyStackableTest : public CharacterTest {
 protected:
  void SetUp() override {
    // The shop's stackable, which is a currency, so what it buys goes to the
    // purse rather than a tab.
    trace_.set_name(kSpellTraceName);
    trace_.set_kind(ITEM_KIND_SPELL_TRACE);
    trace_.set_shop_price(5000);
    drop_.set_name("Green Snail Shell");
    drop_.set_shop_price(5000);
    c_.AddMeso(50000);
  }

  ItemPrototype trace_;
  ItemPrototype drop_;
  CharacterInstance c_ = MakeCharacter(rng_);
};

// Ten of a stackable is one balance of ten or one row of ten, not ten rows,
// unlike buying ten swords.
TEST_F(BuyStackableTest, TakesTheMesoAndStacksTheCopies) {
  EXPECT_TRUE(c_.Buy(trace_, 10));
  EXPECT_EQ(c_.meso(), 0);
  EXPECT_TRUE(c_.stackables().empty()) << "a currency is not carried";
  EXPECT_EQ(c_.CountItem(trace_), 10);

  c_.AddMeso(50000);
  EXPECT_TRUE(c_.Buy(drop_, 10));
  ASSERT_EQ(c_.stackables().size(), 1);
  EXPECT_EQ(c_.stackables()[0].count(), 10);
}

TEST_F(BuyStackableTest, BuysNothingWhenItCannotBuyEverything) {
  EXPECT_FALSE(c_.Buy(trace_, 11));
  EXPECT_EQ(c_.meso(), 50000);
  EXPECT_EQ(c_.CountItem(trace_), 0);
}

TEST_F(BuyStackableTest, WillNotSellWhatTheShopDoesNotStock) {
  ItemPrototype unpriced;
  unpriced.set_name("Snail Shell");
  EXPECT_FALSE(c_.Buy(unpriced, 1));
  EXPECT_EQ(c_.meso(), 50000);
  EXPECT_TRUE(c_.stackables().empty());
}

TEST_F(BuyStackableTest, NonPositiveCountIsNoOp) {
  EXPECT_FALSE(c_.Buy(trace_, 0));
  EXPECT_FALSE(c_.Buy(trace_, -2));
  EXPECT_EQ(c_.meso(), 50000);
}

// Bag space is checked before meso, so an order too big for the bag takes
// nothing, as with equips.
TEST_F(BuyStackableTest, BuysNothingWhenTheBagCannotHoldItAll) {
  // Far more than a full bag costs, so the bag refuses, not the meso check.
  c_.AddMeso(1000000000000LL);
  int room = c_.RoomFor(drop_);
  EXPECT_FALSE(c_.Buy(drop_, room + 1));
  EXPECT_TRUE(c_.stackables().empty());
  EXPECT_TRUE(c_.Buy(drop_, room));
}

// --- SellStackable ---

// Fixture with an Etc item worth 7 meso each and one worth nothing.
class SellStackableTest : public CharacterTest {
 protected:
  void SetUp() override {
    shell_.set_name("Green Snail Shell");
    shell_.set_sell_price(7);
    junk_.set_name("Worthless Junk");
  }
  CharacterInstance c_ = MakeCharacter(rng_);
  ItemPrototype shell_;
  ItemPrototype junk_;
};

TEST_F(SellStackableTest, SellsCopiesAndCreditsMeso) {
  c_.AddItem(shell_, 10);
  EXPECT_EQ(c_.SellStackable(0, 4), 28);  // 4 * 7
  ASSERT_EQ(c_.stackables().size(), 1);
  EXPECT_EQ(c_.stackables()[0].count(), 6);
  EXPECT_EQ(c_.meso(), 28);
}

TEST_F(SellStackableTest, SellingWholeStackRemovesIt) {
  c_.AddItem(shell_, 5);
  EXPECT_EQ(c_.SellStackable(0, 5), 35);
  EXPECT_TRUE(c_.stackables().empty());
  EXPECT_EQ(c_.meso(), 35);
}

TEST_F(SellStackableTest, ClampsCountToStackSize) {
  c_.AddItem(shell_, 3);
  EXPECT_EQ(c_.SellStackable(0, 10), 21);  // only 3 exist
  EXPECT_TRUE(c_.stackables().empty());
  EXPECT_EQ(c_.meso(), 21);
}

// A count of zero or less, or an index past the end of the tab, takes nothing
// and pays nothing.
TEST_F(SellStackableTest, ANonPositiveCountOrAnIndexOffTheEndIsNoOp) {
  c_.AddItem(shell_, 5);
  EXPECT_EQ(c_.SellStackable(0, 0), 0);
  EXPECT_EQ(c_.SellStackable(0, -2), 0);
  EXPECT_EQ(c_.SellStackable(3, 1), 0);
  EXPECT_EQ(c_.SellStackable(-1, 1), 0);
  EXPECT_EQ(c_.stackables()[0].count(), 5);
  EXPECT_EQ(c_.meso(), 0);
}

// Zero is a price, not a refusal: the copies are removed and pay nothing, which
// is how a worthless stack is thrown away.
TEST_F(SellStackableTest, AWorthlessItemStillSells) {
  c_.AddItem(junk_, 5);
  EXPECT_EQ(c_.SellStackable(0, 3), 0);
  EXPECT_EQ(c_.stackables()[0].count(), 2);
  EXPECT_EQ(c_.SellStackable(0, 2), 0);
  EXPECT_TRUE(c_.stackables().empty());
  EXPECT_EQ(c_.meso(), 0);
}

// --- SellEquip ---

// Fixture with a sword worth 900 meso and a worthless one, so both cases of
// "zero is not a refusal" can be told apart.
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

// Like a stackable, a price of zero still sells rather than refusing. That is
// how the starter sword can leave the bag.
TEST_F(SellEquipTest, WorthlessItemStillGoes) {
  c_.PickUp(std::make_unique<EquipInstance>(starter_));
  EXPECT_EQ(c_.SellEquip(0), 0);
  EXPECT_EQ(c_.inventory().size(), 0);
  EXPECT_EQ(c_.meso(), 0);
}

// Scrolls and stars go in but never come back out. Selling a scrolled item for
// more than the base item would turn spell traces, which cost meso, into a way
// to make meso.
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

// A trace records a destroyed item and isn't a copy of it, so it sells for what
// the record is worth, however valuable the item was.
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

// The shelf returns items, so it needs the same catalogs a save does, keyed by
// data-file name rather than display name for the same reason.
class BuyBackTest : public CharacterTest {
 protected:
  void SetUp() override {
    sword_.set_name("Sword");
    sword_.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
    sword_.set_upgrade_slots(7);
    sword_.set_sell_price(900);
    equips_["sword"] = sword_;
    shell_.set_name("Green Snail Shell");
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
  c_.AddItem(shell_, 10);
  c_.SellStackable(0, 4);
  ASSERT_EQ(c_.buy_backs().size(), 1);
  EXPECT_TRUE(c_.buy_backs().Get(0).has_stack());
  EXPECT_EQ(c_.buy_backs().Get(0).stack().name(), "Green Snail Shell");
  EXPECT_EQ(c_.buy_backs().Get(0).stack().count(), 4);
  EXPECT_EQ(c_.buy_backs().Get(0).unit_price(), 7);
}

// The point of the shelf: the sale price didn't pay for the stars, so buying
// the item back must not charge for them again.
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

// A trace is worth nothing both ways, and must come back as a trace. Coming
// back as a live item would undo a star force destruction for free.
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
  c_.AddItem(shell_, 100);
  c_.SellStackable(0, 100);
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  c_.SellEquip(0);  // a newer row, so the shelf has an order to keep
  ASSERT_EQ(c_.buy_backs().size(), 2);

  ASSERT_TRUE(BuyBack(1, 30));
  EXPECT_EQ(c_.CountItem(shell_), 30);
  ASSERT_EQ(c_.buy_backs().size(), 2) << "the row stays until it empties";
  EXPECT_EQ(c_.buy_backs().Get(1).stack().count(), 70);
  EXPECT_TRUE(c_.buy_backs().Get(0).has_equip()) << "and stays where it was";

  ASSERT_TRUE(BuyBack(1, 70));
  EXPECT_EQ(c_.CountItem(shell_), 100);
  EXPECT_EQ(c_.buy_backs().size(), 1);
}

// One row per sale, not per item: two sales of the same thing make two rows,
// with the newer one on top.
TEST_F(BuyBackTest, EachSaleIsItsOwnRowNewestFirst) {
  c_.AddItem(shell_, 300);
  c_.SellStackable(0, 200);
  c_.SellStackable(0, 100);
  ASSERT_EQ(c_.buy_backs().size(), 2);
  EXPECT_EQ(c_.buy_backs().Get(0).stack().count(), 100)
      << "the later sale on top";
  EXPECT_EQ(c_.buy_backs().Get(1).stack().count(), 200);
}

TEST_F(BuyBackTest, TheOldestRowFallsOffAFullShelf) {
  c_.AddItem(shell_, 1000);
  for (int i = 0; i < kBuyBackSlots + 3; ++i) {
    c_.AddItem(shell_, i + 1);
    c_.SellStackable(0, i + 1);
  }
  ASSERT_EQ(c_.buy_backs().size(), kBuyBackSlots);
  // The last sale is on top, and the three oldest have dropped off the bottom.
  EXPECT_EQ(c_.buy_backs().Get(0).stack().count(), kBuyBackSlots + 3);
  EXPECT_EQ(c_.buy_backs().Get(kBuyBackSlots - 1).stack().count(), 4);
}

TEST_F(BuyBackTest, RefusesWhatTheCharacterCannotPayFor) {
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  c_.SellEquip(0);
  // The meso has been spent since, which the shelf must handle: the row is
  // still there but the money isn't.
  shell_.set_shop_price(1);
  ASSERT_TRUE(c_.Buy(shell_, 900));
  ASSERT_EQ(c_.meso(), 0);

  EXPECT_FALSE(BuyBack(0));
  EXPECT_EQ(c_.meso(), 0);
  EXPECT_EQ(c_.inventory().size(), 0);
  EXPECT_EQ(c_.buy_backs().size(), 1) << "still there to come back for";
}

TEST_F(BuyBackTest, RefusesAStackTheBagHasNoRoomFor) {
  c_.AddItem(shell_, 10);
  c_.SellStackable(0, 10);
  c_.AddMeso(1000);
  int64_t meso = c_.meso();
  // Every Etc slot is filled with something else, so adding to a stack can't
  // help.
  ItemPrototype filler;
  for (int i = 0; i < kTabCapacity; ++i) {
    filler.set_name("Filler" + std::to_string(i));
    c_.AddItem(filler, 1);
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
  EXPECT_EQ(c_.equipped().at(EQUIP_SLOT_PRIMARY_WEAPON)->prototype().name(),
            "Sword");
}

// Armour goes in four more slots rather than replacing the weapon: each piece
// goes in its own slot and none displaces another.
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
  EXPECT_EQ(c_.equipped().at(EQUIP_SLOT_PRIMARY_WEAPON)->prototype().name(),
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

// A ring names one slot family and goes in the first free slot of four, so a
// character can wear four different rings without displacing any.
TEST_F(EquipTest, FourRingsWearAtOnce) {
  const char* kNames[] = {"Ring A", "Ring B", "Ring C", "Ring D"};
  for (const char* name : kNames) {
    EquipPrototype ring;
    ring.set_name(name);
    ring.set_equip_slot(EQUIP_SLOT_RING);
    ring.mutable_base_stats()->set_str(10);
    c_.PickUp(std::make_unique<EquipInstance>(ring));
    ASSERT_TRUE(c_.Equip(0)) << name;
  }
  EXPECT_EQ(c_.equipped().size(), 4);
  EXPECT_EQ(c_.equip_stats().str(), 40) << "every ring counts";
  EXPECT_EQ(c_.equipped().at(EQUIP_SLOT_RING)->prototype().name(), "Ring A");
  EXPECT_EQ(c_.equipped().at(EQUIP_SLOT_RING_4)->prototype().name(), "Ring D");
}

// Two pendant slots, filled the same way and no more: a fourth pendant has
// nowhere new to go.
TEST_F(EquipTest, TwoPendantsWearAtOnce) {
  EquipPrototype first;
  first.set_name("Pendant A");
  first.set_equip_slot(EQUIP_SLOT_PENDANT);
  EquipPrototype second = first;
  second.set_name("Pendant B");
  c_.PickUp(std::make_unique<EquipInstance>(first));
  c_.PickUp(std::make_unique<EquipInstance>(second));
  ASSERT_TRUE(c_.Equip(0));
  ASSERT_TRUE(c_.Equip(0));
  EXPECT_EQ(c_.equipped().at(EQUIP_SLOT_PENDANT)->prototype().name(),
            "Pendant A");
  EXPECT_EQ(c_.equipped().at(EQUIP_SLOT_PENDANT_2)->prototype().name(),
            "Pendant B");
}

// No two of the four rings may be the same ring, so a second copy replaces the
// worn one instead of joining it. That is how a better-starred copy goes on.
// Pendants do the same.
TEST_F(EquipTest, TheSameRingSwapsForTheWornOne) {
  c_.AdvanceJob(JOB_BEGINNER);
  EquipPrototype ring;
  ring.set_name("Silver Blossom Ring");
  ring.set_equip_slot(EQUIP_SLOT_RING);
  ring.add_equip_job_categories(EQUIP_JOB_CATEGORY_UNIVERSAL);
  Equip starred;
  starred.set_stars(5);
  c_.PickUp(std::make_unique<EquipInstance>(ring));
  c_.PickUp(std::make_unique<EquipInstance>(ring, starred));
  ASSERT_TRUE(c_.Equip(0));
  EXPECT_EQ(c_.SlotToFill(ring), EQUIP_SLOT_RING) << "the slot it is worn in";
  ASSERT_TRUE(c_.Equip(0));
  EXPECT_EQ(c_.equipped().size(), 1);
  EXPECT_EQ(c_.equipped().at(EQUIP_SLOT_RING)->equip_state().stars(), 5);
  ASSERT_EQ(c_.inventory().size(), 1) << "the first copy takes its place";
  EXPECT_EQ(c_.inventory()[0].equip_state().stars(), 0);

  EquipPrototype pendant;
  pendant.set_name("Dominator Pendant");
  pendant.set_equip_slot(EQUIP_SLOT_PENDANT);
  c_.PickUp(std::make_unique<EquipInstance>(pendant));
  c_.PickUp(std::make_unique<EquipInstance>(pendant, starred));
  ASSERT_TRUE(c_.Equip(1));
  ASSERT_TRUE(c_.Equip(1));
  EXPECT_EQ(c_.equipped().count(EQUIP_SLOT_PENDANT_2), 0u);
  EXPECT_EQ(c_.equipped().at(EQUIP_SLOT_PENDANT)->equip_state().stars(), 5);
}

// A preset also swaps a ring it inherits: the copy becomes its own item over
// the first preset's, which keeps wearing its own.
TEST_F(EquipTest, ASecondPresetSwapsForARingItInherits) {
  EquipPrototype ring;
  ring.set_name("Silver Blossom Ring");
  ring.set_equip_slot(EQUIP_SLOT_RING);
  Equip starred;
  starred.set_stars(5);
  c_.PickUp(std::make_unique<EquipInstance>(ring));
  c_.PickUp(std::make_unique<EquipInstance>(ring, starred));
  ASSERT_TRUE(c_.Equip(0));
  ASSERT_TRUE(c_.Equip(0, StatPreset::kSecond));
  EXPECT_EQ(c_.equipped(StatPreset::kSecond).size(), 1)
      << "its own, not beside the one it inherited";
  EXPECT_EQ(c_.equipped(StatPreset::kSecond)
                .at(EQUIP_SLOT_RING)
                ->equip_state()
                .stars(),
            5);
  EXPECT_EQ(c_.equipped().at(EQUIP_SLOT_RING)->equip_state().stars(), 0);
  EXPECT_TRUE(c_.inventory().empty()) << "an inherited item is not displaced";
}

// The rule holds however the two copies got there: a preset that owns one
// leaves the slot where the other would be inherited empty, rather than showing
// the same ring twice.
TEST_F(EquipTest, NoPresetShowsOneRingTwice) {
  EquipPrototype ring;
  ring.set_equip_slot(EQUIP_SLOT_RING);
  EquipPrototype other = ring;
  ring.set_name("Silver Blossom Ring");
  other.set_name("Gold Blossom Ring");
  c_.PickUp(std::make_unique<EquipInstance>(other));
  c_.PickUp(std::make_unique<EquipInstance>(ring));
  c_.PickUp(std::make_unique<EquipInstance>(other));
  c_.PickUp(std::make_unique<EquipInstance>(ring));
  ASSERT_TRUE(c_.Equip(0));                       // Gold, first preset, ring 1
  ASSERT_TRUE(c_.Equip(0, StatPreset::kSecond));  // Silver, its own ring 2
  ASSERT_TRUE(c_.Equip(0));                       // Gold, first preset, ring 2
  ASSERT_TRUE(c_.Equip(0));                       // Silver, first preset ring 3
  const WornGear& boss = c_.equipped(StatPreset::kSecond);
  EXPECT_EQ(boss.count(EQUIP_SLOT_RING_3), 0u) << "its own Silver is ring 2";
  EXPECT_EQ(boss.at(EQUIP_SLOT_RING_2)->prototype().name(),
            "Silver Blossom Ring");
  EXPECT_EQ(boss.size(), 2);
}

// A one-slot family is exempt: putting on a second hat is the normal swap, and
// the first goes to the bag position the second left.
TEST_F(EquipTest, TheSameHatStillSwaps) {
  EquipPrototype hat;
  hat.set_name("Frozen Hat");
  hat.set_equip_slot(EQUIP_SLOT_HAT);
  c_.PickUp(std::make_unique<EquipInstance>(hat));
  c_.PickUp(std::make_unique<EquipInstance>(hat));
  ASSERT_TRUE(c_.Equip(0));
  EXPECT_TRUE(c_.Equip(0));
  EXPECT_EQ(c_.equipped().size(), 1);
  EXPECT_EQ(c_.inventory().size(), 1);
}

// With every ring slot full, a fifth ring replaces the first, which goes to the
// bag position the new one left.
TEST_F(EquipTest, AFifthRingDisplacesTheFirst) {
  for (int i = 0; i < 5; ++i) {
    EquipPrototype ring;
    ring.set_name("Ring " + std::to_string(i));
    ring.set_equip_slot(EQUIP_SLOT_RING);
    c_.PickUp(std::make_unique<EquipInstance>(ring));
  }
  for (int i = 0; i < 5; ++i) {
    ASSERT_TRUE(c_.Equip(0)) << "ring " << i;
  }
  EXPECT_EQ(c_.equipped().size(), 4);
  EXPECT_EQ(c_.equipped().at(EQUIP_SLOT_RING)->prototype().name(), "Ring 4");
  EXPECT_EQ(c_.equipped().at(EQUIP_SLOT_RING_2)->prototype().name(), "Ring 1");
  ASSERT_EQ(c_.inventory().size(), 1);
  EXPECT_EQ(c_.inventory()[0].prototype().name(), "Ring 0");
}

// Each of the four ring slots can be unequipped, scrolled and starred on its
// own, so a ring in the third slot is reached without touching the others.
TEST_F(EquipTest, EachRingSlotIsUnequippedOnItsOwn) {
  for (int i = 0; i < 3; ++i) {
    EquipPrototype ring;
    ring.set_name("Ring " + std::to_string(i));
    ring.set_equip_slot(EQUIP_SLOT_RING);
    c_.PickUp(std::make_unique<EquipInstance>(ring));
    ASSERT_TRUE(c_.Equip(0));
  }
  EXPECT_TRUE(c_.Unequip(EQUIP_SLOT_RING_3));
  EXPECT_EQ(c_.equipped().size(), 2);
  EXPECT_EQ(c_.equipped().count(EQUIP_SLOT_RING_3), 0u);
  EXPECT_FALSE(c_.Unequip(EQUIP_SLOT_RING_4)) << "nothing was ever in it";
  // The freed slot is filled next, ahead of the empty fourth.
  c_.PickUp(std::make_unique<EquipInstance>(c_.inventory()[0].prototype()));
  ASSERT_TRUE(c_.Equip(0));
  EXPECT_EQ(c_.equipped().count(EQUIP_SLOT_RING_3), 1u);
}

TEST_F(EquipTest, RefusesAnEmptyIndexOrAnItemWithNoSlot) {
  EXPECT_FALSE(c_.Equip(0));  // nothing in the bag
  EquipPrototype proto;
  proto.set_name("Unknown");
  // equip_slot intentionally left unspecified
  c_.PickUp(std::make_unique<EquipInstance>(proto));
  EXPECT_FALSE(c_.Equip(0));
}

// --- Wearing ---

class WearingTest : public CharacterEquipFixture {};

// The copy wears the piece and the character doesn't, which is all pricing a
// shop item needs.
TEST_F(WearingTest, PutsThePieceOnACopyAndLeavesTheCharacterAlone) {
  sword_.mutable_base_stats()->set_str(50);
  EquipInstance blade(sword_);
  c_.PickUp(std::make_unique<EquipInstance>(sword_));

  CharacterInstance probe = c_.Wearing(blade, StatPreset::kFirst);
  EXPECT_EQ(probe.equip_stats(StatPreset::kFirst).str(), 50);
  EXPECT_EQ(c_.equip_stats(StatPreset::kFirst).str(), 0)
      << "nothing reached the character";
  EXPECT_EQ(c_.inventory().size(), 1);
}

// It replaces rather than adds: what the slot held is what the piece displaces,
// so counting both would count the gain twice.
TEST_F(WearingTest, ReplacesWhatTheSlotHolds) {
  sword_.mutable_base_stats()->set_str(10);
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  ASSERT_TRUE(c_.Equip(0));

  EquipPrototype axe = sword_;
  axe.set_name("Axe");
  axe.mutable_base_stats()->set_str(40);
  EquipInstance better(axe);
  EXPECT_EQ(c_.Wearing(better, StatPreset::kFirst)
                .equip_stats(StatPreset::kFirst)
                .str(),
            40);
}

// The account's data is copied too: link skills and Blessing of the Fairy are
// read from the copy, and without them every piece would look like a loss.
TEST_F(WearingTest, KeepsWhatTheAccountMirrorsIn) {
  LinkTally tally;
  tally.Record(JOB_DARK_KNIGHT, 210);
  c_.set_link_tally(tally);
  c_.set_account_max_level(210);
  c_.set_link_skills_off(true);
  EquipInstance blade(sword_);

  CharacterInstance probe = c_.Wearing(blade, StatPreset::kFirst);
  ASSERT_GT(c_.link_tally().LevelFor(JOB_SWORDMAN), 0);
  EXPECT_EQ(probe.link_tally().LevelFor(JOB_SWORDMAN),
            c_.link_tally().LevelFor(JOB_SWORDMAN));
  EXPECT_EQ(probe.account_max_level(), 210);
  EXPECT_TRUE(probe.link_skills_off()) << "the switch came too";
}

// A piece with no slot this character can fill has nothing to price, and the
// copy comes back unchanged.
TEST_F(WearingTest, APieceWithNowhereToGoChangesNothing) {
  EquipPrototype nowhere;
  nowhere.set_name("Nowhere");
  EquipInstance item(nowhere);
  EXPECT_EQ(c_.Wearing(item, StatPreset::kFirst)
                .equip_stats(StatPreset::kFirst)
                .str(),
            c_.equip_stats(StatPreset::kFirst).str());
}

// Each preset is priced separately: a piece put into the second leaves the
// first wearing what it wore.
TEST_F(WearingTest, PricesThePresetItIsAsked) {
  sword_.mutable_base_stats()->set_str(50);
  EquipInstance blade(sword_);
  CharacterInstance probe = c_.Wearing(blade, StatPreset::kSecond);
  EXPECT_EQ(probe.equip_stats(StatPreset::kSecond).str(), 50);
  EXPECT_EQ(probe.equip_stats(StatPreset::kFirst).str(), 0);
}

// A named slot is used instead of the one Equip would pick, so a ring can be
// compared against each of the four the character wears.
TEST_F(WearingTest, PricesTheSlotItIsNamed) {
  EquipPrototype ring;
  ring.set_name("Ring");
  ring.set_equip_slot(EQUIP_SLOT_RING);
  ring.mutable_base_stats()->set_str(10);
  c_.PickUp(std::make_unique<EquipInstance>(ring));
  ASSERT_TRUE(c_.Equip(0));

  EquipPrototype better = ring;
  better.set_name("Better Ring");
  better.mutable_base_stats()->set_str(40);
  EquipInstance item(better);
  // In the worn ring's slot it displaces that ring; in a free slot it doesn't.
  EXPECT_EQ(c_.Wearing(item, StatPreset::kFirst, EQUIP_SLOT_RING)
                .equip_stats(StatPreset::kFirst)
                .str(),
            40);
  EXPECT_EQ(c_.Wearing(item, StatPreset::kFirst, EQUIP_SLOT_RING_3)
                .equip_stats(StatPreset::kFirst)
                .str(),
            50);
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

TEST_F(UnequipTest, RefusesAnUnnamedOrAnEmptySlot) {
  EXPECT_FALSE(c_.Unequip(EQUIP_SLOT_UNSPECIFIED));
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
                ->equip_state()
                .scroll_stats()
                .attack(),
            5);
  EXPECT_EQ(c_.equipped()
                .at(EQUIP_SLOT_PRIMARY_WEAPON)
                ->equip_state()
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

// --- Equip presets ---

// A preset other than the first wears what the first does until it is given an
// item of its own, and that item is its own.
class EquipPresetTest : public CharacterEquipFixture {
 protected:
  EquipPrototype Blade(const std::string& name, int attack) {
    EquipPrototype proto;
    proto.set_name(name);
    proto.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
    proto.mutable_base_stats()->set_attack(attack);
    return proto;
  }
  std::string WeaponIn(StatPreset preset) {
    const EquipInstance* worn = c_.WornAt(preset, EQUIP_SLOT_PRIMARY_WEAPON);
    return worn == nullptr ? "" : worn->prototype().name();
  }
};

TEST_F(EquipPresetTest, EveryPresetWearsWhatTheFirstOneDoes) {
  c_.PickUp(std::make_unique<EquipInstance>(Blade("Farm Sword", 15)));
  ASSERT_TRUE(c_.Equip(0));

  EXPECT_EQ(WeaponIn(StatPreset::kSecond), "Farm Sword");
  EXPECT_EQ(WeaponIn(StatPreset::kThird), "Farm Sword");
  EXPECT_EQ(c_.equip_stats(StatPreset::kThird).attack(), 15);
  EXPECT_TRUE(c_.InheritsSlot(StatPreset::kSecond, EQUIP_SLOT_PRIMARY_WEAPON));
  EXPECT_FALSE(c_.InheritsSlot(StatPreset::kFirst, EQUIP_SLOT_PRIMARY_WEAPON));
}

TEST_F(EquipPresetTest, EquippingIntoOnePresetLeavesTheOthersAlone) {
  c_.PickUp(std::make_unique<EquipInstance>(Blade("Farm Sword", 15)));
  ASSERT_TRUE(c_.Equip(0));
  c_.PickUp(std::make_unique<EquipInstance>(Blade("Boss Sword", 40)));
  ASSERT_TRUE(c_.Equip(0, StatPreset::kSecond));

  EXPECT_EQ(WeaponIn(StatPreset::kFirst), "Farm Sword");
  EXPECT_EQ(WeaponIn(StatPreset::kSecond), "Boss Sword");
  EXPECT_EQ(WeaponIn(StatPreset::kThird), "Farm Sword")
      << "the third inherits the first, not the second";
  EXPECT_EQ(c_.equip_stats(StatPreset::kFirst).attack(), 15);
  EXPECT_EQ(c_.equip_stats(StatPreset::kSecond).attack(), 40);
  EXPECT_TRUE(c_.inventory().empty()) << "nothing was displaced";
}

TEST_F(EquipPresetTest, APresetOnlyTakesOffWhatIsItsOwn) {
  c_.PickUp(std::make_unique<EquipInstance>(Blade("Farm Sword", 15)));
  ASSERT_TRUE(c_.Equip(0));
  c_.PickUp(std::make_unique<EquipInstance>(Blade("Boss Sword", 40)));
  ASSERT_TRUE(c_.Equip(0, StatPreset::kSecond));

  EXPECT_FALSE(c_.Unequip(EQUIP_SLOT_PRIMARY_WEAPON, StatPreset::kThird))
      << "it wears nothing of its own there";
  ASSERT_TRUE(c_.Unequip(EQUIP_SLOT_PRIMARY_WEAPON, StatPreset::kSecond));
  EXPECT_EQ(WeaponIn(StatPreset::kSecond), "Farm Sword")
      << "back to inheriting";
  EXPECT_EQ(c_.inventory().size(), 1);
}

// It is one item, whichever preset upgrades it: a piece the second preset
// inherits belongs to the first, and scrolling it there changes both.
TEST_F(EquipPresetTest, UpgradingAnInheritedItemMovesEveryPresetWearingIt) {
  sword_.set_upgrade_slots(3);
  sword_.mutable_base_stats()->set_attack(15);
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  ASSERT_TRUE(c_.Equip(0));

  Scroll scroll;
  scroll.set_success_rate(100);
  scroll.mutable_stats()->set_attack(7);
  EXPECT_EQ(
      c_.ScrollEquipped(EQUIP_SLOT_PRIMARY_WEAPON, scroll, StatPreset::kSecond),
      kScrollSuccess);

  EXPECT_EQ(c_.equip_stats(StatPreset::kFirst).attack(), 22);
  EXPECT_EQ(c_.equip_stats(StatPreset::kSecond).attack(), 22);
}

// A copy kept for another preset still counts as owned, which the shop checks
// before selling a second.
TEST_F(EquipPresetTest, WhatAnotherPresetWearsCountsAsOwned) {
  c_.PickUp(std::make_unique<EquipInstance>(Blade("Boss Sword", 40)));
  ASSERT_TRUE(c_.Equip(0, StatPreset::kSecond));
  EXPECT_EQ(c_.CountOwned(Blade("Boss Sword", 40)), 1);
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

// GMS charges for the attempt, not the star, which is why the top of the ladder
// is so expensive. Both entry points charge, and neither rolls without paying.
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

// --- Golden Hammer ---

class HammerTest : public CharacterEquipFixture {};

TEST_F(HammerTest, AHammerBuysASlotWhereverTheItemIs) {
  c_.AddMeso(3 * kGoldenHammerCost);
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  ASSERT_TRUE(c_.HammerInventory(0));
  EXPECT_EQ(c_.inventory()[0].equip_state().hammers(), 1);
  EXPECT_EQ(c_.inventory()[0].equip_state().remaining_upgrade_slots(), 8);
  EXPECT_EQ(c_.meso(), 2 * kGoldenHammerCost);

  ASSERT_TRUE(c_.Equip(0));
  ASSERT_TRUE(c_.HammerEquipped(EQUIP_SLOT_PRIMARY_WEAPON));
  const EquipInstance& worn = *c_.equipped().at(EQUIP_SLOT_PRIMARY_WEAPON);
  EXPECT_EQ(worn.equip_state().hammers(), 2);
  EXPECT_EQ(worn.equip_state().remaining_upgrade_slots(), 9);
  EXPECT_EQ(c_.meso(), kGoldenHammerCost);
}

TEST_F(HammerTest, AHammerThatWillNotGoInIsNotCharged) {
  c_.AddMeso(9 * kGoldenHammerCost);
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  ASSERT_TRUE(c_.HammerInventory(0));
  ASSERT_TRUE(c_.HammerInventory(0));
  int64_t spent = c_.meso();

  EXPECT_FALSE(c_.HammerInventory(0)) << "a third hammer";
  EXPECT_FALSE(c_.HammerInventory(4)) << "nothing at that index";
  EXPECT_FALSE(c_.HammerEquipped(EQUIP_SLOT_HAT)) << "an empty slot";
  EXPECT_EQ(c_.meso(), spent);
}

TEST_F(HammerTest, APurseThatCannotCoverItBuysNothing) {
  c_.AddMeso(kGoldenHammerCost - 1);
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  EXPECT_FALSE(c_.HammerInventory(0));
  EXPECT_EQ(c_.meso(), kGoldenHammerCost - 1);
  EXPECT_EQ(c_.inventory()[0].equip_state().hammers(), 0);
}

// --- StarForce traces ---

class StarForceTraceTest : public CharacterTest {
 protected:
  void SetUp() override {
    proto_.set_name("Sword");
    proto_.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
    proto_.set_required_level(138);
  }

  // A character with one 19-star Sword, starred until it was destroyed. 19
  // stars is past the safeguard, so a destroy happens well within 100 tries.
  CharacterInstance WithDestroyedItem(bool equipped) {
    Equip state;
    state.set_stars(19);
    CharacterInstance c = MakeRichCharacter(rng_);
    c.PickUp(std::make_unique<EquipInstance>(proto_, state));
    if (equipped) {
      c.Equip(0);
    }
    for (int i = 0; i < 100; ++i) {
      StarForceOutcome result =
          equipped ? c.StarForceEquipped(EQUIP_SLOT_PRIMARY_WEAPON)
                   : c.StarForceInventory(0);
      if (result == kStarForceDestroy) {
        return c;
      }
    }
    ADD_FAILURE() << "100 attempts at 19 stars destroyed nothing";
    return c;
  }

  EquipPrototype proto_;
};

TEST_F(StarForceTraceTest, NoTracesInitially) {
  CharacterInstance c = MakeCharacter(rng_);
  EXPECT_TRUE(c.traces().empty());
}

TEST_F(StarForceTraceTest, ADestroyedItemLeavesATraceWhereverItWas) {
  CharacterInstance equipped = WithDestroyedItem(true);
  ASSERT_EQ(equipped.traces().size(), 1u);
  EXPECT_EQ(equipped.traces()[0]->prototype().name(), "Sword");
  EXPECT_GE(equipped.traces()[0]->equip_state().stars(), 19);

  CharacterInstance bagged = WithDestroyedItem(false);
  ASSERT_EQ(bagged.traces().size(), 1u);
  EXPECT_EQ(bagged.traces()[0]->prototype().name(), "Sword");
}

// A trace is a record, not an item: it stays in the bag and refuses every
// upgrade the original equip could have taken.
TEST_F(StarForceTraceTest, ATraceRefusesEveryUpgradeAndCannotBeWorn) {
  CharacterInstance c = WithDestroyedItem(false);
  ASSERT_EQ(c.inventory().size(), 1);

  EXPECT_FALSE(c.Equip(0)) << "a trace is not an EquipInstance";
  Scroll scroll;
  scroll.set_success_rate(100);
  EXPECT_EQ(c.ScrollInventory(0, scroll), kScrollFail);
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
// counted and 10 means it didn't.
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
  // A new character holding both, since the test is about the pairing, not
  // about one character over time.
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

  // Still worn (a thief may carry stars while holding a dagger), but nothing in
  // hand uses them, so their attack doesn't count. The two arrow types are
  // unrelated to each other, just as stars are to both.
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

  // Swapping the claw for a dagger must re-check the stars, not just subtract
  // the claw.
  c_.PickUp(std::make_unique<EquipInstance>(Weapon(EQUIP_TYPE_DAGGER)));
  ASSERT_TRUE(c_.Equip(0));
  EXPECT_EQ(c_.equip_stats().attack(), 10);
}

// --- capacity ---

// Fixture for the 128-slot tab limit, with an Etc item (default max_stack 200)
// and a plain equip to fill tabs with.
class CapacityTest : public CharacterTest {
 protected:
  void SetUp() override {
    shell_.set_name("Green Snail Shell");
    other_.set_name("Blue Snail Shell");
    sword_.set_name("Sword");
    sword_.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
    sword_.set_shop_price(10);
  }
  // Opens `count` distinct Etc stacks, so the tab fills by slots rather than
  // one item stacking up.
  void OpenDistinctStacks(int count) {
    for (int i = 0; i < count; ++i) {
      ItemPrototype proto;
      proto.set_name("Junk " + std::to_string(i));
      c_.AddItem(proto, 1);
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

// Traces are on the equip tab too, so they take slots like anything else.
TEST_F(CapacityTest, RoomForAnEquipCountsTracesAsWell) {
  c_.PickUp(std::make_unique<EquipTrace>(sword_, Equip()));
  EXPECT_EQ(c_.RoomFor(sword_), kTabCapacity - 1);
}

// --- CountOwned ---

// Worn items are still owned. A player looking at the shop's second Sword
// already has one, whether it is in the bag or equipped.
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

// A trace records a destroyed item and isn't a copy of it. Someone deciding
// whether to buy another has none of the item itself.
//
// This passes whichever of the two guards does the work (the nullptr filter, or
// the suffix on EquipTrace's display name), so it tests the behaviour rather
// than the implementation. Only removing both breaks it.
TEST_F(CapacityTest, CountOwnedDoesNotCountTraces) {
  c_.PickUp(std::make_unique<EquipTrace>(sword_, Equip()));
  ASSERT_EQ(c_.inventory().size(), 1) << "it is in the bag, taking a slot";
  EXPECT_EQ(c_.CountOwned(sword_), 0);
}

// --- RoomFor(ItemPrototype) ---

TEST_F(CapacityTest, RoomForAStackableOnAnEmptyTabIsEveryStack) {
  // 128 slots of 200 each, the Etc default.
  EXPECT_EQ(c_.RoomFor(shell_), kTabCapacity * 200);
}

// The case that motivated the rule: part-full stacks count for their remaining
// space, plus a whole stack for every free slot.
TEST_F(CapacityTest, RoomCountsPartStacksAndFreeSlots) {
  OpenDistinctStacks(kTabCapacity - 11);
  c_.AddItem(shell_, 100);
  // 118 stacks open, so 10 slots are free, and the shell stack has 100 spare.
  ASSERT_EQ(c_.stackables().size(), kTabCapacity - 10);
  EXPECT_EQ(c_.RoomFor(shell_), 10 * 200 + 100);
}

// Space in another item's stack doesn't help.
TEST_F(CapacityTest, RoomIgnoresOtherItemsPartStacks) {
  OpenDistinctStacks(kTabCapacity - 11);
  c_.AddItem(other_, 100);
  ASSERT_EQ(c_.stackables().size(), kTabCapacity - 10);
  EXPECT_EQ(c_.RoomFor(shell_), 10 * 200);
}

// With no free slot, the only room is what that item's open stacks can still
// take.
TEST_F(CapacityTest, RoomOnAFullTabIsTheOpenStacks) {
  OpenDistinctStacks(kTabCapacity - 1);
  c_.AddItem(shell_, 150);
  ASSERT_EQ(c_.stackables().size(), kTabCapacity);
  EXPECT_EQ(c_.RoomFor(shell_), 50);
}

TEST_F(CapacityTest, RoomOnAFullTabOfOtherItemsIsNone) {
  OpenDistinctStacks(kTabCapacity);
  EXPECT_EQ(c_.RoomFor(shell_), 0);
}

// Room follows the item's own stack size rather than a fixed number.
TEST_F(CapacityTest, RoomFollowsTheItemsStackSize) {
  ItemPrototype deep;
  deep.set_name("Deep Thing");
  deep.set_max_stack(30000);
  EXPECT_EQ(c_.RoomFor(deep), kTabCapacity * 30000);
  ItemPrototype tiny;
  tiny.set_name("Odd Thing");
  tiny.set_max_stack(5);
  EXPECT_EQ(c_.RoomFor(tiny), kTabCapacity * 5);
}

// --- AddItem against the limit ---

TEST_F(CapacityTest, AddItemReportsWhatItTook) {
  EXPECT_EQ(c_.AddItem(shell_, 250), 250);
}

// A drop that doesn't fit is taken as far as it goes, and the rest is lost.
TEST_F(CapacityTest, AddItemTakesWhatFitsAndLosesTheRest) {
  OpenDistinctStacks(kTabCapacity - 1);
  c_.AddItem(shell_, 150);
  ASSERT_EQ(c_.RoomFor(shell_), 50);
  EXPECT_EQ(c_.AddItem(shell_, 500), 50);
  EXPECT_EQ(c_.RoomFor(shell_), 0);
  // The tab didn't grow past its limit to hold the overflow.
  EXPECT_EQ(c_.stackables().size(), kTabCapacity);
}

// Adding to an open stack needs no slot, so a full tab still takes some.
TEST_F(CapacityTest, AddItemStillTopsUpOnAFullTab) {
  OpenDistinctStacks(kTabCapacity - 1);
  c_.AddItem(shell_, 10);
  ASSERT_EQ(c_.stackables().size(), kTabCapacity);
  EXPECT_EQ(c_.AddItem(shell_, 30), 30);
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
  // Filling it exactly is fine.
  EXPECT_TRUE(c_.Buy(sword_, 2));
  EXPECT_EQ(c_.inventory().size(), kTabCapacity);
}

// --- ToProto / RestoreFrom ---

// A save stores catalog keys, not item definitions, so a round trip needs the
// catalogs. These stand in for what the game loads from data/.
class SaveRoundTripTest : public CharacterTest {
 protected:
  void SetUp() override {
    sword_.set_name("Sword");
    sword_.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
    sword_.set_upgrade_slots(7);
    sword_.set_required_level(138);
    // Keyed by data-file name, as the real catalogs are, and deliberately not
    // by the item's display name. A save names items by display name, so a
    // fixture whose keys match the names would hide a lookup against the wrong
    // one.
    equips_["sword"] = sword_;

    ItemPrototype shell;
    shell.set_name("Green Snail Shell");
    items_["green_snail_shell"] = shell;
    ItemPrototype trace;
    trace.set_name(kSpellTraceName);
    trace.set_kind(ITEM_KIND_SPELL_TRACE);
    items_["spell_trace"] = trace;
  }

  // Items the first preset wears, as stored in the save.
  static int WornInSave(const Character& saved) {
    return PresetOf(saved.equip_presets(), StatPreset::kFirst)
        .equipped()
        .size();
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

// The equip tab is a vector of C++ objects that the proto doesn't hold until
// ToProto is called, so this is the part a save would most easily lose.
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
  // The stats must be rebuilt from the prototype plus the saved state, not just
  // the state: the base attack lives in the catalog.
  EXPECT_EQ(item->stats().attack(), attack_before);
}

// A trace and a live item differ by one flag, and only that flag decides which
// type comes back. Getting it wrong turns a destroyed item into a wearable one
// on the next launch.
//
// The flag is not set here. It used to be, and that was the only reason this
// passed: nothing in the game set it, so every trace was saved as a live item
// and came back as one, with its stars.
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

// The same thing via the path the player takes, since a trace built by hand has
// whatever flags the test chose.
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

// Recovery copies the trace's state onto the item that replaces it, so the flag
// must not be copied too, or the recovered weapon saves as another trace.
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

// The Etc tab and the purse are two containers the proto doesn't hold until
// ToProto is called, and a currency is saved as a balance rather than a row:
// what comes back is more than any stack of it could hold.
TEST_F(SaveRoundTripTest, CarriesTheStacksAndThePurseAcross) {
  CharacterInstance c = MakeCharacter(rng_);
  c.AddItem(items_["green_snail_shell"], 42);
  c.AddItem(items_["spell_trace"], 250000);

  Character saved = c.ToProto();
  EXPECT_EQ(saved.stacks_size(), 1);
  EXPECT_EQ(saved.currencies().at(kSpellTraceName), 250000)
      << "a currency is saved as a balance, not as stacks";

  CharacterInstance loaded = Reload(saved);
  ASSERT_EQ(loaded.stackables().size(), 1u);
  EXPECT_EQ(loaded.stackables()[0].name(), "Green Snail Shell");
  EXPECT_EQ(loaded.stackables()[0].count(), 42);
  EXPECT_EQ(loaded.CountItem(kSpellTraceName), 250000);
}

// The shelf is the shop's record of what this character sold, so it must last
// beyond the session the sale happened in.
TEST_F(SaveRoundTripTest, CarriesTheBuyBackShelfAcross) {
  CharacterInstance c = MakeCharacter(rng_);
  Equip starred;
  starred.set_equip_name("Sword");
  starred.set_stars(9);
  c.PickUp(std::make_unique<EquipInstance>(sword_, starred));
  c.SellEquip(0);
  ItemPrototype shell = items_["green_snail_shell"];
  shell.set_sell_price(7);  // the fixture's copy is unsellable
  c.AddItem(shell, 6);
  ASSERT_GT(c.SellStackable(0, 6), 0);

  CharacterInstance loaded = Reload(c.ToProto());
  ASSERT_EQ(loaded.buy_backs().size(), 2);
  EXPECT_EQ(loaded.buy_backs().Get(0).stack().count(), 6) << "newest first";
  EXPECT_EQ(loaded.buy_backs().Get(1).equip().stars(), 9);
}

// Each preset's own items are saved separately, so what one wears and what
// another inherits comes back the same.
TEST_F(SaveRoundTripTest, CarriesEveryPresetsOwnGear) {
  CharacterInstance c = MakeCharacter(rng_);
  c.PickUp(std::make_unique<EquipInstance>(sword_));
  c.Equip(0);
  EquipPrototype spare = sword_;
  spare.set_name("Spare Sword");
  c.PickUp(std::make_unique<EquipInstance>(spare));
  ASSERT_TRUE(c.Equip(0, StatPreset::kThird));
  equips_["spare_sword"] = spare;

  CharacterInstance loaded = Reload(c.ToProto());
  EXPECT_EQ(loaded.WornAt(StatPreset::kFirst, EQUIP_SLOT_PRIMARY_WEAPON)
                ->prototype()
                .name(),
            "Sword");
  EXPECT_EQ(loaded.WornAt(StatPreset::kSecond, EQUIP_SLOT_PRIMARY_WEAPON)
                ->prototype()
                .name(),
            "Sword")
      << "still inheriting";
  EXPECT_EQ(loaded.WornAt(StatPreset::kThird, EQUIP_SLOT_PRIMARY_WEAPON)
                ->prototype()
                .name(),
            "Spare Sword");
}

// A save from before presets existed has one worn map, which becomes the first
// preset.
TEST_F(SaveRoundTripTest, ReadsAPrePresetSaveIntoTheFirstPreset) {
  CharacterInstance c = MakeCharacter(rng_);
  c.PickUp(std::make_unique<EquipInstance>(sword_));
  c.Equip(0);
  Character old = c.ToProto();
  *old.mutable_legacy_equipped() =
      PresetOf(old.equip_presets(), StatPreset::kFirst).equipped();
  old.clear_equip_presets();

  CharacterInstance loaded = Reload(old);
  EXPECT_EQ(loaded.equipped().count(EQUIP_SLOT_PRIMARY_WEAPON), 1u);
  EXPECT_EQ(loaded.WornAt(StatPreset::kThird, EQUIP_SLOT_PRIMARY_WEAPON)
                ->prototype()
                .name(),
            "Sword");
}

TEST_F(SaveRoundTripTest, CarriesWornItemsInTheirOwnSlots) {
  CharacterInstance c = MakeCharacter(rng_);
  c.PickUp(std::make_unique<EquipInstance>(sword_));
  c.Equip(0);
  ASSERT_TRUE(c.equipped().count(EQUIP_SLOT_PRIMARY_WEAPON));

  CharacterInstance loaded = Reload(c.ToProto());
  ASSERT_TRUE(loaded.equipped().count(EQUIP_SLOT_PRIMARY_WEAPON));
  EXPECT_EQ(loaded.equipped().at(EQUIP_SLOT_PRIMARY_WEAPON)->prototype().name(),
            "Sword");
  EXPECT_TRUE(loaded.inventory().empty()) << "worn, not in the bag";
  // Rebuilt from what was loaded, not carried over: a loaded character must hit
  // as hard as the one that was saved.
  EXPECT_EQ(loaded.equip_stats().attack(), c.equip_stats().attack());
}

// Skills are keyed by name, so they load without the skill catalog.
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

// Data files outlive saves. Deleting an item from data/ costs the player that
// item, but must not break the character.
TEST_F(SaveRoundTripTest, DropsItemsTheCatalogsNoLongerName) {
  CharacterInstance c = MakeCharacter(rng_);
  c.PickUp(std::make_unique<EquipInstance>(sword_));
  c.AddItem(items_["green_snail_shell"], 5);
  c.AddMeso(99);
  Character saved = c.ToProto();

  equips_.clear();
  items_.clear();
  CharacterInstance loaded = Reload(saved);
  EXPECT_TRUE(loaded.inventory().empty());
  EXPECT_TRUE(loaded.stackables().empty());
  EXPECT_EQ(loaded.meso(), 99) << "the character survives its lost items";
}

// Restoring replaces rather than merges: loading over a character mid-session
// must not leave that character's items in the bag.
TEST_F(SaveRoundTripTest, ReplacesWhateverWasThereBefore) {
  CharacterInstance c = MakeCharacter(rng_);
  c.PickUp(std::make_unique<EquipInstance>(sword_));
  c.AddItem(items_["red_potion"], 9);

  c.RestoreFrom(Character{}, equips_, items_);
  EXPECT_TRUE(c.inventory().empty());
  EXPECT_TRUE(c.stackables().empty());
}

// Unequipping must empty the slot in the next save too. A proto map overwrites
// by key, so a stale worn item can't be a duplicate. It can only be one that
// was never cleared, which is harder to notice.
TEST_F(SaveRoundTripTest, AnEmptiedSlotIsNotSaved) {
  CharacterInstance c = MakeCharacter(rng_);
  c.PickUp(std::make_unique<EquipInstance>(sword_));
  c.Equip(0);
  CharacterInstance loaded = Reload(c.ToProto());
  ASSERT_EQ(WornInSave(loaded.ToProto()), 1);

  ASSERT_TRUE(loaded.Unequip(EQUIP_SLOT_PRIMARY_WEAPON));
  EXPECT_EQ(WornInSave(loaded.ToProto()), 0);
  EXPECT_EQ(loaded.ToProto().inventory().equip_tab_size(), 1)
      << "back in the bag";
}

// The round trip must be idempotent: a loaded character must save exactly what
// it was loaded from. This catches an entry duplicated, dropped or left stale
// on either side, which checking one direction alone would miss.
TEST_F(SaveRoundTripTest, ReSavingALoadedCharacterGivesTheSameSave) {
  CharacterInstance c = MakeCharacter(rng_);
  c.PickUp(std::make_unique<EquipInstance>(sword_));
  c.PickUp(std::make_unique<EquipInstance>(sword_));
  c.Equip(0);
  c.AddItem(items_["green_snail_shell"], 12);
  c.AddItem(items_["red_potion"], 2);
  c.AddMeso(500);

  Character first = c.ToProto();
  Character second = Reload(first).ToProto();
  EXPECT_EQ(second.inventory().equip_tab_size(),
            first.inventory().equip_tab_size());
  EXPECT_EQ(WornInSave(second), WornInSave(first));
  EXPECT_EQ(second.stacks_size(), first.stacks_size());
  EXPECT_EQ(second.meso(), first.meso());
  EXPECT_EQ(second.level(), first.level());
}

// --- ReconcileAp ---

// A saved proto with the four AP stats set as given. Level and job stage say
// what the check should expect; the stats say what it finds.
Character SavedProto(int level, int job_stage, Job job, int ap, int str,
                     int dex) {
  Character proto;
  proto.set_level(level);
  proto.set_job_stage(job_stage);
  proto.set_job(job);
  proto.set_ap(ap);
  proto.mutable_allocated_stats()->set_str(str);
  proto.mutable_allocated_stats()->set_dex(dex);
  proto.mutable_allocated_stats()->set_int_(kBaseStat);
  proto.mutable_allocated_stats()->set_luk(kBaseStat);
  return proto;
}

// The level-1 Beginner the game really starts with. ExpectedTotalAp counts the
// free STR as AP already spent, so a fixture built from a bare proto would be
// nine short and prove nothing.
CharacterInstance MakeFreshBeginner(std::mt19937& rng, int ap = 0) {
  return CharacterInstance(
      rng, SavedProto(1, 0, JOB_BEGINNER, ap, kBeginnerStr, kBaseStat));
}

class ReconcileApTest : public CharacterTest {};

// The test that keeps the check honest: whatever the rules for granting AP, a
// character levelled under them balances at every step. If a 5th job paid AP by
// a rule ExpectedTotalAp doesn't know, this would fail, rather than every save
// being quietly "corrected" against an outdated rule.
TEST_F(ReconcileApTest, ACharacterTheGameGrewNeverNeedsCorrecting) {
  const std::vector<Job> kPath = {JOB_SWORDMAN, JOB_FIGHTER, JOB_CRUSADER,
                                  JOB_HERO};
  CharacterInstance c = MakeFreshBeginner(rng_);
  ASSERT_EQ(c.ReconcileAp(), 0) << "at level 1";
  size_t taken = 0;
  for (int level = 2; level <= 140; ++level) {
    c.LevelUp();
    if (c.CanAdvanceJob() && taken < kPath.size()) {
      c.AdvanceJob(kPath[taken]);
      if (c.proto().job_stage() == 1) {
        c.ResetStatsForJob(kPath[0]);
      }
      ++taken;
    }
    // Spending must not unbalance it either, since the check reads the stats as
    // well as the pool.
    c.AllocateStat(STAT_FIELD_STR, 3);
    ASSERT_EQ(c.ReconcileAp(), 0) << "at level " << level;
  }
  EXPECT_EQ(c.proto().job_stage(), 4);
}

TEST_F(ReconcileApTest, ASaveThatIsShortIsHandedTheDifferenceLoose) {
  CharacterInstance c(
      rng_, SavedProto(10, 0, JOB_BEGINNER, /*ap=*/0, kBeginnerStr, kBaseStat));
  // 5 per level over levels 2-10, none of which the save recorded.
  EXPECT_EQ(c.ReconcileAp(), 45);
  EXPECT_EQ(c.proto().ap(), 45);
  EXPECT_EQ(c.proto().allocated_stats().str(), kBeginnerStr);
  EXPECT_EQ(c.ReconcileAp(), 0) << "correcting twice is a no-op";
}

// Excess AP comes out of the pool first, so a character who had nothing unspent
// keeps every stat they bought.
TEST_F(ReconcileApTest, TooMuchComesOffThePoolBeforeTheStats) {
  CharacterInstance c = MakeFreshBeginner(rng_, /*ap=*/100);
  EXPECT_EQ(c.ReconcileAp(), -100);
  EXPECT_EQ(c.proto().ap(), 0);
  EXPECT_EQ(c.proto().allocated_stats().str(), kBeginnerStr);
}

// Past the pool, it comes out of the stats, with the primary stat last: a
// warrior without STR can't be played.
TEST_F(ReconcileApTest, PastThePoolItComesOffTheStatsPrimaryLast) {
  // The save has 96 over base in STR and 46 in DEX, against the 54 a level-10
  // 1st job has received in total.
  CharacterInstance c(rng_, SavedProto(10, 1, JOB_SWORDMAN, /*ap=*/0,
                                       /*str=*/100, /*dex=*/50));
  EXPECT_EQ(c.ReconcileAp(), -88);
  EXPECT_EQ(c.proto().allocated_stats().dex(), kBaseStat);
  EXPECT_EQ(c.proto().allocated_stats().str(), 58);
  EXPECT_EQ(c.proto().ap(), 0);
  EXPECT_EQ(c.ReconcileAp(), 0);
}

// A stat drains to its base and stops, and what is still owed comes from the
// next stat rather than pushing the first below base.
TEST_F(ReconcileApTest, AStatDrainsToItsBaseAndNoFurther) {
  Character proto = SavedProto(1, 0, JOB_BEGINNER, /*ap=*/0, /*str=*/10,
                               /*dex=*/10);
  proto.mutable_allocated_stats()->set_int_(10);
  proto.mutable_allocated_stats()->set_luk(10);
  // 6 over base in each of the four, against the 9 a level-1 Beginner has. STR
  // is the Beginner's primary, so it is left alone.
  CharacterInstance c(rng_, std::move(proto));
  EXPECT_EQ(c.ReconcileAp(), -15);
  const AllocatedStats& s = c.proto().allocated_stats();
  EXPECT_EQ(s.dex(), kBaseStat);
  EXPECT_EQ(s.int_(), kBaseStat);
  EXPECT_EQ(s.luk(), 7);
  EXPECT_EQ(s.str(), 10) << "nothing was owed by the time it was reached";
  EXPECT_EQ(c.ReconcileAp(), 0);
}

// The 5 AP the 3rd and 4th advancements grant count too, so a save with them
// must not read as ten too many.
TEST_F(ReconcileApTest, TheAdvancementBonusesCount) {
  int at_second = 5 * 99 + kBeginnerStr - kBaseStat;
  EXPECT_EQ(ExpectedTotalAp(/*level=*/100, /*job_stage=*/2), at_second);
  EXPECT_EQ(ExpectedTotalAp(/*level=*/100, /*job_stage=*/3), at_second + 5);
  EXPECT_EQ(ExpectedTotalAp(/*level=*/100, /*job_stage=*/4), at_second + 10);
}

// --- Arcane Symbols ---

CharacterInstance MakeHero(std::mt19937& rng, Job job = JOB_HERO) {
  Character proto;
  proto.set_level(200);
  proto.set_job(job);
  proto.set_job_stage(job == JOB_HERO ? 4 : 3);
  return CharacterInstance(rng, std::move(proto));
}

class SymbolTest : public CharacterTest {
 protected:
  // The area only decides the slot, so one helper covers all six.
  EquipPrototype Symbol(EquipSlot slot) {
    EquipPrototype proto;
    proto.set_name("Symbol");
    proto.set_equip_slot(slot);
    proto.mutable_arcane_symbol()->set_meso_cost_base(8);
    return proto;
  }
  void Wear(CharacterInstance& c, const EquipPrototype& proto, int level) {
    Equip state;
    state.set_symbol_level(level);
    c.PickUp(std::make_unique<EquipInstance>(proto, state));
    ASSERT_TRUE(c.Equip(0));
  }
};

TEST_F(SymbolTest, WornSymbolsGrantForceAndThePrimaryStat) {
  CharacterInstance c_ = MakeHero(rng_);
  Wear(c_, Symbol(EQUIP_SLOT_SYMBOL_VANISHING_JOURNEY), 8);
  EXPECT_EQ(c_.arcane_force(), 100);
  EXPECT_EQ(c_.equip_stats().str(), 1000);
  EXPECT_EQ(c_.equip_stats().dex(), 0);

  // A second area is a second slot, so the two add up rather than one replacing
  // the other.
  Wear(c_, Symbol(EQUIP_SLOT_SYMBOL_CHU_CHU_ISLAND), 1);
  EXPECT_EQ(c_.arcane_force(), 130);
  EXPECT_EQ(c_.equip_stats().str(), 1300);
}

TEST_F(SymbolTest, TakingOneOffTakesItsForceWithIt) {
  CharacterInstance c_ = MakeHero(rng_);
  Wear(c_, Symbol(EQUIP_SLOT_SYMBOL_VANISHING_JOURNEY), 5);
  ASSERT_EQ(c_.arcane_force(), 70);
  ASSERT_TRUE(c_.Unequip(EQUIP_SLOT_SYMBOL_VANISHING_JOURNEY));
  EXPECT_EQ(c_.arcane_force(), 0);
  EXPECT_EQ(c_.equip_stats().str(), 0);
}

// Combining uses spares from the bag to raise the worn symbol.
TEST_F(SymbolTest, CombiningSpendsSparesIntoTheWornSymbol) {
  CharacterInstance c_ = MakeHero(rng_);
  EquipPrototype proto = Symbol(EQUIP_SLOT_SYMBOL_VANISHING_JOURNEY);
  Wear(c_, proto, /*level=*/1);
  for (int i = 0; i < 5; ++i) {
    c_.PickUp(std::make_unique<EquipInstance>(proto));
  }
  // A spare from another area doesn't count as a spare for this one.
  c_.PickUp(std::make_unique<EquipInstance>(
      Symbol(EQUIP_SLOT_SYMBOL_CHU_CHU_ISLAND)));
  EXPECT_EQ(c_.SpareSymbols(EQUIP_SLOT_SYMBOL_VANISHING_JOURNEY), 5);

  EXPECT_EQ(c_.CombineSymbols(EQUIP_SLOT_SYMBOL_VANISHING_JOURNEY, 3), 3);
  EXPECT_EQ(c_.equipped()
                .at(EQUIP_SLOT_SYMBOL_VANISHING_JOURNEY)
                ->equip_state()
                .symbol_exp(),
            3);
  EXPECT_EQ(c_.SpareSymbols(EQUIP_SLOT_SYMBOL_VANISHING_JOURNEY), 2);
  EXPECT_EQ(c_.inventory().size(), 3) << "the Chu Chu spare was left alone";

  // Asking for more than there are takes all there are.
  EXPECT_EQ(c_.CombineSymbols(EQUIP_SLOT_SYMBOL_VANISHING_JOURNEY, 99), 2);
  EXPECT_EQ(c_.SpareSymbols(EQUIP_SLOT_SYMBOL_VANISHING_JOURNEY), 0);
}

// A consumed symbol is worth itself plus everything absorbed into it, levels
// included: a symbol raised to level 2 counts as the twenty copies it was built
// from.
TEST_F(SymbolTest, ASpareCarriesEverythingBankedInItAcross) {
  CharacterInstance c_ = MakeHero(rng_);
  EquipPrototype proto = Symbol(EQUIP_SLOT_SYMBOL_VANISHING_JOURNEY);
  Wear(c_, proto, /*level=*/1);
  Equip banked;
  banked.set_symbol_exp(4);
  c_.PickUp(std::make_unique<EquipInstance>(proto, banked));
  Equip packed;
  packed.set_symbol_level(2);
  packed.set_symbol_exp(7);
  c_.PickUp(std::make_unique<EquipInstance>(proto, packed));

  // Taken from the back of the bag, so the levelled one goes first.
  EXPECT_EQ(c_.SpareSymbolWorths(EQUIP_SLOT_SYMBOL_VANISHING_JOURNEY),
            (std::vector<int>{20, 5}));
  ASSERT_EQ(c_.CombineSymbols(EQUIP_SLOT_SYMBOL_VANISHING_JOURNEY, 2), 2);
  EXPECT_EQ(c_.equipped()
                .at(EQUIP_SLOT_SYMBOL_VANISHING_JOURNEY)
                ->equip_state()
                .symbol_exp(),
            25);
}

TEST_F(SymbolTest, CombiningIntoAnEmptySlotTakesNothing) {
  CharacterInstance c_ = MakeHero(rng_);
  c_.PickUp(std::make_unique<EquipInstance>(
      Symbol(EQUIP_SLOT_SYMBOL_VANISHING_JOURNEY)));
  EXPECT_EQ(c_.CombineSymbols(EQUIP_SLOT_SYMBOL_VANISHING_JOURNEY, 1), 0);
  EXPECT_EQ(c_.inventory().size(), 1);
}

TEST_F(SymbolTest, LevellingChargesTheMesoAndMovesTheForce) {
  CharacterInstance c_ = MakeHero(rng_);
  c_.AddMeso(10'000'000);
  EquipPrototype proto = Symbol(EQUIP_SLOT_SYMBOL_VANISHING_JOURNEY);
  Equip state;
  state.set_symbol_level(1);
  state.set_symbol_exp(20);  // 12 buys the rung, 8 carries over
  c_.PickUp(std::make_unique<EquipInstance>(proto, state));
  ASSERT_TRUE(c_.Equip(0));
  ASSERT_EQ(c_.arcane_force(), 30);

  ASSERT_TRUE(c_.LevelUpSymbol(EQUIP_SLOT_SYMBOL_VANISHING_JOURNEY));
  EXPECT_EQ(c_.proto().meso(), 10'000'000 - 970'000);
  EXPECT_EQ(c_.arcane_force(), 40);
  EXPECT_EQ(c_.equip_stats().str(), 400);
  const Equip& after =
      c_.equipped().at(EQUIP_SLOT_SYMBOL_VANISHING_JOURNEY)->equip_state();
  EXPECT_EQ(after.symbol_level(), 2);
  EXPECT_EQ(after.symbol_exp(), 8);
}

TEST_F(SymbolTest, LevellingRefusesWhatItCannotPayFor) {
  CharacterInstance c_ = MakeHero(rng_);
  EquipPrototype proto = Symbol(EQUIP_SLOT_SYMBOL_VANISHING_JOURNEY);

  // The duplicates are absorbed, but there is no meso.
  Equip ready;
  ready.set_symbol_level(1);
  ready.set_symbol_exp(20);
  c_.PickUp(std::make_unique<EquipInstance>(proto, ready));
  ASSERT_TRUE(c_.Equip(0));
  EXPECT_FALSE(c_.LevelUpSymbol(EQUIP_SLOT_SYMBOL_VANISHING_JOURNEY));

  // There is enough meso, but not enough duplicates.
  c_.AddMeso(10'000'000);
  ASSERT_TRUE(c_.Unequip(EQUIP_SLOT_SYMBOL_VANISHING_JOURNEY));
  Equip waiting;
  waiting.set_symbol_level(1);
  waiting.set_symbol_exp(11);
  c_.PickUp(std::make_unique<EquipInstance>(proto, waiting));
  ASSERT_TRUE(c_.Equip(1));
  EXPECT_FALSE(c_.LevelUpSymbol(EQUIP_SLOT_SYMBOL_VANISHING_JOURNEY));
  EXPECT_EQ(c_.proto().meso(), 10'000'000) << "nothing was taken";

  // A slot holding gear rather than a symbol can't be levelled at all.
  EquipPrototype axe;
  axe.set_name("Axe");
  axe.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  c_.PickUp(std::make_unique<EquipInstance>(axe));
  ASSERT_TRUE(c_.Equip(c_.inventory().size() - 1));
  EXPECT_FALSE(c_.LevelUpSymbol(EQUIP_SLOT_PRIMARY_WEAPON));
}

TEST_F(SymbolTest, TheGrantFollowsTheJob) {
  CharacterInstance c_ = MakeHero(rng_, JOB_CLERIC);
  Wear(c_, Symbol(EQUIP_SLOT_SYMBOL_VANISHING_JOURNEY), 1);
  ASSERT_EQ(c_.equip_stats().int_(), 300) << "a Cleric swings on INT";
  c_.AdvanceJob(JOB_HERO);
  EXPECT_EQ(c_.equip_stats().int_(), 0);
  EXPECT_EQ(c_.equip_stats().str(), 300) << "and a Hero on STR";
}

// --- Cubing worn gear ---

// A worn piece that can have potential, at a level where every band applies.
EquipPrototype Cubeable(EquipSlot slot) {
  EquipPrototype proto;
  proto.set_name("Gear");
  proto.set_equip_slot(slot);
  proto.set_required_level(100);
  return proto;
}

// The totals are rebuilt from the worn items, so cubing reaches the stats
// without anything else being notified.
TEST_F(CharacterTest, CubingWornGearRollsItsLinesAndMovesTheTotals) {
  CharacterInstance c = MakeCharacter(rng_);
  c.PickUp(
      std::make_unique<EquipInstance>(Cubeable(EQUIP_SLOT_PRIMARY_WEAPON)));
  ASSERT_TRUE(c.Equip(0));

  ASSERT_TRUE(c.CubeWorn(EQUIP_SLOT_PRIMARY_WEAPON, CubeType::kRed));
  const Potential& potential =
      c.equipped().at(EQUIP_SLOT_PRIMARY_WEAPON)->potential();
  EXPECT_EQ(potential.rank(), POTENTIAL_RANK_RARE) << "the first cube is Rare";
  EXPECT_EQ(potential.lines_size(), kPotentialLines);
  // Everything a weapon rolls at Rare is a percentage of something, so one of
  // these changed whichever three lines came out.
  const PotentialTotals& totals = c.potential_totals();
  EXPECT_GT(totals.str_pct + totals.dex_pct + totals.int_pct + totals.luk_pct +
                totals.attack_pct + totals.magic_attack_pct +
                totals.damage_pct + totals.max_hp_pct,
            0.0);
}

// The purchase, which splits the mechanism above in two: the cube is paid for,
// the roll is returned, and nothing changes on the item until it is accepted.
TEST_F(CharacterTest, BuyingACubeChargesForARollAndPutsNothingOn) {
  CharacterInstance c = MakeCharacter(rng_);
  c.PickUp(
      std::make_unique<EquipInstance>(Cubeable(EQUIP_SLOT_PRIMARY_WEAPON)));
  ASSERT_TRUE(c.Equip(0));
  c.AddMeso(kCubeCost);

  std::optional<Potential> rolled =
      c.BuyCube(EQUIP_SLOT_PRIMARY_WEAPON, CubeType::kRed);
  ASSERT_TRUE(rolled.has_value());
  EXPECT_EQ(rolled->lines_size(), kPotentialLines);
  EXPECT_EQ(c.meso(), 0);
  EXPECT_EQ(
      c.equipped().at(EQUIP_SLOT_PRIMARY_WEAPON)->potential().lines_size(), 0)
      << "declining a roll leaves the piece as it was";

  ASSERT_TRUE(c.TakePotential(EQUIP_SLOT_PRIMARY_WEAPON, *rolled));
  EXPECT_EQ(
      c.equipped().at(EQUIP_SLOT_PRIMARY_WEAPON)->potential().lines_size(),
      kPotentialLines);
}

TEST_F(CharacterTest, BuyingACubeTakesNothingFromAPurseThatCannotCoverIt) {
  CharacterInstance c = MakeCharacter(rng_);
  c.PickUp(
      std::make_unique<EquipInstance>(Cubeable(EQUIP_SLOT_PRIMARY_WEAPON)));
  ASSERT_TRUE(c.Equip(0));
  c.AddMeso(kCubeCost - 1);

  EXPECT_FALSE(
      c.BuyCube(EQUIP_SLOT_PRIMARY_WEAPON, CubeType::kRed).has_value());
  EXPECT_EQ(c.meso(), kCubeCost - 1);
  // A piece that can't have potential is refused without charging.
  c.PickUp(std::make_unique<EquipInstance>(Cubeable(EQUIP_SLOT_MEDAL)));
  ASSERT_TRUE(c.Equip(c.inventory().size() - 1));
  c.AddMeso(kCubeCost);
  EXPECT_FALSE(c.BuyCube(EQUIP_SLOT_MEDAL, CubeType::kRed).has_value());
  EXPECT_EQ(c.meso(), kCubeCost * 2 - 1);
}

TEST_F(CharacterTest, CubingRefusesAnEmptySlotAndAPieceThatTakesNoPotential) {
  CharacterInstance c = MakeCharacter(rng_);
  EXPECT_FALSE(c.CubeWorn(EQUIP_SLOT_PRIMARY_WEAPON, CubeType::kRed));

  c.PickUp(std::make_unique<EquipInstance>(Cubeable(EQUIP_SLOT_MEDAL)));
  ASSERT_TRUE(c.Equip(0));
  EXPECT_FALSE(c.CubeWorn(EQUIP_SLOT_MEDAL, CubeType::kRed));
  EXPECT_EQ(c.equipped().at(EQUIP_SLOT_MEDAL)->potential().lines_size(), 0);
}

// --- ReconcileSkills ---

// A character of `job` at `stage` with `levels` already learned and `sp` left
// in the stage's pool. Level 30, so no skill is blocked by required_level.
CharacterInstance MakeCharacterWithSkills(
    std::mt19937& rng, Job job, int stage,
    const std::map<std::string, int>& levels, int sp = 0) {
  Character proto;
  proto.set_job(job);
  proto.set_job_stage(stage);
  proto.set_level(30);
  (*proto.mutable_sp_by_stage())[stage] = sp;
  for (const std::pair<const std::string, int>& entry : levels) {
    (*proto.mutable_skill_levels())[entry.first] = entry.second;
  }
  return CharacterInstance(rng, std::move(proto));
}

// A book for one advancement, keyed by name where the real catalog uses file
// names. That is close enough here, since no skill in these tests has two
// different names.
void AddBook(std::map<std::string, Skill>& skills, JobAdvancement advancement,
             const std::vector<std::pair<std::string, int>>& maxes) {
  for (const std::pair<std::string, int>& entry : maxes) {
    skills[entry.first] = MakeSkill(entry.first, advancement, entry.second);
  }
}

// The total of every skill in `skills` for this character: the SP spent in the
// books, which must not change.
int TotalLearned(const CharacterInstance& c,
                 const std::map<std::string, Skill>& skills) {
  int total = 0;
  for (const std::pair<const std::string, Skill>& entry : skills) {
    total += c.skill_level(entry.second);
  }
  return total;
}

class ReconcileSkillsTest : public CharacterTest {};

TEST_F(ReconcileSkillsTest, ABookThatFitsIsLeftAlone) {
  std::map<std::string, Skill> skills;
  AddBook(skills, JOB_ADVANCEMENT_SWORDMAN, {{"Slash", 10}, {"Guard", 20}});
  CharacterInstance c = MakeCharacterWithSkills(rng_, JOB_SWORDMAN, 1,
                                                {{"Slash", 10}, {"Guard", 3}});
  EXPECT_EQ(c.ReconcileSkills(skills), 0);
  EXPECT_EQ(c.skill_level(skills.at("Slash")), 10);
  EXPECT_EQ(c.skill_level(skills.at("Guard")), 3);
}

// The main case: the level comes down, the points aren't lost, and the pool
// that paid for them doesn't change.
TEST_F(ReconcileSkillsTest, PointsPastTheMaxAreSpentAgainInTheSameBook) {
  std::map<std::string, Skill> skills;
  AddBook(skills, JOB_ADVANCEMENT_SWORDMAN,
          {{"Slash", 10}, {"Guard", 20}, {"Rage", 20}, {"Focus", 20}});
  CharacterInstance c = MakeCharacterWithSkills(
      rng_, JOB_SWORDMAN, 1, {{"Slash", 40}, {"Guard", 0}, {"Rage", 0}}, 7);
  int before = TotalLearned(c, skills);

  EXPECT_EQ(c.ReconcileSkills(skills), 30);
  EXPECT_EQ(c.skill_level(skills.at("Slash")), 10);
  EXPECT_EQ(TotalLearned(c, skills), before) << "points were lost or invented";
  EXPECT_EQ(c.sp(1), 7) << "the pool already paid for them";
  for (const std::pair<const std::string, Skill>& entry : skills) {
    EXPECT_LE(c.skill_level(entry.second), entry.second.max_level())
        << entry.first << " was overfilled in its turn";
  }
  // The seed is fixed, so this is a deterministic spread: thirty points drawn
  // over three skills reach all three.
  EXPECT_GT(c.skill_level(skills.at("Guard")), 0);
  EXPECT_GT(c.skill_level(skills.at("Rage")), 0);
  EXPECT_GT(c.skill_level(skills.at("Focus")), 0);
}

// A point belongs to the pool that paid for it, so it may only go to that
// pool's book. Otherwise a Fighter's stage-2 SP would quietly pay for stage-1
// levels, and neither book would balance.
TEST_F(ReconcileSkillsTest, AnotherBooksSkillIsNeverATaker) {
  std::map<std::string, Skill> skills;
  AddBook(skills, JOB_ADVANCEMENT_SWORDMAN, {{"Slash", 10}, {"Guard", 20}});
  AddBook(skills, JOB_ADVANCEMENT_FIGHTER, {{"Rage", 20}});
  CharacterInstance c =
      MakeCharacterWithSkills(rng_, JOB_FIGHTER, 2, {{"Slash", 15}});

  EXPECT_EQ(c.ReconcileSkills(skills), 5);
  EXPECT_EQ(c.skill_level(skills.at("Guard")), 5);
  EXPECT_EQ(c.skill_level(skills.at("Rage")), 0);
}

// Points are drawn one at a time, so a skill unlocked by earlier points can
// take later ones. Nothing else here can take a point, so only one order is
// possible.
TEST_F(ReconcileSkillsTest, ASkillTheseVeryPointsUnlockCanTakeThem) {
  std::map<std::string, Skill> skills;
  AddBook(skills, JOB_ADVANCEMENT_SWORDMAN, {{"Slash", 10}, {"Gate", 5}});
  Skill& gated = skills["Gated"] =
      MakeSkill("Gated", JOB_ADVANCEMENT_SWORDMAN, 20);
  gated.mutable_required_skill()->set_skill_name("Gate");
  gated.mutable_required_skill()->set_level(5);
  CharacterInstance c =
      MakeCharacterWithSkills(rng_, JOB_SWORDMAN, 1, {{"Slash", 18}});

  EXPECT_EQ(c.ReconcileSkills(skills), 8);
  EXPECT_EQ(c.skill_level(skills.at("Gate")), 5) << "the only taker at first";
  EXPECT_EQ(c.skill_level(skills.at("Gated")), 3) << "and the taker after";
}

// A book costs exactly what its levels pay, so this should never happen. If it
// does, returning the point to the pool is better than losing it.
TEST_F(ReconcileSkillsTest, WithNoTakerThePointsGoBackToThePool) {
  std::map<std::string, Skill> skills;
  AddBook(skills, JOB_ADVANCEMENT_SWORDMAN, {{"Slash", 10}});
  CharacterInstance c =
      MakeCharacterWithSkills(rng_, JOB_SWORDMAN, 1, {{"Slash", 13}}, 2);

  EXPECT_EQ(c.ReconcileSkills(skills), 3);
  EXPECT_EQ(c.skill_level(skills.at("Slash")), 10);
  EXPECT_EQ(c.sp(1), 5);
}

// A book the character hasn't reached isn't theirs to rebalance: a Swordman
// with a Fighter's skill in their save should be left alone, not have a
// Fighter's points spent.
TEST_F(ReconcileSkillsTest, ABookTheCharacterCannotHoldIsNotTouched) {
  std::map<std::string, Skill> skills;
  AddBook(skills, JOB_ADVANCEMENT_FIGHTER, {{"Rage", 10}, {"Guard", 20}});
  CharacterInstance c =
      MakeCharacterWithSkills(rng_, JOB_SWORDMAN, 1, {{"Rage", 15}});

  EXPECT_EQ(c.ReconcileSkills(skills), 0);
  EXPECT_EQ(c.skill_level(skills.at("Rage")), 15);
}

// --- ReconcileSp ---

class ReconcileSpTest : public CharacterTest {};

// The same check as for ReconcileAp: whatever the rules for granting SP, a
// character levelled under them balances at every step. If a level ever paid SP
// by a rule ExpectedSpForStage doesn't know, this would fail, rather than every
// save being quietly "corrected" against an outdated rule.
TEST_F(ReconcileSpTest, ACharacterTheGameGrewNeverNeedsCorrecting) {
  const std::vector<Job> kPath = {JOB_SWORDMAN, JOB_FIGHTER, JOB_CRUSADER,
                                  JOB_HERO};
  CharacterInstance c = MakeFreshBeginner(rng_);
  ASSERT_EQ(c.ReconcileSp({}), 0) << "at level 1";
  size_t taken = 0;
  for (int level = 2; level <= 200; ++level) {
    c.LevelUp();
    if (c.CanAdvanceJob() && taken < kPath.size()) {
      c.AdvanceJob(kPath[taken]);
      ++taken;
    }
    ASSERT_EQ(c.ReconcileSp({}), 0) << "at level " << level;
  }
}

// The case that prompted this: a character already past a rung when the Hyper
// ladder was added never got its point, and nothing else would ever give it.
TEST_F(ReconcileSpTest, AMissedHyperRungIsHandedOver) {
  Character proto;
  proto.set_job(JOB_HERO);
  proto.set_job_stage(4);
  proto.set_level(150);
  (*proto.mutable_sp_by_stage())[1] = 60;
  (*proto.mutable_sp_by_stage())[2] = 90;
  (*proto.mutable_sp_by_stage())[3] = 120;
  (*proto.mutable_sp_by_stage())[4] = 200;
  CharacterInstance c(rng_, std::move(proto));

  // 140, 145 and 150 have each paid one.
  EXPECT_EQ(c.ReconcileSp({}), 3);
  EXPECT_EQ(c.proto().hyper_sp(), 3);
  EXPECT_EQ(c.ReconcileSp({}), 0) << "correcting twice is a no-op";
}

// The whole ladder, with a book that has already spent it.
TEST_F(ReconcileSpTest, LearnedHyperSkillsAreSpentPoints) {
  std::map<std::string, Skill> skills;
  std::map<std::string, int> learned;
  for (int i = 0; i < 12; ++i) {
    std::string name = "Hyper " + std::to_string(i);
    skills[name] = MakeSkill(name, JOB_ADVANCEMENT_HERO, 1);
    skills[name].set_hyper(true);
    learned[name] = 1;
  }
  Character proto;
  proto.set_job(JOB_HERO);
  proto.set_job_stage(4);
  proto.set_level(195);
  (*proto.mutable_sp_by_stage())[1] = 60;
  (*proto.mutable_sp_by_stage())[2] = 90;
  (*proto.mutable_sp_by_stage())[3] = 120;
  (*proto.mutable_sp_by_stage())[4] = 200;
  for (const std::pair<const std::string, int>& entry : learned) {
    (*proto.mutable_skill_levels())[entry.first] = entry.second;
  }
  CharacterInstance c(rng_, std::move(proto));

  EXPECT_EQ(c.ReconcileSp(skills), 0) << "twelve rungs, twelve skills";
  EXPECT_EQ(c.proto().hyper_sp(), 0);
}

// A pool that looks short by what its book already holds isn't short.
TEST_F(ReconcileSpTest, PointsAlreadySpentCountTowardTheBook) {
  std::map<std::string, Skill> skills;
  AddBook(skills, JOB_ADVANCEMENT_SWORDMAN, {{"Slash", 40}, {"Guard", 40}});
  CharacterInstance c = MakeCharacterWithSkills(
      rng_, JOB_SWORDMAN, 1, {{"Slash", 40}, {"Guard", 15}}, /*sp=*/5);

  // Levels 11-30 pay 60, of which 55 are in the book and 5 in the pool.
  EXPECT_EQ(c.ReconcileSp(skills), 0);
  EXPECT_EQ(c.sp(1), 5);
}

// The same book, with nothing in the pool to make up the difference.
TEST_F(ReconcileSpTest, AShortPoolIsHandedTheDifference) {
  std::map<std::string, Skill> skills;
  AddBook(skills, JOB_ADVANCEMENT_SWORDMAN, {{"Slash", 40}, {"Guard", 40}});
  CharacterInstance c =
      MakeCharacterWithSkills(rng_, JOB_SWORDMAN, 1, {{"Slash", 40}});

  EXPECT_EQ(c.ReconcileSp(skills), 20);
  EXPECT_EQ(c.sp(1), 20);
}

// Excess comes out of the pool only, and stops at empty: cutting learned skills
// is ReconcileSkills's job.
TEST_F(ReconcileSpTest, TooMuchComesOffThePoolAndNoFurther) {
  std::map<std::string, Skill> skills;
  AddBook(skills, JOB_ADVANCEMENT_SWORDMAN, {{"Slash", 100}});
  CharacterInstance c = MakeCharacterWithSkills(rng_, JOB_SWORDMAN, 1,
                                                {{"Slash", 100}}, /*sp=*/10);

  // 110 held against the 60 the levels paid, and only 10 in the pool to give
  // back.
  EXPECT_EQ(c.ReconcileSp(skills), -10);
  EXPECT_EQ(c.sp(1), 0);
  EXPECT_EQ(c.skill_level(skills.at("Slash")), 100);
}

// A book the character never took isn't their spending, so it can't make their
// pool look overspent.
TEST_F(ReconcileSpTest, ABookTheCharacterCannotHoldIsNotSpending) {
  std::map<std::string, Skill> skills;
  AddBook(skills, JOB_ADVANCEMENT_MAGICIAN, {{"Magic Claw", 60}});
  CharacterInstance c = MakeCharacterWithSkills(
      rng_, JOB_SWORDMAN, 1, {{"Magic Claw", 60}}, /*sp=*/60);

  EXPECT_EQ(c.ReconcileSp(skills), 0);
  EXPECT_EQ(c.sp(1), 60);
}

}  // namespace
}  // namespace ms
