#include "src/combat.h"

#include <gtest/gtest.h>

#include <cmath>

namespace ms {
namespace {

// A mob carrying just the combat-relevant fields: PDR (whole percent), the boss
// flag, and max HP (only the kill-cycle tests need HP).
Mob MakeMob(int pdr = 0, bool boss = false, int max_hp = 0) {
  Mob mob;
  mob.set_pdr(pdr);
  mob.set_boss(boss);
  mob.set_max_hp(max_hp);
  return mob;
}

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
  EXPECT_DOUBLE_EQ(ExpectedAttackDamage(Baseline(), MakeMob()), 25.875);
}

TEST_F(OffenseTest, FullMasteryRemovesMinFloor) {
  OffenseStats s = Baseline();
  s.mastery = 1.0;  // min == max, so expected == max base.
  EXPECT_DOUBLE_EQ(ExpectedAttackDamage(s, MakeMob()), 45.0);
}

TEST_F(OffenseTest, SkillPctScalesLinearly) {
  OffenseStats s = Baseline();
  s.skill_pct = 2.0;
  EXPECT_DOUBLE_EQ(ExpectedAttackDamage(s, MakeMob()), 51.75);
}

TEST_F(OffenseTest, LinesMultiplyDamage) {
  OffenseStats s = Baseline();
  s.lines = 3;
  EXPECT_DOUBLE_EQ(ExpectedAttackDamage(s, MakeMob()), 77.625);
}

TEST_F(OffenseTest, DamagePctIsAdditive) {
  OffenseStats s = Baseline();
  s.damage_pct = 0.20;  // *1.2
  EXPECT_DOUBLE_EQ(ExpectedAttackDamage(s, MakeMob()), 31.05);
}

TEST_F(OffenseTest, BossPctAppliesOnlyToBosses) {
  OffenseStats s = Baseline();
  s.boss_pct = 0.50;
  EXPECT_DOUBLE_EQ(ExpectedAttackDamage(s, MakeMob()), 25.875);
}

TEST_F(OffenseTest, CritIncludesHiddenBaseCritDamage) {
  OffenseStats s = Baseline();
  s.crit_rate = 1.0;  // always crit
  s.crit_dmg = 0.0;   // only the hidden 0.35 base applies
  EXPECT_DOUBLE_EQ(ExpectedAttackDamage(s, MakeMob()), 25.875 * 1.35);
}

TEST_F(OffenseTest, FinalDamageMultiplies) {
  OffenseStats s = Baseline();
  s.final_dmg_pct = 0.10;
  EXPECT_DOUBLE_EQ(ExpectedAttackDamage(s, MakeMob()), 25.875 * 1.10);
}

TEST_F(OffenseTest, MobDefenseReducesDamage) {
  EXPECT_DOUBLE_EQ(ExpectedAttackDamage(Baseline(), MakeMob(30)),
                   25.875 * 0.70);
}

TEST_F(OffenseTest, IedNegatesMobDefense) {
  OffenseStats s = Baseline();
  s.ied = 1.0;  // fully ignore the 30% PDR
  EXPECT_DOUBLE_EQ(ExpectedAttackDamage(s, MakeMob(30)), 25.875);
}

TEST_F(OffenseTest, BossesTakeHalfElementalByDefault) {
  EXPECT_DOUBLE_EQ(ExpectedAttackDamage(Baseline(), MakeMob(0, true)),
                   25.875 * 0.5);
}

TEST_F(OffenseTest, IerRestoresBossElemental) {
  OffenseStats s = Baseline();
  s.ier = 1.0;  // 0.5*(1+1) == 1.0
  EXPECT_DOUBLE_EQ(ExpectedAttackDamage(s, MakeMob(0, true)), 25.875);
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

TEST(BaseAttackDelayMsTest, OneHandedSwordIs800) {
  EXPECT_EQ(BaseAttackDelayMs(EQUIP_TYPE_ONE_HANDED_SWORD), 800);
}

TEST(BaseAttackDelayMsTest, UnknownTypeFallsBackToOneHanded) {
  EXPECT_EQ(BaseAttackDelayMs(EQUIP_TYPE_UNSPECIFIED), 800);
}

// Kill-cycle tests override respawn_interval_seconds = 1.0 so the cycle is just
// (tick count) * kGameSpeedFactor(10), keeping the arithmetic round. mob HP is
// 10 throughout; damage_per_hit sets the hit count.
TEST(KillCycleTest, BelowTickRoundsUpToOneTick) {
  // 10 dmg vs 10 HP = 1 hit; kill_time 0.5 < 1 tick -> 1 tick * 10 = 10.
  EXPECT_DOUBLE_EQ(KillCycleSeconds(10.0, 0.5, MakeMob(0, false, 10), 1.0),
                   10.0);
}

TEST(KillCycleTest, CliffAtRespawnTick) {
  // One fewer point of damage adds a hit that spills past the tick boundary,
  // jumping the cycle by a whole tick: 10 dmg = 1 hit (0.6 < 1 tick -> 10),
  // 6 dmg = ceil(10/6) = 2 hits (1.2 > 1 tick -> 2 ticks -> 20). Exactly
  // double.
  Mob mob = MakeMob(0, false, 10);
  EXPECT_DOUBLE_EQ(KillCycleSeconds(10.0, 0.6, mob, 1.0), 10.0);
  EXPECT_DOUBLE_EQ(KillCycleSeconds(6.0, 0.6, mob, 1.0), 20.0);
}

TEST(KillCycleTest, MultiTickKill) {
  // 1 hit but kill_time 2.1 spans into the third tick -> 3 ticks * 10 = 30.
  EXPECT_DOUBLE_EQ(KillCycleSeconds(10.0, 2.1, MakeMob(0, false, 10), 1.0),
                   30.0);
}

TEST(KillCycleTest, ZeroDamageNeverKills) {
  EXPECT_TRUE(
      std::isinf(KillCycleSeconds(0.0, 0.5, MakeMob(0, false, 10), 1.0)));
}

TEST(KillCycleTest, DefaultRespawnUsesTheGmsTick) {
  // 1 hit, kill_time 1 < one 7.56s tick -> 1 tick * 10 = 75.6.
  EXPECT_DOUBLE_EQ(KillCycleSeconds(10.0, 1.0, MakeMob(0, false, 10)),
                   kRespawnIntervalSeconds * 10.0);
}

TEST(OffenseStatsForTest, SumsAllocatedAndEquippedStats) {
  AllocatedStats allocated;
  allocated.set_str(13);
  allocated.set_dex(4);
  EquipStats equipped;
  equipped.set_str(10);  // e.g. a stat-bearing piece
  equipped.set_dex(2);
  equipped.set_attack(15);  // the Sword

  OffenseStats offense = OffenseStatsFor(JOB_BEGINNER, allocated, equipped);
  EXPECT_EQ(offense.primary, 23);   // 13 + 10
  EXPECT_EQ(offense.secondary, 6);  // 4 + 2
  EXPECT_EQ(offense.attack, 15);
}

TEST(OffenseStatsForTest, WarriorUsesStrPrimaryDexSecondary) {
  AllocatedStats allocated;
  allocated.set_str(100);
  allocated.set_dex(20);
  OffenseStats offense = OffenseStatsFor(JOB_WARRIOR, allocated, EquipStats());
  EXPECT_EQ(offense.primary, 100);
  EXPECT_EQ(offense.secondary, 20);
}

TEST(OffenseStatsForTest, GearGraduatesBossPctAndIed) {
  EquipStats equipped;
  equipped.set_boss_damage(30);           // 30%
  equipped.set_ignore_enemy_defense(20);  // 20%
  OffenseStats offense =
      OffenseStatsFor(JOB_WARRIOR, AllocatedStats(), equipped);
  EXPECT_DOUBLE_EQ(offense.boss_pct, 0.30);
  EXPECT_DOUBLE_EQ(offense.ied, 0.20);
}

TEST(OffenseStatsForTest, DefaultsAreUntouchedWithoutGear) {
  OffenseStats offense =
      OffenseStatsFor(JOB_BEGINNER, AllocatedStats(), EquipStats());
  EXPECT_DOUBLE_EQ(offense.mastery, 0.15);
  EXPECT_DOUBLE_EQ(offense.skill_pct, 1.0);
  EXPECT_DOUBLE_EQ(offense.boss_pct, 0.0);
  EXPECT_DOUBLE_EQ(offense.ied, 0.0);
}

TEST(OffenseStatsForTest, UnknownJobYieldsZeroMainStats) {
  AllocatedStats allocated;
  allocated.set_str(50);
  allocated.set_dex(30);
  OffenseStats offense =
      OffenseStatsFor(JOB_UNSPECIFIED, allocated, EquipStats());
  EXPECT_EQ(offense.primary, 0);  // fail safe: unknown job has no main stat
  EXPECT_EQ(offense.secondary, 0);
}

}  // namespace
}  // namespace ms
