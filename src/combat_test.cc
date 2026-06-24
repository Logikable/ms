#include "src/combat.h"

#include <gtest/gtest.h>

namespace ms {
namespace {

// A mob carrying just the combat-relevant fields: PDR (whole percent), the boss
// flag, and max HP (only the kill-rate tests need HP).
Mob MakeMob(int pdr = 0, bool boss = false, int max_hp = 0) {
  Mob mob;
  mob.set_pdr(pdr);
  mob.set_boss(boss);
  mob.set_max_hp(max_hp);
  return mob;
}

// A map carrying just its spawn-point count.
MapData MakeMap(int spawn_count) {
  MapData map;
  map.set_spawn_count(spawn_count);
  return map;
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

TEST_F(OffenseTest, DpsIsDamageOverSwingInterval) {
  // Baseline expected damage 25.875 at base 720ms / stage 4 (0.72s).
  EXPECT_DOUBLE_EQ(Dps(Baseline(), MakeMob(), 720, 4), 25.875 / 0.72);
}

// All kill-rate tests pass respawn_interval_seconds = 1.0 so the spawn cap is
// just spawn_count, keeping the arithmetic round.
TEST(KillsPerSecondTest, DpsLimitedBelowSpawnCap) {
  // dps_rate = 100*1/10 = 10 < spawn 50; 10 / kGameSpeedFactor(10) = 1.0.
  EXPECT_DOUBLE_EQ(
      KillsPerSecond(100.0, MakeMob(0, false, 10), 1, MakeMap(50), 1.0), 1.0);
}

TEST(KillsPerSecondTest, SpawnCappedAboveCrossover) {
  // dps_rate = 100000*1/10 = 10000, clamped to spawn 5; 5 / 10 = 0.5.
  EXPECT_DOUBLE_EQ(
      KillsPerSecond(100000.0, MakeMob(0, false, 10), 1, MakeMap(5), 1.0), 0.5);
}

TEST(KillsPerSecondTest, MaxTargetsScalesDpsRate) {
  // dps_rate = 100*3/10 = 30 < spawn 100; 30 / 10 = 3.0.
  EXPECT_DOUBLE_EQ(
      KillsPerSecond(100.0, MakeMob(0, false, 10), 3, MakeMap(100), 1.0), 3.0);
}

TEST(KillsPerSecondTest, MoreDpsDoesNothingOnceSpawnCapped) {
  // Both well past the spawn cap of 10 -> identical 10/10 = 1.0.
  EXPECT_DOUBLE_EQ(
      KillsPerSecond(1000.0, MakeMob(0, false, 10), 1, MakeMap(10), 1.0),
      KillsPerSecond(100000.0, MakeMob(0, false, 10), 1, MakeMap(10), 1.0));
}

TEST(KillsPerSecondTest, DefaultRespawnUsesTheGmsTick) {
  // spawn 756 over the default 7.56s tick = 100/s cap; dps_rate far higher.
  EXPECT_DOUBLE_EQ(KillsPerSecond(1e9, MakeMob(0, false, 10), 1, MakeMap(756)),
                   10.0);
}

TEST(ExpPerSecondTest, ScalesKillsByMobExp) {
  EXPECT_DOUBLE_EQ(ExpPerSecond(2.0, 3), 6.0);
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
