#include "src/combat/damage.h"

#include <gtest/gtest.h>

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

class OffenseTest : public ::testing::Test {
 protected:
  // Only primary/secondary/attack set; every modifier at identity (mastery at
  // its 0.15 default). StatValue = 4*10+5 = 45; MaxBase = 45*100/100 = 45;
  // expected = 45*(1+0.15)/2 = 25.875.
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
                   25.875 * kEqualLevel);
}

TEST_F(OffenseTest, FullMasteryRemovesMinFloor) {
  OffenseStats s = Baseline();
  s.mastery = 1.0;  // min == max, so expected == max base.
  EXPECT_DOUBLE_EQ(ExpectedAttackDamage(s, MakeMob()), 45.0 * kEqualLevel);
}

TEST_F(OffenseTest, SkillPctScalesLinearly) {
  OffenseStats s = Baseline();
  s.skill_pct = 2.0;
  EXPECT_DOUBLE_EQ(ExpectedAttackDamage(s, MakeMob()), 51.75 * kEqualLevel);
}

TEST_F(OffenseTest, LinesMultiplyDamage) {
  OffenseStats s = Baseline();
  s.lines = 3;
  EXPECT_DOUBLE_EQ(ExpectedAttackDamage(s, MakeMob()), 77.625 * kEqualLevel);
}

TEST_F(OffenseTest, DamagePctIsAdditive) {
  OffenseStats s = Baseline();
  s.damage_pct = 0.20;  // *1.2
  EXPECT_DOUBLE_EQ(ExpectedAttackDamage(s, MakeMob()), 31.05 * kEqualLevel);
}

TEST_F(OffenseTest, BossPctAppliesOnlyToBosses) {
  OffenseStats s = Baseline();
  s.boss_pct = 0.50;
  EXPECT_DOUBLE_EQ(ExpectedAttackDamage(s, MakeMob()), 25.875 * kEqualLevel);
}

TEST_F(OffenseTest, CritIncludesHiddenBaseCritDamage) {
  OffenseStats s = Baseline();
  s.crit_rate = 1.0;  // always crit
  s.crit_dmg = 0.0;   // only the hidden 0.35 base applies
  EXPECT_DOUBLE_EQ(ExpectedAttackDamage(s, MakeMob()),
                   25.875 * 1.35 * kEqualLevel);
}

TEST_F(OffenseTest, FinalDamageMultiplies) {
  OffenseStats s = Baseline();
  s.final_dmg_pct = 0.10;
  EXPECT_DOUBLE_EQ(ExpectedAttackDamage(s, MakeMob()),
                   25.875 * 1.10 * kEqualLevel);
}

TEST_F(OffenseTest, MobDefenseReducesDamage) {
  EXPECT_DOUBLE_EQ(ExpectedAttackDamage(Baseline(), MakeMob(30)),
                   25.875 * 0.70 * kEqualLevel);
}

TEST_F(OffenseTest, IedNegatesMobDefense) {
  OffenseStats s = Baseline();
  s.ied = 1.0;  // fully ignore the 30% PDR
  EXPECT_DOUBLE_EQ(ExpectedAttackDamage(s, MakeMob(30)), 25.875 * kEqualLevel);
}

TEST_F(OffenseTest, BossesTakeHalfElementalByDefault) {
  EXPECT_DOUBLE_EQ(ExpectedAttackDamage(Baseline(), MakeMob(0, true)),
                   25.875 * 0.5 * kEqualLevel);
}

TEST_F(OffenseTest, IerRestoresBossElemental) {
  OffenseStats s = Baseline();
  s.ier = 1.0;  // 0.5*(1+1) == 1.0
  EXPECT_DOUBLE_EQ(ExpectedAttackDamage(s, MakeMob(0, true)),
                   25.875 * kEqualLevel);
}

TEST_F(OffenseTest, LevelMultiplierAppliesToOutput) {
  // Attacker at level 0 against a level-5 mob: 5 levels under -> 0.88 penalty.
  EXPECT_DOUBLE_EQ(ExpectedAttackDamage(Baseline(), MakeMob(0, false, 5)),
                   25.875 * 0.88);
}

TEST_F(OffenseTest, FortyLevelsUnderFloorsToOneDamage) {
  // The level multiplier hits 0 at a 40-level gap; output floors to 1 damage.
  OffenseStats s = Baseline();  // level 0
  EXPECT_DOUBLE_EQ(ExpectedAttackDamage(s, MakeMob(0, false, 40)), 1.0);
}

TEST_F(OffenseTest, CombatPowerIsTheDamageChainWithoutATarget) {
  // The same 25.875 the baseline swing produces, floored -- no mob, so no
  // level multiplier and no defense.
  EXPECT_EQ(CombatPower(Baseline()), 25);
}

TEST_F(OffenseTest, CombatPowerCountsBossDamageAgainstEveryone) {
  OffenseStats s = Baseline();
  s.boss_pct = 0.6;
  // A swing at an ordinary mob ignores this entirely; combat power does not.
  EXPECT_DOUBLE_EQ(ExpectedAttackDamage(s, MakeMob()), 25.875 * kEqualLevel);
  EXPECT_EQ(CombatPower(s), 41);  // 25.875 * 1.6
}

TEST_F(OffenseTest, CombatPowerWeightsCritDamageByItsRate) {
  OffenseStats s = Baseline();
  s.crit_rate = 0.5;
  s.crit_dmg = 0.25;
  // 25.875 * (1 + 0.5 * (0.25 + 0.35)) = 33.6375. GMS's flat 1.35 + 0.25 would
  // read 41 here, pricing the crit damage as though every swing crit.
  EXPECT_EQ(CombatPower(s), 33);
}

TEST_F(OffenseTest, CombatPowerRisesWithMastery) {
  OffenseStats s = Baseline();
  s.mastery = 1.0;  // no min-damage floor at all
  EXPECT_EQ(CombatPower(s), 45);
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

TEST(LevelMultiplierTest, EqualLevelGivesTenPercentBonus) {
  EXPECT_DOUBLE_EQ(LevelMultiplier(10, 10), 1.1);
}

TEST(LevelMultiplierTest, BonusRisesToTwentyPercentAndCapsAtFiveAbove) {
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

TEST(BaseAttackDelayMsTest, OneHandedSwordIs800) {
  EXPECT_EQ(BaseAttackDelayMs(EQUIP_TYPE_ONE_HANDED_SWORD), 800);
}

TEST(BaseAttackDelayMsTest, BowShootsOnTheSameReferenceAsASword) {
  // Nothing published separates the two animations; the attack speed stage on
  // the item is what makes a bow feel different.
  EXPECT_EQ(BaseAttackDelayMs(EQUIP_TYPE_BOW), 800);
}

TEST(BaseAttackDelayMsTest, WandCastsOnTheSameReference) {
  EXPECT_EQ(BaseAttackDelayMs(EQUIP_TYPE_WAND), 800);
}

TEST(BaseAttackDelayMsTest, UnknownTypeFallsBackToOneHanded) {
  EXPECT_EQ(BaseAttackDelayMs(EQUIP_TYPE_UNSPECIFIED), 800);
}

TEST(OffenseStatsForTest, SumsAllocatedAndEquippedStats) {
  AllocatedStats allocated;
  allocated.set_str(13);
  allocated.set_dex(4);
  EquipStats equipped;
  equipped.set_str(10);  // e.g. a stat-bearing piece
  equipped.set_dex(2);
  equipped.set_attack(15);  // the Sword

  OffenseStats offense =
      OffenseStatsFor(JOB_BEGINNER, 7, allocated, equipped, nullptr, 0);
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
      OffenseStatsFor(JOB_SWORDMAN, 1, allocated, EquipStats(), nullptr, 0);
  EXPECT_EQ(offense.primary, 100);
  EXPECT_EQ(offense.secondary, 20);
}

TEST(OffenseStatsForTest, GearGraduatesBossPctAndIed) {
  EquipStats equipped;
  equipped.set_boss_damage(30);           // 30%
  equipped.set_ignore_enemy_defense(20);  // 20%
  OffenseStats offense =
      OffenseStatsFor(JOB_SWORDMAN, 1, AllocatedStats(), equipped, nullptr, 0);
  EXPECT_DOUBLE_EQ(offense.boss_pct, 0.30);
  EXPECT_DOUBLE_EQ(offense.ied, 0.20);
}

TEST(OffenseStatsForTest, DefaultsAreUntouchedWithoutGear) {
  OffenseStats offense = OffenseStatsFor(JOB_BEGINNER, 1, AllocatedStats(),
                                         EquipStats(), nullptr, 0);
  EXPECT_DOUBLE_EQ(offense.mastery, 0.15);
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
  OffenseStats offense =
      OffenseStatsFor(JOB_ARCHER, 1, allocated, equipped, nullptr, 0);
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
  OffenseStats offense =
      OffenseStatsFor(JOB_MAGICIAN, 1, allocated, equipped, nullptr, 0);
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
  OffenseStats offense =
      OffenseStatsFor(JOB_ROGUE, 1, allocated, equipped, nullptr, 0);
  EXPECT_EQ(offense.primary, 55);
  EXPECT_EQ(offense.secondary, 33);  // and DEX backs it up
}

TEST(OffenseStatsForTest, NonMagiciansIgnoreMagicAttack) {
  EquipStats equipped;
  equipped.set_attack(40);
  equipped.set_magic_attack(999);
  OffenseStats offense =
      OffenseStatsFor(JOB_ARCHER, 1, AllocatedStats(), equipped, nullptr, 0);
  EXPECT_EQ(offense.attack, 40);
}

TEST(OffenseStatsForTest, PassiveCritRateReachesTheOffense) {
  PassiveOffense passives;
  passives.crit_rate = 0.40;
  OffenseStats offense = OffenseStatsFor(JOB_ARCHER, 15, AllocatedStats(),
                                         EquipStats(), nullptr, 0, passives);
  EXPECT_DOUBLE_EQ(offense.crit_rate, 0.40);
}

TEST(OffenseStatsForTest, PassiveMasteryReplacesTheBaseline) {
  PassiveOffense passives;
  passives.mastery = 0.50;
  OffenseStats offense = OffenseStatsFor(JOB_SWORDMAN, 30, AllocatedStats(),
                                         EquipStats(), nullptr, 0, passives);
  EXPECT_DOUBLE_EQ(offense.mastery, 0.50);
}

// The first level of a mastery skill is worth less than what every character
// swings at already, and learning a skill must never make a swing worse.
TEST(OffenseStatsForTest, MasteryBelowTheBaselineIsIgnored) {
  OffenseStats bare = OffenseStatsFor(JOB_SWORDMAN, 30, AllocatedStats(),
                                      EquipStats(), nullptr, 0);
  PassiveOffense passives;
  passives.mastery = 0.14;
  OffenseStats learned = OffenseStatsFor(JOB_SWORDMAN, 30, AllocatedStats(),
                                         EquipStats(), nullptr, 0, passives);
  EXPECT_DOUBLE_EQ(learned.mastery, bare.mastery);
}

TEST(OffenseStatsForTest, UnknownJobYieldsZeroMainStats) {
  AllocatedStats allocated;
  allocated.set_str(50);
  allocated.set_dex(30);
  OffenseStats offense =
      OffenseStatsFor(JOB_UNSPECIFIED, 1, allocated, EquipStats(), nullptr, 0);
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
  OffenseStats offense = OffenseStatsFor(JOB_SWORDMAN, 15, AllocatedStats(),
                                         EquipStats(), &slash_blast, 1);
  EXPECT_DOUBLE_EQ(offense.skill_pct, 1.83);  // base, no per-level yet
}

TEST(OffenseStatsForTest, AttackSkillAddsPerLevelBeyondLevelOne) {
  Skill slash_blast = SlashBlast();
  OffenseStats offense = OffenseStatsFor(JOB_SWORDMAN, 15, AllocatedStats(),
                                         EquipStats(), &slash_blast, 10);
  EXPECT_DOUBLE_EQ(offense.skill_pct, 1.83 + 0.08 * 9);  // 2.55 at level 10
}

TEST(OffenseStatsForTest, MultiHitSkillSetsLines) {
  Skill leap_attack;
  leap_attack.set_name("Leap Attack");
  leap_attack.set_kind(SKILL_KIND_ATTACK);
  leap_attack.set_max_level(1);
  leap_attack.set_lines(2);
  leap_attack.mutable_base()->set_skill_pct(0.90);
  OffenseStats offense = OffenseStatsFor(JOB_SWORDMAN, 15, AllocatedStats(),
                                         EquipStats(), &leap_attack, 1);
  EXPECT_DOUBLE_EQ(offense.skill_pct, 0.90);
  EXPECT_EQ(offense.lines, 2);  // 90% twice = 180% a target
}

TEST(OffenseStatsForTest, SingleHitSkillKeepsOneLine) {
  Skill slash_blast = SlashBlast();  // no lines set
  OffenseStats offense = OffenseStatsFor(JOB_SWORDMAN, 15, AllocatedStats(),
                                         EquipStats(), &slash_blast, 1);
  EXPECT_EQ(offense.lines, 1);
}

TEST(OffenseStatsForTest, NoAttackSkillKeepsTheBarePoke) {
  OffenseStats offense = OffenseStatsFor(JOB_SWORDMAN, 15, AllocatedStats(),
                                         EquipStats(), nullptr, 0);
  EXPECT_DOUBLE_EQ(offense.skill_pct, 1.0);
}

TEST(OffenseStatsForTest, PassiveSkillDoesNotChangeSkillPct) {
  Skill passive;
  passive.set_name("Iron Body");
  passive.set_kind(SKILL_KIND_PASSIVE);
  passive.set_max_level(10);
  passive.mutable_base()->set_max_hp_pct(0.10);
  OffenseStats offense = OffenseStatsFor(JOB_SWORDMAN, 15, AllocatedStats(),
                                         EquipStats(), &passive, 5);
  EXPECT_DOUBLE_EQ(offense.skill_pct,
                   1.0);  // passives fold into HP, not damage
}

TEST(OffenseStatsForTest, LevelOneSlashBlastKillsRoughly83PercentFaster) {
  // A learned attack skill scales expected damage by exactly its skill_pct
  // versus the bare poke -- 1.83x here. At equal swing speed (Slash Blast is
  // assumed to swing as fast as the poke for now) that is 1.83x the kills per
  // unit time, i.e. "83% faster".
  AllocatedStats allocated;
  allocated.set_str(40);
  EquipStats equipped;
  equipped.set_attack(15);
  Mob mob = MakeMob(/*pdr=*/0, /*boss=*/false, /*level=*/15);

  OffenseStats poke =
      OffenseStatsFor(JOB_SWORDMAN, 15, allocated, equipped, nullptr, 0);
  Skill slash_blast = SlashBlast();
  OffenseStats slash =
      OffenseStatsFor(JOB_SWORDMAN, 15, allocated, equipped, &slash_blast, 1);
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

TEST_F(DamageTakenTest, CappedArmourWaivesTheUnderLevellingPenalty) {
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
