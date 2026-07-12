#include "src/combat/idle_rate.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <vector>

#include "src/combat/constants.h"
#include "src/combat/damage.h"
#include "src/protos/equip.pb.h"
#include "src/protos/mob.pb.h"

namespace ms {
namespace {

// A mob carrying just the fields the kill-rate model reads: max HP, plus PDR
// and the boss flag, which reach it through ExpectedAttackDamage.
Mob MakeMob(int pdr = 0, bool boss = false, int max_hp = 0) {
  Mob mob;
  mob.set_pdr(pdr);
  mob.set_boss(boss);
  mob.set_max_hp(max_hp);
  return mob;
}

// A weapon carrying just the fields MapKillPeriods reads: type (swing
// animation) and attack speed (the stage).
EquipPrototype MakeWeapon(EquipType type, AttackSpeed speed) {
  EquipPrototype weapon;
  weapon.set_equip_type(type);
  weapon.set_attack_speed(speed);
  return weapon;
}

// A plain damage-dealing offense for the map-farming tests: 23 expected damage
// per hit against a no-defense mob (StatValue 40, MaxBase 40, * (1.15)/2).
OffenseStats FarmOffense() {
  OffenseStats offense;
  offense.primary = 10;
  offense.attack = 100;
  return offense;
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

TEST(KillCycleTest, OverkillOnTheKillingHitIsWasted) {
  // 10 dmg/hit, 1s swing, 1s respawn -> cycle = hits_to_kill * 10. Each mob
  // needs a whole hit from full HP; overkill on the last hit never carries
  // forward to spare the next mob a hit. HP 11..20 all take 2 hits (the
  // second overkills by up to 9) and share one cycle; HP 21 needs a third.
  EXPECT_DOUBLE_EQ(KillCycleSeconds(10.0, 1.0, MakeMob(0, false, 10), 1.0),
                   10.0);  // exactly 1 hit, no overkill
  EXPECT_DOUBLE_EQ(KillCycleSeconds(10.0, 1.0, MakeMob(0, false, 11), 1.0),
                   20.0);  // 1 HP over -> a full 2nd hit
  EXPECT_DOUBLE_EQ(KillCycleSeconds(10.0, 1.0, MakeMob(0, false, 20), 1.0),
                   20.0);  // 2nd hit overkills by 0; still 2 hits
  EXPECT_DOUBLE_EQ(KillCycleSeconds(10.0, 1.0, MakeMob(0, false, 21), 1.0),
                   30.0);  // spills into a 3rd hit
}

// Map-farming tests override respawn = 1.0 and farm the one-handed sword at
// AVERAGE (swing interval 0.81s).
TEST(MapKillPeriodsTest, SplitsSpawnCountEvenly) {
  // Two identical mobs share the slots evenly, so periods match; doubling
  // spawn_count doubles each type's slot share and halves its period.
  Mob a = MakeMob(0, false, 100);
  Mob b = MakeMob(0, false, 100);
  std::vector<const Mob*> mobs = {&a, &b};
  EquipPrototype weapon =
      MakeWeapon(EQUIP_TYPE_ONE_HANDED_SWORD, ATTACK_SPEED_AVERAGE);

  std::vector<double> two = MapKillPeriods(FarmOffense(), weapon, mobs, 2, 1.0);
  std::vector<double> four =
      MapKillPeriods(FarmOffense(), weapon, mobs, 4, 1.0);
  EXPECT_DOUBLE_EQ(two[0], two[1]);
  EXPECT_DOUBLE_EQ(four[0], two[0] / 2.0);
}

TEST(MapKillPeriodsTest, TankierMobHasLongerPeriodInOrder) {
  // Same slot share, but the high-HP mob needs more hits -> longer period. The
  // result stays index-aligned with the input mobs.
  Mob weak = MakeMob(0, false, 10);
  Mob tanky = MakeMob(0, false, 1000);
  std::vector<const Mob*> mobs = {&weak, &tanky};
  std::vector<double> periods = MapKillPeriods(
      FarmOffense(),
      MakeWeapon(EQUIP_TYPE_ONE_HANDED_SWORD, ATTACK_SPEED_AVERAGE), mobs, 2,
      1.0);
  EXPECT_LT(periods[0], periods[1]);
}

TEST(MapKillPeriodsTest, UnkillableMobHasInfinitePeriod) {
  // A zero-stat offense does no damage, so the mob is never killed.
  Mob mob = MakeMob(0, false, 10);
  std::vector<const Mob*> mobs = {&mob};
  std::vector<double> periods = MapKillPeriods(
      OffenseStats(),
      MakeWeapon(EQUIP_TYPE_ONE_HANDED_SWORD, ATTACK_SPEED_AVERAGE), mobs, 4,
      1.0);
  EXPECT_TRUE(std::isinf(periods[0]));
}

TEST(MapKillPeriodsTest, NoSpawnSlotsNeverFarmed) {
  Mob mob = MakeMob(0, false, 10);
  std::vector<const Mob*> mobs = {&mob};
  std::vector<double> periods = MapKillPeriods(
      FarmOffense(),
      MakeWeapon(EQUIP_TYPE_ONE_HANDED_SWORD, ATTACK_SPEED_AVERAGE), mobs, 0,
      1.0);
  EXPECT_TRUE(std::isinf(periods[0]));
}

TEST(MapKillPeriodsTest, EmptyMapHasNoPeriods) {
  std::vector<const Mob*> mobs;
  EXPECT_TRUE(MapKillPeriods(
                  FarmOffense(),
                  MakeWeapon(EQUIP_TYPE_ONE_HANDED_SWORD, ATTACK_SPEED_AVERAGE),
                  mobs, 4, 1.0)
                  .empty());
}

TEST(FlushKillsTest, AccumulatesAcrossCallsUntilWhole) {
  double acc = 0.0;
  EXPECT_EQ(FlushKills(10.0, 5.0, &acc), 0);  // 0.5 banked, no kill yet
  EXPECT_EQ(FlushKills(10.0, 5.0, &acc), 1);  // 0.5 + 0.5 -> 1 kill
  EXPECT_DOUBLE_EQ(acc, 0.0);
}

TEST(FlushKillsTest, FlushesMultipleKillsAndCarriesRemainder) {
  double acc = 0.0;
  EXPECT_EQ(FlushKills(2.0, 7.0, &acc), 3);  // 3.5 -> 3 kills
  EXPECT_DOUBLE_EQ(acc, 0.5);
}

TEST(FlushKillsTest, RemainderCarriesIntoNextAdvance) {
  double acc = 0.0;
  EXPECT_EQ(FlushKills(4.0, 10.0, &acc), 2);  // 2.5 -> 2, 0.5 left
  EXPECT_EQ(FlushKills(4.0, 10.0, &acc), 3);  // 0.5 + 2.5 = 3.0 -> 3
  EXPECT_DOUBLE_EQ(acc, 0.0);
}

TEST(FlushKillsTest, InfinitePeriodYieldsNoKills) {
  double acc = 0.25;
  EXPECT_EQ(FlushKills(std::numeric_limits<double>::infinity(), 1000.0, &acc),
            0);
  EXPECT_DOUBLE_EQ(acc, 0.25);  // accumulator untouched
}

}  // namespace
}  // namespace ms
