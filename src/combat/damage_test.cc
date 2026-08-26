#include "src/combat/damage.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <random>
#include <vector>

#include "src/character/character.h"
#include "src/combat/constants.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/mob.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {
namespace {

// A mob carrying just the damage-relevant fields: PDR (whole percent), the boss
// flag, and level (for the level multiplier).
Mob MakeMob(int pdr = 0, bool boss = false, int level = 0) {
  Mob mob;
  mob.set_pdr(pdr);
  mob.set_boss(boss);
  mob.set_level(level);
  return mob;
}

// The level multiplier at equal attacker/mob level. Baseline() and MakeMob()
// both sit at level 0, so every ExpectedAttackDamage below carries this factor.
constexpr double kEqualLevel = 1.1;

// What the base crit pair is worth to a character who has bought neither.
// Every figure below carries it, the level multiplier included.
constexpr double kBaseCrit = 1.0 + kBaseCritRate * kBaseCritDamage;

// What the baseline swing below comes to before any modifier: 45 max base at
// the melee mastery the default carries, 45 * (1 + 0.20) / 2.
constexpr double kBaseline = 27.0;

class OffenseTest : public ::testing::Test {
 protected:
  // Only primary/secondary/attack set; every modifier at identity, mastery at
  // the melee base its default carries. StatValue = 4*10+5 = 45;
  // MaxBase = 45*100/100 = 45; expected = 45*(1+0.20)/2 = 27.
  OffenseStats Baseline() {
    OffenseStats s;
    s.primary = 10;
    s.secondary = 5;
    s.attack = 100;
    return s;
  }
};

TEST_F(OffenseTest, BaselineUsesStatAttackAndMastery) {
  EXPECT_DOUBLE_EQ(ExpectedAttackDamage(Baseline(), MakeMob()),
                   kBaseline * kEqualLevel * kBaseCrit);
}

TEST_F(OffenseTest, FullMasteryRemovesMinFloor) {
  OffenseStats s = Baseline();
  s.mastery = 1.0;  // min == max, so expected == max base.
  EXPECT_DOUBLE_EQ(ExpectedAttackDamage(s, MakeMob()),
                   45.0 * kEqualLevel * kBaseCrit);
}

TEST_F(OffenseTest, SkillPctScalesLinearly) {
  OffenseStats s = Baseline();
  s.skill_pct = 2.0;
  EXPECT_DOUBLE_EQ(ExpectedAttackDamage(s, MakeMob()),
                   kBaseline * 2.0 * kEqualLevel * kBaseCrit);
}

TEST_F(OffenseTest, LinesMultiplyDamage) {
  OffenseStats s = Baseline();
  s.lines = 3;
  EXPECT_DOUBLE_EQ(ExpectedAttackDamage(s, MakeMob()),
                   kBaseline * 3.0 * kEqualLevel * kBaseCrit);
}

TEST_F(OffenseTest, DamagePctIsAdditive) {
  OffenseStats s = Baseline();
  s.damage_pct = 0.20;  // *1.2
  EXPECT_DOUBLE_EQ(ExpectedAttackDamage(s, MakeMob()),
                   kBaseline * 1.2 * kEqualLevel * kBaseCrit);
}

TEST_F(OffenseTest, BossPctAppliesOnlyToBosses) {
  OffenseStats s = Baseline();
  s.boss_pct = 0.50;
  EXPECT_DOUBLE_EQ(ExpectedAttackDamage(s, MakeMob()),
                   kBaseline * kEqualLevel * kBaseCrit);
}

// The mirror of boss_pct, and pointedly not in the same place: it joins the
// swing's own percentage, so it is worth its value once per LINE. Three lines
// of 100% carrying 50% land 450%, where 50% of plain damage would land 350%.
TEST_F(OffenseTest, NormalSkillPctJoinsTheSwingOnceALine) {
  OffenseStats s = Baseline();
  s.lines = 3;
  s.normal_skill_pct = 0.50;
  EXPECT_DOUBLE_EQ(ExpectedAttackDamage(s, MakeMob()),
                   kBaseline * 4.5 * kEqualLevel * kBaseCrit);
}

// A boss is what the bonus is not for, so it drops out entirely -- leaving the
// half-elemental every boss takes as the only difference from a plain swing.
TEST_F(OffenseTest, NormalSkillPctIsWorthNothingAgainstABoss) {
  OffenseStats s = Baseline();
  s.normal_skill_pct = 0.50;
  EXPECT_DOUBLE_EQ(ExpectedAttackDamage(s, MakeMob(0, true)),
                   kBaseline * 0.5 * kEqualLevel * kBaseCrit);
}

// A rate of 1 is already every swing, so the base 5% has nowhere to go and the
// figure carries the base bonus alone rather than kBaseCrit on top of it.
TEST_F(OffenseTest, ARateOfOneCritsEverySwingAndNoMore) {
  OffenseStats s = Baseline();
  s.crit_rate = 1.0;
  s.crit_dmg = 0.0;  // only the 0.35 base bonus applies
  EXPECT_DOUBLE_EQ(ExpectedAttackDamage(s, MakeMob()),
                   kBaseline * 1.35 * kEqualLevel);
}

TEST_F(OffenseTest, FinalDamageMultiplies) {
  OffenseStats s = Baseline();
  s.final_dmg_pct = 0.10;
  EXPECT_DOUBLE_EQ(ExpectedAttackDamage(s, MakeMob()),
                   kBaseline * 1.10 * kEqualLevel * kBaseCrit);
}

TEST_F(OffenseTest, TheWeaponConstantScalesTheWholeHit) {
  OffenseStats s = Baseline();
  s.weapon_constant = 1.44;
  EXPECT_DOUBLE_EQ(ExpectedAttackDamage(s, MakeMob()),
                   kBaseline * 1.44 * kEqualLevel * kBaseCrit);
}

TEST_F(OffenseTest, MobDefenseReducesDamage) {
  EXPECT_DOUBLE_EQ(ExpectedAttackDamage(Baseline(), MakeMob(30)),
                   kBaseline * 0.70 * kEqualLevel * kBaseCrit);
}

TEST_F(OffenseTest, IedNegatesMobDefense) {
  OffenseStats s = Baseline();
  s.ied = 1.0;  // fully ignore the 30% PDR
  EXPECT_DOUBLE_EQ(ExpectedAttackDamage(s, MakeMob(30)),
                   kBaseline * kEqualLevel * kBaseCrit);
}

TEST_F(OffenseTest, BossesTakeHalfElementalByDefault) {
  EXPECT_DOUBLE_EQ(ExpectedAttackDamage(Baseline(), MakeMob(0, true)),
                   kBaseline * 0.5 * kEqualLevel * kBaseCrit);
}

TEST_F(OffenseTest, IerRestoresBossElemental) {
  OffenseStats s = Baseline();
  s.ier = 1.0;  // 0.5*(1+1) == 1.0
  EXPECT_DOUBLE_EQ(ExpectedAttackDamage(s, MakeMob(0, true)),
                   kBaseline * kEqualLevel * kBaseCrit);
}

TEST_F(OffenseTest, LevelMultiplierAppliesToOutput) {
  // Attacker at level 0 against a level-5 mob: 5 levels under -> 0.88 penalty.
  EXPECT_DOUBLE_EQ(ExpectedAttackDamage(Baseline(), MakeMob(0, false, 5)),
                   kBaseline * 0.88 * kBaseCrit);
}

TEST_F(OffenseTest, FortyLevelsUnderFloorsToOneDamage) {
  // The level multiplier hits 0 at a 40-level gap; output floors to 1 damage.
  OffenseStats s = Baseline();  // level 0
  EXPECT_DOUBLE_EQ(ExpectedAttackDamage(s, MakeMob(0, false, 40)), 1.0);
}

TEST_F(OffenseTest, CombatPowerIsTheDamageChainWithoutATarget) {
  // The same 27 the baseline swing produces, with the base crit pair and
  // floored -- no mob, so no level multiplier and no defense.
  EXPECT_EQ(CombatPower(Baseline()), 27);
}

TEST_F(OffenseTest, CombatPowerAlwaysCountsBossDamage) {
  OffenseStats s = Baseline();
  s.boss_pct = 0.6;
  // A swing at an ordinary mob ignores this entirely; combat power does not.
  EXPECT_DOUBLE_EQ(ExpectedAttackDamage(s, MakeMob()),
                   kBaseline * kEqualLevel * kBaseCrit);
  EXPECT_EQ(CombatPower(s), 43);  // kBaseline * 1.6 * kBaseCrit
}

TEST_F(OffenseTest, CombatPowerWeightsCritDamageByItsRate) {
  OffenseStats s = Baseline();
  s.crit_rate = 0.5;
  s.crit_dmg = 0.25;
  // kBaseline * (1 + 0.55 * (0.25 + 0.35)) = 35.91 -- the rate carrying the
  // base 5% with it. GMS's flat 1.35 + 0.25 would read 43 here, pricing the
  // crit damage as though every swing crit.
  EXPECT_EQ(CombatPower(s), 35);
}

TEST_F(OffenseTest, CombatPowerRisesWithMastery) {
  OffenseStats s = Baseline();
  s.mastery = 1.0;  // no min-damage floor at all
  EXPECT_EQ(CombatPower(s), 45);
}

// Unlike GMS, which leaves the weapon constant out of the figure it shows.
// Two characters holding different weapon classes really do hit differently
// hard, and a combat power that says otherwise is not worth reading.
TEST_F(OffenseTest, CombatPowerCountsTheWeaponConstant) {
  OffenseStats s = Baseline();
  s.weapon_constant = 1.49;
  EXPECT_EQ(CombatPower(s), 40);  // kBaseline * 1.49 * kBaseCrit
}

TEST_F(OffenseTest, CombatPowerIgnoresTheSwingAndTheTarget) {
  // Everything that depends on which attack is thrown, or at what, drops out.
  OffenseStats s = Baseline();
  s.skill_pct = 3.0;
  s.lines = 4;
  s.ied = 0.8;
  s.ier = 0.5;
  s.level = 60;
  EXPECT_EQ(CombatPower(s), CombatPower(Baseline()));
}

// The published table is by 4th job, so ours is by weapon with the line that
// owns it. Spot-checked against the figures on the Damage Formula page.
TEST(WeaponConstantTest, EachWeaponCarriesItsOwnLinesConstant) {
  EXPECT_DOUBLE_EQ(WeaponConstant(JOB_SWORDMAN, EQUIP_TYPE_ONE_HANDED_SWORD),
                   1.24);
  EXPECT_DOUBLE_EQ(WeaponConstant(JOB_SPEARMAN, EQUIP_TYPE_SPEAR), 1.49);
  EXPECT_DOUBLE_EQ(WeaponConstant(JOB_SPEARMAN, EQUIP_TYPE_POLEARM), 1.49);
  EXPECT_DOUBLE_EQ(WeaponConstant(JOB_ARCHER, EQUIP_TYPE_BOW), 1.30);
  EXPECT_DOUBLE_EQ(WeaponConstant(JOB_ARCHER, EQUIP_TYPE_CROSSBOW), 1.35);
  EXPECT_DOUBLE_EQ(WeaponConstant(JOB_MAGICIAN, EQUIP_TYPE_STAFF), 1.20);
  EXPECT_DOUBLE_EQ(WeaponConstant(JOB_ROGUE, EQUIP_TYPE_DAGGER), 1.30);
  EXPECT_DOUBLE_EQ(WeaponConstant(JOB_ROGUE, EQUIP_TYPE_CLAW), 1.75);
}

// The whole reason the constant takes a job at all: the same sword is worth
// more to the Hero line than to the Paladin line the default comes from.
TEST(WeaponConstantTest, AFighterSwingsASwordHarderThanAnyoneElse) {
  EXPECT_DOUBLE_EQ(WeaponConstant(JOB_FIGHTER, EQUIP_TYPE_ONE_HANDED_SWORD),
                   1.34);
  EXPECT_DOUBLE_EQ(WeaponConstant(JOB_FIGHTER, EQUIP_TYPE_TWO_HANDED_SWORD),
                   1.44);
  EXPECT_DOUBLE_EQ(WeaponConstant(JOB_PAGE, EQUIP_TYPE_ONE_HANDED_SWORD), 1.24);
  EXPECT_DOUBLE_EQ(WeaponConstant(JOB_PAGE, EQUIP_TYPE_TWO_HANDED_SWORD), 1.34);
}

// A line's constant belongs to the line, not to one rung of it: a Crusader is
// still on the Hero line and a Berserker still on the Dark Knight's. Checked
// against the 2nd job rather than against a figure, so the pair cannot drift.
TEST(WeaponConstantTest, AThirdJobSwingsWhatItsLineSwings) {
  const EquipType kWeapons[] = {
      EQUIP_TYPE_ONE_HANDED_SWORD,
      EQUIP_TYPE_TWO_HANDED_SWORD,
      EQUIP_TYPE_ONE_HANDED_AXE,
      EQUIP_TYPE_TWO_HANDED_AXE,
      EQUIP_TYPE_SPEAR,
      EQUIP_TYPE_POLEARM,
  };
  for (EquipType weapon : kWeapons) {
    EXPECT_DOUBLE_EQ(WeaponConstant(JOB_CRUSADER, weapon),
                     WeaponConstant(JOB_FIGHTER, weapon))
        << EquipType_Name(weapon);
    EXPECT_DOUBLE_EQ(WeaponConstant(JOB_BERSERKER, weapon),
                     WeaponConstant(JOB_SPEARMAN, weapon))
        << EquipType_Name(weapon);
  }
}

// An axe is the Hero line's own weapon, so the default already is theirs and
// no override is needed. A Page holding one is not a case GMS has.
TEST(WeaponConstantTest, AnAxeIsTheSameInAnyWarriorsHands) {
  EXPECT_DOUBLE_EQ(WeaponConstant(JOB_FIGHTER, EQUIP_TYPE_TWO_HANDED_AXE),
                   1.44);
  EXPECT_DOUBLE_EQ(WeaponConstant(JOB_SWORDMAN, EQUIP_TYPE_TWO_HANDED_AXE),
                   1.44);
}

// A weapon its holder's line has no figure for falls back to the line that
// does own it, rather than to nothing: a Fighter with a blunt is off-class,
// not unarmed. Only a character holding nothing at all gets the identity.
TEST(WeaponConstantTest, AnOffClassWeaponKeepsItsOwnConstant) {
  EXPECT_DOUBLE_EQ(WeaponConstant(JOB_FIGHTER, EQUIP_TYPE_TWO_HANDED_BLUNT),
                   1.34);
  EXPECT_DOUBLE_EQ(WeaponConstant(JOB_FIGHTER, EQUIP_TYPE_ONE_HANDED_BLUNT),
                   1.24);
  EXPECT_DOUBLE_EQ(WeaponConstant(JOB_FIGHTER, EQUIP_TYPE_UNSPECIFIED), 1.0);
  EXPECT_DOUBLE_EQ(WeaponConstant(JOB_FIGHTER, EQUIP_TYPE_THROWING_STAR), 1.0);
}

// The constant has to come off the weapon in hand rather than off the summed
// stats, which no longer say what is being held.
TEST(OffenseStatsForTest, TheWeaponInHandDecidesTheConstant) {
  OffenseStats offense =
      OffenseStatsFor(JOB_SPEARMAN, 30, AllocatedStats(), EquipStats(),
                      EQUIP_TYPE_POLEARM, nullptr, 0);
  EXPECT_DOUBLE_EQ(offense.weapon_constant, 1.49);
}

TEST(SkillLinesAtTest, ASkillWithNoLadderStrikesTheSameAtEveryLevel) {
  Skill skill;
  EXPECT_EQ(SkillLinesAt(skill, 1), 1);  // unset is one strike
  EXPECT_EQ(SkillLinesAt(skill, 30), 1);
  skill.set_lines(4);
  EXPECT_EQ(SkillLinesAt(skill, 1), 4);
  EXPECT_EQ(SkillLinesAt(skill, 30), 4);
}

// Dark Impale's ladder: two strikes bought over thirty levels, so the second
// lands exactly on the level Combat Orders can reach.
TEST(SkillLinesAtTest, TheLadderBuysAStrikeAtTheLevelItPaysFor) {
  Skill skill;
  skill.set_lines(5);
  skill.set_lines_per_level(2.0 / 30.0);
  EXPECT_EQ(SkillLinesAt(skill, 1), 5);
  EXPECT_EQ(SkillLinesAt(skill, 15), 5);
  EXPECT_EQ(SkillLinesAt(skill, 16), 6);
  EXPECT_EQ(SkillLinesAt(skill, 30), 6);
  EXPECT_EQ(SkillLinesAt(skill, 31), 7);
  EXPECT_EQ(SkillLinesAt(skill, 32), 7);
}

TEST(ComboOrbsAtTest, ARingWithNoLadderIsTheSameAtEveryLevel) {
  Skill skill;
  EXPECT_EQ(ComboOrbsAt(skill, 1), 0);  // most skills hand out none
  EXPECT_EQ(ComboOrbsAt(skill, 30), 0);
  skill.set_combo_orbs(5);
  EXPECT_EQ(ComboOrbsAt(skill, 1), 5);
  EXPECT_EQ(ComboOrbsAt(skill, 30), 5);
}

// Advanced Combo's ladder, as the textproto carries it: five orbs bought over
// twenty levels, the fifth landing exactly on the master level.
TEST(ComboOrbsAtTest, ADecimalRateStillLandsOnTheLevelItNames) {
  Skill skill;
  skill.set_combo_orbs(5);
  skill.set_combo_orbs_per_level(0.26316);
  EXPECT_EQ(ComboOrbsAt(skill, 1), 5);
  EXPECT_EQ(ComboOrbsAt(skill, 4), 5);
  EXPECT_EQ(ComboOrbsAt(skill, 5), 6);
  EXPECT_EQ(ComboOrbsAt(skill, 20), 10);
  EXPECT_EQ(ComboOrbsAt(skill, 22), 10);  // Combat Orders buys no eleventh
}

TEST(SkillLinesAtTest, ADecimalRateStillLandsOnTheLevelItNames) {
  // What the textproto actually carries, rounded where a decimal must be.
  Skill skill;
  skill.set_lines(5);
  skill.set_lines_per_level(0.0666667);
  EXPECT_EQ(SkillLinesAt(skill, 16), 6);
  EXPECT_EQ(SkillLinesAt(skill, 31), 7);
}

TEST(OffenseStatsForTest, TheLineLadderReachesTheDamageChain) {
  Skill skill;
  skill.set_kind(SKILL_KIND_ATTACK);
  skill.set_lines(5);
  skill.set_lines_per_level(2.0 / 30.0);
  skill.mutable_base()->set_skill_pct(1.0);
  OffenseStats early =
      OffenseStatsFor(JOB_SPEARMAN, 30, AllocatedStats(), EquipStats(),
                      EQUIP_TYPE_POLEARM, &skill, 15);
  OffenseStats late =
      OffenseStatsFor(JOB_SPEARMAN, 30, AllocatedStats(), EquipStats(),
                      EQUIP_TYPE_POLEARM, &skill, 16);
  EXPECT_EQ(early.lines, 5);
  EXPECT_EQ(late.lines, 6);
  // The shadow copies whatever the swing turned out to be.
  EXPECT_EQ(late.mirror_lines, 6);
}

TEST(SwingIntervalTest, Stage4IsTheUnscaledBase) {
  // stage 4 => (20-4)/16 == 1.0; 720 is already a 30ms multiple.
  EXPECT_DOUBLE_EQ(SwingIntervalSeconds(720, 4), 0.72);
}

TEST(SwingIntervalTest, WorkedExampleStage8) {
  // 660 * (20-8)/16 = 495 -> ceil to 510ms.
  EXPECT_DOUBLE_EQ(SwingIntervalSeconds(660, 8), 0.51);
}

TEST(SwingIntervalTest, RoundsUpToNextTick) {
  // 100 * 1.0 = 100 -> ceil(100/30)=4 ticks -> 120ms.
  EXPECT_DOUBLE_EQ(SwingIntervalSeconds(100, 4), 0.12);
}

TEST(SwingIntervalTest, ExactTickMultipleIsUnchanged) {
  // 600 is exactly 20 ticks; ceil must not bump it.
  EXPECT_DOUBLE_EQ(SwingIntervalSeconds(600, 4), 0.60);
}

TEST(SwingIntervalTest, FastestStageIsQuickest) {
  // stage 10 => (20-10)/16 = 0.625; 800*0.625 = 500 -> ceil to 510ms.
  EXPECT_DOUBLE_EQ(SwingIntervalSeconds(800, 10), 0.51);
}

TEST(SwingIntervalTest, SlowestStageIsSlowerThanBase) {
  // stage 1 => 19/16 = 1.1875; 800*1.1875 = 950 -> ceil to 960ms.
  EXPECT_DOUBLE_EQ(SwingIntervalSeconds(800, 1), 0.96);
}

// GMS holds a magician's weapon out of the timing entirely: every cast starts
// at the unscaled stage, however slow the staff in hand is.
TEST(BaseAttackSpeedStageTest, EveryMagicianCastsAtTheUnscaledStage) {
  const Job kMagicians[] = {
      JOB_MAGICIAN, JOB_ICE_LIGHTNING_WIZARD, JOB_FIRE_POISON_WIZARD,
      JOB_CLERIC,   JOB_ICE_LIGHTNING_MAGE,   JOB_FIRE_POISON_MAGE,
      JOB_PRIEST};
  for (Job job : kMagicians) {
    EXPECT_EQ(BaseAttackSpeedStage(job, ATTACK_SPEED_SLOW_1),
              kUnscaledAttackSpeedStage)
        << Job_Name(job);
    // Not a floor on a slow weapon -- a fast one is ignored just the same.
    EXPECT_EQ(BaseAttackSpeedStage(job, ATTACK_SPEED_FASTEST_3),
              kUnscaledAttackSpeedStage)
        << Job_Name(job);
  }
}

TEST(BaseAttackSpeedStageTest, EveryOtherJobStartsAtItsWeapon) {
  EXPECT_EQ(BaseAttackSpeedStage(JOB_FIGHTER, ATTACK_SPEED_FAST_1),
            ATTACK_SPEED_FAST_1);
  EXPECT_EQ(BaseAttackSpeedStage(JOB_HERMIT, ATTACK_SPEED_FAST_2),
            ATTACK_SPEED_FAST_2);
  EXPECT_EQ(BaseAttackSpeedStage(JOB_SNIPER, ATTACK_SPEED_SLOWER),
            ATTACK_SPEED_SLOWER);
}

TEST(LevelMultiplierTest, EqualLevelGivesTenPercentBonus) {
  EXPECT_DOUBLE_EQ(LevelMultiplier(10, 10), 1.1);
}

TEST(LevelMultiplierTest, TheBonusCapsAtFiveLevelsAbove) {
  EXPECT_DOUBLE_EQ(LevelMultiplier(12, 10), 1.14);  // +2
  EXPECT_DOUBLE_EQ(LevelMultiplier(15, 10), 1.2);   // +5
  EXPECT_DOUBLE_EQ(LevelMultiplier(100, 10), 1.2);  // capped past +5
}

TEST(LevelMultiplierTest, StaysAboveOneUntilThreeLevelsUnder) {
  EXPECT_DOUBLE_EQ(LevelMultiplier(9, 10), 1.0584);  // -1
  EXPECT_DOUBLE_EQ(LevelMultiplier(8, 10), 1.007);   // -2, still a bonus
  EXPECT_DOUBLE_EQ(LevelMultiplier(7, 10), 0.9672);  // -3, first penalty
}

TEST(LevelMultiplierTest, DeepUnderLevelIsHeavilyPenalized) {
  EXPECT_DOUBLE_EQ(LevelMultiplier(1, 21), 0.5);   // -20
  EXPECT_DOUBLE_EQ(LevelMultiplier(1, 40), 0.03);  // -39, last nonzero row
}

TEST(LevelMultiplierTest, FortyOrMoreLevelsUnderIsZero) {
  EXPECT_DOUBLE_EQ(LevelMultiplier(1, 41), 0.0);   // -40
  EXPECT_DOUBLE_EQ(LevelMultiplier(1, 100), 0.0);  // far under
}

// Stage 4 is the reference the whole scale is built around: a swing there takes
// exactly as long as its animation says.
TEST(SwingIntervalSecondsTest, AverageSpeedIsTheAnimationItself) {
  EXPECT_DOUBLE_EQ(SwingIntervalSeconds(780, ATTACK_SPEED_AVERAGE), 0.78);
  EXPECT_DOUBLE_EQ(
      SwingIntervalSeconds(kDefaultSwingDelayMs, ATTACK_SPEED_AVERAGE), 0.78);
}

TEST(OffenseStatsForTest, SumsAllocatedAndEquippedStats) {
  AllocatedStats allocated;
  allocated.set_str(13);
  allocated.set_dex(4);
  EquipStats equipped;
  equipped.set_str(10);  // e.g. a stat-bearing piece
  equipped.set_dex(2);
  equipped.set_attack(15);  // the Sword

  OffenseStats offense = OffenseStatsFor(JOB_BEGINNER, 7, allocated, equipped,
                                         EQUIP_TYPE_UNSPECIFIED, nullptr, 0);
  EXPECT_EQ(offense.primary, 23);   // 13 + 10
  EXPECT_EQ(offense.secondary, 6);  // 4 + 2
  EXPECT_EQ(offense.attack, 15);
  EXPECT_EQ(offense.level, 7);
}

TEST(OffenseStatsForTest, WarriorUsesStrPrimaryDexSecondary) {
  AllocatedStats allocated;
  allocated.set_str(100);
  allocated.set_dex(20);
  OffenseStats offense =
      OffenseStatsFor(JOB_SWORDMAN, 1, allocated, EquipStats(),
                      EQUIP_TYPE_UNSPECIFIED, nullptr, 0);
  EXPECT_EQ(offense.primary, 100);
  EXPECT_EQ(offense.secondary, 20);
}

// Three switches in this file answer per job, and every one of them has a
// default a new job falls through silently: a Bishop read a warrior's stats,
// swung on weapon attack they do not have and dealt nothing at all. Walked off
// the enum rather than listed, so the next job joins by existing.
TEST(OffenseStatsForTest, EveryJobIsNamedByTheDamageChain) {
  AllocatedStats allocated;
  allocated.set_str(101);
  allocated.set_dex(102);
  allocated.set_int_(103);
  allocated.set_luk(104);
  EquipStats equipped;
  equipped.set_attack(50);
  equipped.set_magic_attack(70);
  // Held against the job's own primary stat rather than against a list: what
  // is asked is that the damage chain and the stats page agree about a job.
  const std::map<StatField, int> kValue = {{STAT_FIELD_STR, 101},
                                           {STAT_FIELD_DEX, 102},
                                           {STAT_FIELD_INT, 103},
                                           {STAT_FIELD_LUK, 104}};
  const google::protobuf::EnumDescriptor* jobs = Job_descriptor();
  for (int i = 0; i < jobs->value_count(); ++i) {
    Job job = static_cast<Job>(jobs->value(i)->number());
    if (job == JOB_UNSPECIFIED) {
      continue;
    }
    StatField primary = PrimaryStatField(job);
    ASSERT_TRUE(kValue.count(primary)) << Job_Name(job) << " has no primary";
    OffenseStats offense = OffenseStatsFor(job, 1, allocated, equipped,
                                           EQUIP_TYPE_UNSPECIFIED, nullptr, 0);
    // A job in no branch of the stat switch reads nothing at all, and one in
    // the wrong branch reads its neighbour's stat.
    EXPECT_EQ(offense.primary, kValue.at(primary)) << Job_Name(job);
    // The number the branch swings on, and the mastery it holds a weapon by.
    // A magician swings on magic attack and masters a wand; everyone whose
    // primary stat is DEX draws a bow, and the rest swing something melee.
    bool magic = primary == STAT_FIELD_INT;
    EXPECT_EQ(offense.attack, magic ? 70 : 50) << Job_Name(job);
    Job like = JOB_SWORDMAN;
    if (magic) {
      like = JOB_MAGICIAN;
    } else if (primary == STAT_FIELD_DEX) {
      like = JOB_ARCHER;
    }
    EXPECT_DOUBLE_EQ(BaseMastery(job), BaseMastery(like)) << Job_Name(job);
  }
}

TEST(OffenseStatsForTest, GearGraduatesBossPctAndIed) {
  EquipStats equipped;
  equipped.set_boss_damage(30);           // 30%
  equipped.set_ignore_enemy_defense(20);  // 20%
  OffenseStats offense =
      OffenseStatsFor(JOB_SWORDMAN, 1, AllocatedStats(), equipped,
                      EQUIP_TYPE_UNSPECIFIED, nullptr, 0);
  EXPECT_DOUBLE_EQ(offense.boss_pct, 0.30);
  EXPECT_DOUBLE_EQ(offense.ied, 0.20);
}

// Boss damage from gear and from a passive are the same quantity from two
// places, so they add -- unlike IED, which meets in reverse just below.
TEST(OffenseStatsForTest, WornAndLearnedBossDamageAdd) {
  EquipStats equipped;
  equipped.set_boss_damage(30);
  PassiveOffense passives;
  passives.boss_pct = 0.10;
  OffenseStats offense =
      OffenseStatsFor(JOB_ROGUE, 1, AllocatedStats(), equipped,
                      EQUIP_TYPE_UNSPECIFIED, nullptr, 0, passives);
  EXPECT_DOUBLE_EQ(offense.boss_pct, 0.40);
}

TEST(OffenseStatsForTest, WornAndLearnedIedMeetInReverse) {
  EquipStats equipped;
  equipped.set_ignore_enemy_defense(30);
  PassiveOffense passives;
  passives.ied = 0.40;
  OffenseStats offense =
      OffenseStatsFor(JOB_SWORDMAN, 1, AllocatedStats(), equipped,
                      EQUIP_TYPE_UNSPECIFIED, nullptr, 0, passives);
  // Summed they would be 70%; what is left of the armour is 0.70 * 0.60.
  EXPECT_DOUBLE_EQ(offense.ied, 1.0 - 0.70 * 0.60);
}

// Gungnir's Descent ignores 30% of a monster's defence and Heaven's Hammer
// hits a boss 30% harder, and GMS means both only while that skill is the one
// landing. The character's own share is already in, so the swing's meets it
// the way a second source does.
// Heaven's Hammer comes back sooner the further it is taught: 29 seconds down
// to 15 over its thirty levels, which is GMS's 30 - floor(L/2) walked as a
// line. The step is negative and the pair never falls below nothing.
TEST(CooldownAtTest, AWaitShortensAsTheSkillIsTaught) {
  Skill hammer;
  hammer.set_cooldown_seconds(29.5);
  hammer.set_cooldown_seconds_per_level(-0.5);
  EXPECT_DOUBLE_EQ(CooldownAt(hammer, 1), 29.5);
  EXPECT_DOUBLE_EQ(CooldownAt(hammer, 30), 15.0);

  // A wait with no step holds wherever it is read, and a skill with no wait
  // has none at any level.
  Skill flat;
  flat.set_cooldown_seconds(7.0);
  EXPECT_DOUBLE_EQ(CooldownAt(flat, 1), 7.0);
  EXPECT_DOUBLE_EQ(CooldownAt(flat, 30), 7.0);
  EXPECT_DOUBLE_EQ(CooldownAt(Skill(), 30), 0.0);

  // A step steep enough to run the wait past zero leaves no wait at all,
  // rather than handing the fight a negative one.
  Skill steep;
  steep.set_cooldown_seconds(5.0);
  steep.set_cooldown_seconds_per_level(-1.0);
  EXPECT_DOUBLE_EQ(CooldownAt(steep, 20), 0.0);
}

TEST(OffenseStatsForTest, AnAttacksOwnLeversRideThatSwing) {
  Skill gungnir;
  gungnir.set_kind(SKILL_KIND_ATTACK);
  gungnir.mutable_base()->set_skill_pct(1.96);
  gungnir.mutable_base()->set_ied_pct(0.01);
  gungnir.mutable_per_level()->set_ied_pct(0.01);
  gungnir.mutable_base()->set_boss_pct(0.30);
  gungnir.mutable_base()->set_final_dmg_pct(0.20);
  PassiveOffense passives;
  passives.ied = 0.40;
  passives.boss_pct = 0.10;
  passives.final_dmg_pct = 0.50;

  OffenseStats offense =
      OffenseStatsFor(JOB_SWORDMAN, 1, AllocatedStats(), EquipStats(),
                      EQUIP_TYPE_UNSPECIFIED, &gungnir, 30, passives);
  // 30% at level 30, meeting the character's 40% in reverse rather than
  // summing.
  EXPECT_DOUBLE_EQ(offense.ied, 1.0 - 0.60 * 0.70);
  EXPECT_DOUBLE_EQ(offense.boss_pct, 0.40);
  // Final damage multiplies where the two above do not: 1.5 x 1.2.
  EXPECT_DOUBLE_EQ(offense.final_dmg_pct, 0.80);

  // The swing after it carries none of them: what the character has is all it
  // has.
  OffenseStats bare =
      OffenseStatsFor(JOB_SWORDMAN, 1, AllocatedStats(), EquipStats(),
                      EQUIP_TYPE_UNSPECIFIED, nullptr, 0, passives);
  EXPECT_DOUBLE_EQ(bare.ied, 0.40);
  EXPECT_DOUBLE_EQ(bare.boss_pct, 0.10);
  EXPECT_DOUBLE_EQ(bare.final_dmg_pct, 0.50);
}

TEST(OffenseStatsForTest, EachIedSourceOnlyTakesAShareOfWhatIsLeft) {
  EXPECT_DOUBLE_EQ(CombineIgnoredDefense(0.30, 0.40), 0.58);
  // Three halves leave an eighth rather than cancelling the armour outright,
  // which is what summing them to 150% would do. Each source is worth less
  // than the one before it: the second takes half of the half left standing.
  double one = CombineIgnoredDefense(0.0, 0.50);
  double two = CombineIgnoredDefense(one, 0.50);
  double three = CombineIgnoredDefense(two, 0.50);
  EXPECT_DOUBLE_EQ(three, 0.875);
  EXPECT_GT(two - one, three - two);
}

TEST(OffenseStatsForTest, ANamedBoostRaisesOnlyThatSkillsLevers) {
  Skill wind;
  wind.set_name("Wind Arrow");
  wind.set_kind(SKILL_KIND_ATTACK);
  wind.set_lines(3);
  wind.mutable_base()->set_skill_pct(1.78);
  Skill other = wind;
  other.set_name("Arrow Blaster");

  PassiveOffense passives;
  passives.boss_pct = 0.10;
  passives.ied = 0.20;
  passives.final_dmg_pct = 0.10;
  SkillBonus& bonus = passives.skill_bonus["Wind Arrow"];
  bonus.skill_pct = 0.70;
  bonus.damage_pct = 1.50;
  bonus.boss_pct = 0.30;
  bonus.ied = 0.20;
  bonus.crit_rate = 0.20;
  bonus.final_dmg_pct = 0.15;

  OffenseStats boosted =
      OffenseStatsFor(JOB_ARCHER, 1, AllocatedStats(), EquipStats(),
                      EQUIP_TYPE_BOW, &wind, 1, passives);
  OffenseStats untouched =
      OffenseStatsFor(JOB_ARCHER, 1, AllocatedStats(), EquipStats(),
                      EQUIP_TYPE_BOW, &other, 1, passives);

  // Damage is added to the multiplier, so it is worth its value once per line:
  // three lines of 178% become three of 248%.
  EXPECT_DOUBLE_EQ(boosted.skill_pct, 2.48);
  EXPECT_EQ(boosted.lines, 3);
  // Each of the rest meets what the character brought the way two sources of
  // it always meet.
  EXPECT_DOUBLE_EQ(boosted.damage_pct, untouched.damage_pct + 1.50);
  EXPECT_DOUBLE_EQ(boosted.boss_pct, untouched.boss_pct + 0.30);
  EXPECT_DOUBLE_EQ(boosted.ied, CombineIgnoredDefense(untouched.ied, 0.20));
  EXPECT_DOUBLE_EQ(boosted.crit_rate, untouched.crit_rate + 0.20);
  EXPECT_DOUBLE_EQ(boosted.final_dmg_pct,
                   (1.0 + untouched.final_dmg_pct) * 1.15 - 1.0);
  // The skill it does not name keeps every one of them.
  EXPECT_DOUBLE_EQ(untouched.skill_pct, 1.78);
  EXPECT_DOUBLE_EQ(untouched.damage_pct, 0.0);
  EXPECT_DOUBLE_EQ(untouched.boss_pct, 0.10);
  EXPECT_DOUBLE_EQ(untouched.ied, 0.20);
}

// The mastery a bare character of `job` swings at, holding `passives`.
double MasteryFor(Job job, const PassiveOffense& passives) {
  return OffenseStatsFor(job, 30, AllocatedStats(), EquipStats(),
                         EQUIP_TYPE_UNSPECIFIED, nullptr, 0, passives)
      .mastery;
}

// The whole contract: whatever rolls, the average is the damage already
// worked out. Only that keeps the fight and the sims agreeing.
TEST(RollFactorTest, AveragesToOne) {
  SwingRolls rolls;
  rolls.lines = 4;
  rolls.mirror_lines = 4;
  rolls.mirror_pct = 0.7;
  rolls.mastery = 0.5;
  rolls.crit_rate = 0.3;
  rolls.crit_dmg = 1.0;
  std::mt19937 rng(1234);
  double total = 0.0;
  const int kRuns = 200000;
  for (int i = 0; i < kRuns; ++i) {
    total += RollFactor(rolls, rng);
  }
  EXPECT_NEAR(total / kRuns, 1.0, 0.005);
}

// The lines a caller is handed are the landing broken up: one per hit the
// swing and its shadow made, summing to the factor the monster loses.
TEST(RollFactorTest, TheLinesSumToTheFactor) {
  SwingRolls rolls;
  rolls.lines = 5;
  rolls.mirror_lines = 5;
  rolls.mirror_pct = 0.5;
  rolls.mastery = 0.3;
  rolls.crit_rate = 0.5;
  rolls.crit_dmg = 1.0;
  std::mt19937 rng(99);
  std::vector<LineRoll> lines;
  bool crit_seen = false;
  bool plain_seen = false;
  for (int i = 0; i < 200; ++i) {
    double factor = RollFactor(rolls, rng, &lines);
    ASSERT_EQ(lines.size(), 10u);
    double total = 0.0;
    for (const LineRoll& line : lines) {
      total += line.share;
      crit_seen = crit_seen || line.crit;
      plain_seen = plain_seen || !line.crit;
    }
    EXPECT_NEAR(total, factor, 1e-12);
  }
  EXPECT_TRUE(crit_seen);
  EXPECT_TRUE(plain_seen);
}

// Every character carries a shadow's worth of copies and almost none of them
// has a shadow. Those copies land nothing, so there is nothing to draw for
// them -- but they are still rolled, or the fight would play out differently
// for everyone.
TEST(RollFactorTest, ShadowCopiesWorthNothingAreNotDrawn) {
  SwingRolls rolls;
  rolls.lines = 3;
  rolls.mirror_lines = 3;
  rolls.mirror_pct = 0.0;
  rolls.mastery = 0.5;
  std::mt19937 with_sink(5);
  std::mt19937 without(5);
  std::vector<LineRoll> lines;
  for (int i = 0; i < 20; ++i) {
    EXPECT_DOUBLE_EQ(RollFactor(rolls, with_sink, &lines),
                     RollFactor(rolls, without));
    EXPECT_EQ(lines.size(), 3u);
  }
}

// A swing that rolls nothing still landed once, so it has a line to draw.
TEST(RollFactorTest, ASwingThatRollsNothingIsOneLine) {
  std::mt19937 rng(3);
  std::vector<LineRoll> lines = {{9.0, true}};
  EXPECT_DOUBLE_EQ(RollFactor(SwingRolls(), rng, &lines), 1.0);
  ASSERT_EQ(lines.size(), 1u);
  EXPECT_DOUBLE_EQ(lines[0].share, 1.0);
  EXPECT_FALSE(lines[0].crit);
}

// A caller filling in nothing gets no variance, which is what lets an attack
// built by hand land exactly the damage it was given.
TEST(RollFactorTest, DefaultsRollNothing) {
  std::mt19937 rng(1);
  for (int i = 0; i < 20; ++i) {
    EXPECT_DOUBLE_EQ(RollFactor(SwingRolls(), rng), 1.0);
  }
}

// One line, so the factor is the roll itself: never below the mastery floor
// over the mean, never above a crit at full roll, and both ends reached.
TEST(RollFactorTest, StaysWithinTheFloorAndTheCrit) {
  SwingRolls rolls;
  rolls.mastery = 0.4;
  rolls.crit_rate = 0.5;
  rolls.crit_dmg = 0.8;
  double mean = 0.7 * 1.4;
  std::mt19937 rng(7);
  double low = 2.0;
  double high = 0.0;
  for (int i = 0; i < 5000; ++i) {
    double factor = RollFactor(rolls, rng);
    EXPECT_GE(factor, 0.4 / mean);
    EXPECT_LE(factor, 1.8 / mean);
    low = std::min(low, factor);
    high = std::max(high, factor);
  }
  EXPECT_LT(low, 0.5 / mean);   // a non-crit near the floor turned up
  EXPECT_GT(high, 1.7 / mean);  // so did a crit at full roll
}

// The rolls come off the stats the chain already holds, with the base crit
// pair folded in -- what varies is the whole chance, not the bought share.
TEST(RollFactorTest, RollsForTakesTheWholeCritChance) {
  OffenseStats offense;
  offense.lines = 3;
  offense.mirror_lines = 3;
  offense.mirror_pct = 0.5;
  offense.mastery = 0.9;
  offense.crit_rate = 0.20;
  offense.crit_dmg = 0.15;
  SwingRolls rolls = RollsFor(offense);
  EXPECT_EQ(rolls.lines, 3);
  EXPECT_EQ(rolls.mirror_lines, 3);
  EXPECT_DOUBLE_EQ(rolls.mirror_pct, 0.5);
  EXPECT_DOUBLE_EQ(rolls.mastery, 0.9);
  EXPECT_DOUBLE_EQ(rolls.crit_rate, 0.20 + kBaseCritRate);
  EXPECT_DOUBLE_EQ(rolls.crit_dmg, 0.15 + kBaseCritDamage);
}

TEST(OffenseStatsForTest, DefaultsAreUntouchedWithoutGear) {
  OffenseStats offense =
      OffenseStatsFor(JOB_BEGINNER, 1, AllocatedStats(), EquipStats(),
                      EQUIP_TYPE_UNSPECIFIED, nullptr, 0);
  EXPECT_DOUBLE_EQ(offense.mastery, 0.20);
  EXPECT_DOUBLE_EQ(offense.skill_pct, 1.0);
  EXPECT_DOUBLE_EQ(offense.boss_pct, 0.0);
  EXPECT_DOUBLE_EQ(offense.ied, 0.0);
}

TEST(OffenseStatsForTest, ArcherReadsDexAsTheMainStat) {
  AllocatedStats allocated;
  allocated.set_str(30);
  allocated.set_dex(50);
  EquipStats equipped;
  equipped.set_str(3);
  equipped.set_dex(5);
  OffenseStats offense = OffenseStatsFor(JOB_ARCHER, 1, allocated, equipped,
                                         EQUIP_TYPE_UNSPECIFIED, nullptr, 0);
  EXPECT_EQ(offense.primary, 55);    // DEX leads for an archer
  EXPECT_EQ(offense.secondary, 33);  // and STR backs it up
}

TEST(OffenseStatsForTest, MagicianReadsIntAndSwingsOnMagicAttack) {
  AllocatedStats allocated;
  allocated.set_int_(50);
  allocated.set_luk(30);
  EquipStats equipped;
  equipped.set_int_(5);
  equipped.set_luk(3);
  equipped.set_attack(999);  // a wand carries no weapon attack
  equipped.set_magic_attack(70);
  OffenseStats offense = OffenseStatsFor(JOB_MAGICIAN, 1, allocated, equipped,
                                         EQUIP_TYPE_UNSPECIFIED, nullptr, 0);
  EXPECT_EQ(offense.primary, 55);
  EXPECT_EQ(offense.secondary, 33);
  EXPECT_EQ(offense.attack, 70);  // magic attack, not the 999
}

TEST(OffenseStatsForTest, RogueReadsLukAsTheMainStat) {
  AllocatedStats allocated;
  allocated.set_luk(50);
  allocated.set_dex(30);
  EquipStats equipped;
  equipped.set_luk(5);
  equipped.set_dex(3);
  OffenseStats offense = OffenseStatsFor(JOB_ROGUE, 1, allocated, equipped,
                                         EQUIP_TYPE_UNSPECIFIED, nullptr, 0);
  EXPECT_EQ(offense.primary, 55);
  EXPECT_EQ(offense.secondary, 33);  // and DEX backs it up
}

TEST(OffenseStatsForTest, NonMagiciansIgnoreMagicAttack) {
  EquipStats equipped;
  equipped.set_attack(40);
  equipped.set_magic_attack(999);
  OffenseStats offense =
      OffenseStatsFor(JOB_ARCHER, 1, AllocatedStats(), equipped,
                      EQUIP_TYPE_UNSPECIFIED, nullptr, 0);
  EXPECT_EQ(offense.attack, 40);
}

TEST(OffenseStatsForTest, PassiveCritRateReachesTheOffense) {
  PassiveOffense passives;
  passives.crit_rate = 0.40;
  OffenseStats offense =
      OffenseStatsFor(JOB_ARCHER, 15, AllocatedStats(), EquipStats(),
                      EQUIP_TYPE_UNSPECIFIED, nullptr, 0, passives);
  EXPECT_DOUBLE_EQ(offense.crit_rate, 0.40);
}

// The base is the line's, and the skill adds to it -- so the warrior ends a
// 2nd job book at 70, the archer a 4th at 85 and the magician a 2nd at 75.
TEST(OffenseStatsForTest, MasteryAddsTheSkillToTheLineBase) {
  PassiveOffense none;
  EXPECT_DOUBLE_EQ(MasteryFor(JOB_SWORDMAN, none), 0.20);
  EXPECT_DOUBLE_EQ(MasteryFor(JOB_ARCHER, none), 0.15);
  EXPECT_DOUBLE_EQ(MasteryFor(JOB_MAGICIAN, none), 0.25);
  EXPECT_DOUBLE_EQ(MasteryFor(JOB_ROGUE, none), 0.20);

  PassiveOffense second;
  second.mastery = 0.50;
  EXPECT_DOUBLE_EQ(MasteryFor(JOB_SWORDMAN, second), 0.70);
  EXPECT_DOUBLE_EQ(MasteryFor(JOB_MAGICIAN, second), 0.75);

  PassiveOffense fourth;
  fourth.mastery = 0.70;
  EXPECT_DOUBLE_EQ(MasteryFor(JOB_BOW_MASTER, fourth), 0.85);
  EXPECT_DOUBLE_EQ(MasteryFor(JOB_HERO, fourth), 0.90);
}

// Adding rather than taking the better of the two is what makes the ladder
// monotonic: a mastery skill's first level grants 12%, under every line's
// base, and taking the better of the two would have thrown it away.
TEST(OffenseStatsForTest, TheFirstLevelOfAMasterySkillStillPays) {
  PassiveOffense first;
  first.mastery = 0.12;
  EXPECT_GT(MasteryFor(JOB_SWORDMAN, first), MasteryFor(JOB_SWORDMAN, {}));
}

TEST(OffenseStatsForTest, UnknownJobYieldsZeroMainStats) {
  AllocatedStats allocated;
  allocated.set_str(50);
  allocated.set_dex(30);
  OffenseStats offense =
      OffenseStatsFor(JOB_UNSPECIFIED, 1, allocated, EquipStats(),
                      EQUIP_TYPE_UNSPECIFIED, nullptr, 0);
  EXPECT_EQ(offense.primary, 0);  // fail safe: unknown job has no main stat
  EXPECT_EQ(offense.secondary, 0);
}

// Slash Blast: 183% at level 1, +8% per level.
Skill SlashBlast() {
  Skill skill;
  skill.set_name("Slash Blast");
  skill.set_kind(SKILL_KIND_ATTACK);
  skill.set_max_level(20);
  skill.mutable_base()->set_skill_pct(1.83);
  skill.mutable_per_level()->set_skill_pct(0.08);
  return skill;
}

TEST(OffenseStatsForTest, AttackSkillSetsSkillPctAtLevelOne) {
  Skill slash_blast = SlashBlast();
  OffenseStats offense =
      OffenseStatsFor(JOB_SWORDMAN, 15, AllocatedStats(), EquipStats(),
                      EQUIP_TYPE_UNSPECIFIED, &slash_blast, 1);
  EXPECT_DOUBLE_EQ(offense.skill_pct, 1.83);  // base, no per-level yet
}

TEST(OffenseStatsForTest, AttackSkillAddsPerLevelBeyondLevelOne) {
  Skill slash_blast = SlashBlast();
  OffenseStats offense =
      OffenseStatsFor(JOB_SWORDMAN, 15, AllocatedStats(), EquipStats(),
                      EQUIP_TYPE_UNSPECIFIED, &slash_blast, 10);
  EXPECT_DOUBLE_EQ(offense.skill_pct, 1.83 + 0.08 * 9);  // 2.55 at level 10
}

TEST(OffenseStatsForTest, MultiHitSkillSetsLines) {
  Skill leap_attack;
  leap_attack.set_name("Leap Attack");
  leap_attack.set_kind(SKILL_KIND_ATTACK);
  leap_attack.set_max_level(1);
  leap_attack.set_lines(2);
  leap_attack.mutable_base()->set_skill_pct(0.90);
  OffenseStats offense =
      OffenseStatsFor(JOB_SWORDMAN, 15, AllocatedStats(), EquipStats(),
                      EQUIP_TYPE_UNSPECIFIED, &leap_attack, 1);
  EXPECT_DOUBLE_EQ(offense.skill_pct, 0.90);
  EXPECT_EQ(offense.lines, 2);  // 90% twice = 180% a target
}

TEST(OffenseStatsForTest, SingleHitSkillKeepsOneLine) {
  Skill slash_blast = SlashBlast();  // no lines set
  OffenseStats offense =
      OffenseStatsFor(JOB_SWORDMAN, 15, AllocatedStats(), EquipStats(),
                      EQUIP_TYPE_UNSPECIFIED, &slash_blast, 1);
  EXPECT_EQ(offense.lines, 1);
}

TEST(OffenseStatsForTest, NoAttackSkillKeepsTheBarePoke) {
  OffenseStats offense =
      OffenseStatsFor(JOB_SWORDMAN, 15, AllocatedStats(), EquipStats(),
                      EQUIP_TYPE_UNSPECIFIED, nullptr, 0);
  EXPECT_DOUBLE_EQ(offense.skill_pct, 1.0);
}

TEST(OffenseStatsForTest, PassiveSkillDoesNotChangeSkillPct) {
  Skill passive;
  passive.set_name("Iron Body");
  passive.set_kind(SKILL_KIND_PASSIVE);
  passive.set_max_level(10);
  passive.mutable_base()->set_max_hp_pct(0.10);
  OffenseStats offense =
      OffenseStatsFor(JOB_SWORDMAN, 15, AllocatedStats(), EquipStats(),
                      EQUIP_TYPE_UNSPECIFIED, &passive, 5);
  EXPECT_DOUBLE_EQ(offense.skill_pct,
                   1.0);  // passives fold into HP, not damage
}

TEST(OffenseStatsForTest, SlashBlastKills83PercentFaster) {
  // A learned attack skill scales expected damage by exactly its skill_pct
  // versus the bare poke -- 1.83x here. At equal swing speed (Slash Blast is
  // assumed to swing as fast as the poke for now) that is 1.83x the kills per
  // unit time, i.e. "83% faster".
  AllocatedStats allocated;
  allocated.set_str(40);
  EquipStats equipped;
  equipped.set_attack(15);
  Mob mob = MakeMob(/*pdr=*/0, /*boss=*/false, /*level=*/15);

  OffenseStats poke = OffenseStatsFor(JOB_SWORDMAN, 15, allocated, equipped,
                                      EQUIP_TYPE_UNSPECIFIED, nullptr, 0);
  Skill slash_blast = SlashBlast();
  OffenseStats slash = OffenseStatsFor(JOB_SWORDMAN, 15, allocated, equipped,
                                       EQUIP_TYPE_UNSPECIFIED, &slash_blast, 1);
  EXPECT_DOUBLE_EQ(ExpectedAttackDamage(slash, mob),
                   1.83 * ExpectedAttackDamage(poke, mob));
}

// A mob that only swings: attack and level, which is all ExpectedDamageTaken
// reads.
Mob Attacker(int attack, int level) {
  Mob mob;
  mob.set_attack(attack);
  mob.set_level(level);
  return mob;
}

class DamageTakenTest : public ::testing::Test {
 protected:
  // A character of the same level as Attacker() below, with no DEF at all.
  // Both rolls land in full: (85 + 100) / 2 * 0.85 == 78.625.
  DefenseStats Naked() {
    DefenseStats defense;
    defense.level = 10;
    return defense;
  }
};

TEST_F(DamageTakenTest, WithoutDefenseTheWholeAttackLands) {
  EXPECT_DOUBLE_EQ(ExpectedDamageTaken(Naked(), Attacker(100, 10)), 78.625);
}

TEST_F(DamageTakenTest, DefenseSubtractsFromBothRolls) {
  DefenseStats defense = Naked();
  defense.def = 40;
  // Under both caps (68 and 80), so all 40 counts: (45 + 60) / 2 * 0.85.
  EXPECT_DOUBLE_EQ(ExpectedDamageTaken(defense, Attacker(100, 10)), 44.625);
}

TEST_F(DamageTakenTest, DefenseStopsCountingAtTheCap) {
  DefenseStats defense = Naked();
  defense.def = 80;
  // The caps bind on both rolls: (85 - 68 + 100 - 80) / 2 * 0.85.
  EXPECT_DOUBLE_EQ(ExpectedDamageTaken(defense, Attacker(100, 10)), 15.725);
}

TEST_F(DamageTakenTest, ArmourPastTheCapIsWorthNothing) {
  DefenseStats capped = Naked();
  capped.def = 80;
  DefenseStats absurd = Naked();
  absurd.def = 10000;
  EXPECT_DOUBLE_EQ(ExpectedDamageTaken(absurd, Attacker(100, 10)),
                   ExpectedDamageTaken(capped, Attacker(100, 10)));
}

TEST_F(DamageTakenTest, UnderLevellingShrinksDefense) {
  DefenseStats defense = Naked();
  defense.def = 50;
  // Ten levels under, so only 90% of the DEF counts: 45 rather than 50, and
  // both caps stay clear. (40 + 55) / 2 * 0.85.
  EXPECT_DOUBLE_EQ(ExpectedDamageTaken(defense, Attacker(100, 20)), 40.375);
}

TEST_F(DamageTakenTest, DefenseEffectivenessFloorsAtHalf) {
  DefenseStats thirty = Naked();
  thirty.def = 50;
  DefenseStats fifty = thirty;
  // Thirty levels under is the floor; twenty further down changes nothing
  // about the DEF, so the two differ only by the level multiplier.
  double at_floor = ExpectedDamageTaken(thirty, Attacker(200, 40));
  double past_floor = ExpectedDamageTaken(fifty, Attacker(200, 60));
  // 0.5 * 50 == 25 cancelled on both rolls: (145 + 175) / 2 == 160, times A.
  EXPECT_DOUBLE_EQ(at_floor, 160.0 * 0.8725);
  EXPECT_DOUBLE_EQ(past_floor, 160.0 * 0.88);
}

TEST_F(DamageTakenTest, CappedArmourWaivesThePenalty) {
  DefenseStats defense = Naked();
  defense.def = 80;
  // Thirty levels under, where only half the DEF would normally count -- but
  // 80 already clears the 80-point cap, so the character takes the minimum as
  // though the penalty did not exist. Same 15.725 as at parity, scaled by the
  // deeper level multiplier.
  EXPECT_DOUBLE_EQ(ExpectedDamageTaken(defense, Attacker(100, 40)),
                   18.5 * 0.8725);
}

TEST_F(DamageTakenTest, LevelMultiplierEasesOffAboveTheMob) {
  DefenseStats defense = Naked();
  defense.level = 20;
  // Ten or more levels above the mob is the 0.775 floor for A.
  EXPECT_DOUBLE_EQ(ExpectedDamageTaken(defense, Attacker(100, 10)),
                   92.5 * 0.775);
  DefenseStats further = defense;
  further.level = 40;
  EXPECT_DOUBLE_EQ(ExpectedDamageTaken(further, Attacker(100, 10)),
                   92.5 * 0.775);
}

TEST_F(DamageTakenTest, LevelMultiplierClimbsInBandsBelowTheMob) {
  DefenseStats defense = Naked();
  // Fifteen under is still the parity multiplier; sixteen crosses into the
  // first band.
  EXPECT_DOUBLE_EQ(ExpectedDamageTaken(defense, Attacker(100, 25)),
                   92.5 * 0.85);
  EXPECT_DOUBLE_EQ(ExpectedDamageTaken(defense, Attacker(100, 26)),
                   92.5 * 0.8575);
}

TEST_F(DamageTakenTest, ReductionAppliesAfterTheFormula) {
  DefenseStats defense = Naked();
  defense.damage_taken_pct = 0.5;
  EXPECT_DOUBLE_EQ(ExpectedDamageTaken(defense, Attacker(100, 10)),
                   78.625 / 2.0);
}

TEST_F(DamageTakenTest, DodgingTakesAShareOfTheHitsAway) {
  DefenseStats defense = Naked();
  defense.dodge_chance = 0.30;
  EXPECT_DOUBLE_EQ(ExpectedDamageTaken(defense, Attacker(100, 10)),
                   78.625 * 0.70);
}

TEST_F(DamageTakenTest, DodgingAndReductionBothLand) {
  DefenseStats defense = Naked();
  defense.dodge_chance = 0.30;
  defense.damage_taken_pct = 0.5;
  EXPECT_DOUBLE_EQ(ExpectedDamageTaken(defense, Attacker(100, 10)),
                   78.625 / 2.0 * 0.70);
}

TEST_F(DamageTakenTest, DodgingCutsPastTheOneDamageFloor) {
  DefenseStats defense = Naked();
  defense.level = 30;
  defense.def = 200;
  // The floor holds a hit at 1 point; dodging it half the time costs half a
  // point on average. Reduction can never do this -- it lands before the
  // floor, and the floor is what a hit that arrives always costs.
  EXPECT_DOUBLE_EQ(ExpectedDamageTaken(defense, Attacker(2, 1)), 1.0);
  defense.dodge_chance = 0.5;
  EXPECT_DOUBLE_EQ(ExpectedDamageTaken(defense, Attacker(2, 1)), 0.5);
}

TEST_F(DamageTakenTest, EveryHitCostsAtLeastAPoint) {
  DefenseStats defense = Naked();
  defense.level = 30;
  defense.def = 200;
  // A snail against a level-30 character: the caps put the hit well under a
  // point, and it still costs one. Armour cancels an attack, it does not make
  // the character untouchable.
  EXPECT_DOUBLE_EQ(ExpectedDamageTaken(defense, Attacker(2, 1)), 1.0);
}

TEST_F(DamageTakenTest, AHitWorthMoreThanAPointIsNotRaisedToOne) {
  DefenseStats defense = Naked();
  defense.level = 30;
  defense.def = 200;
  // Seven attack against the same character: the caps leave 0.185 * 7 * 0.775,
  // just over a point, and just over is enough.
  double landed = ExpectedDamageTaken(defense, Attacker(7, 1));
  EXPECT_GT(landed, 1.0);
  EXPECT_LT(landed, 2.0);
}

}  // namespace
}  // namespace ms
