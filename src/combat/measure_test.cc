#include "src/combat/measure.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "src/combat/encounter.h"
#include "src/protos/mob.pb.h"

namespace ms {
namespace {

Mob MakeMob(const std::string& name, int64_t max_hp) {
  Mob mob;
  mob.set_name(name);
  mob.set_max_hp(max_hp);
  return mob;
}

// One attack dealing `damage` to one monster every `swing` seconds, hitting up
// to `reach` monsters at once.
CombatParams MakeParams(const Mob* mob, double swing, double damage,
                        int reach = 1) {
  CombatParams params;
  params.active = true;
  params.encounter = "field";
  params.respawn_seconds = 1e9;
  CombatType type;
  type.mob = mob;
  type.simultaneous = 1;
  params.types.push_back(type);
  AttackOption attack;
  attack.max_enemies = reach;
  attack.swing_seconds = swing;
  attack.damage_per_hit = {damage};
  params.attacks.push_back(std::move(attack));
  return params;
}

TEST(MeasureFightTest, TheRateIsTheDamageOverTheHorizon) {
  Mob mob = MakeMob("Snail", 10);
  CombatParams params = MakeParams(&mob, 2.0, 50.0);

  Sequence played = MeasureFight(params, 100.0);
  EXPECT_NEAR(played.seconds, 100.0, 1e-6);
  EXPECT_NEAR(played.damage, 50 * 50.0, 1e-6);
  EXPECT_EQ(played.main_attack, 0);
  ASSERT_EQ(played.by_attack.size(), 1u);
  EXPECT_NEAR(played.by_attack[0].damage, played.damage, 1e-6);
}

// The crowd size is whatever the caller asks for, not what the map holds.
TEST(MeasureFightTest, TheCrowdIsTheOneAskedFor) {
  Mob mob = MakeMob("Snail", 10);
  CombatParams params = MakeParams(&mob, 1.0, 10.0, /*reach=*/6);

  EXPECT_NEAR(MeasureFight(params, 10.0, 1).damage, 10 * 10.0, 1e-6);
  EXPECT_NEAR(MeasureFight(params, 10.0, 4).damage, 10 * 40.0, 1e-6);
  // The attack hits six, so a bigger crowd adds nothing.
  EXPECT_NEAR(MeasureFight(params, 10.0, 12).damage, 10 * 60.0, 1e-6);
}

// Two runs of the same build give the same result exactly. A sim comparing
// scrolls needs to see the scroll's effect, not random noise.
TEST(MeasureFightTest, TwoRunsAgreeExactly) {
  Mob mob = MakeMob("Snail", 10);
  CombatParams params = MakeParams(&mob, 1.0, 25.0);
  HitGroup group;
  group.damage = {25.0};
  group.rolls.lines = 4;
  group.rolls.mastery = 0.4;
  group.rolls.crit_rate = 0.5;
  group.rolls.crit_dmg = 1.0;
  params.attacks[0].groups.push_back(group);

  EXPECT_EQ(MeasureFight(params, 100.0).damage,
            MeasureFight(params, 100.0).damage);
}

// A summon's damage is added to the attacks' damage, not substituted for it,
// and reported separately so the caller can see where the damage came from.
TEST(MeasureFightTest, AnOwnClockCastIsCountedApart) {
  Mob mob = MakeMob("Snail", 10);
  CombatParams params = MakeParams(&mob, 1.0, 10.0);
  AttackOption summon;
  summon.max_enemies = 1;
  summon.interval_seconds = 2.0;
  summon.damage_per_hit = {30.0};
  params.auto_attacks.push_back(summon);

  Sequence played = MeasureFight(params, 100.0);
  EXPECT_NEAR(played.by_attack[0].damage, 100 * 10.0, 1e-6);
  EXPECT_NEAR(played.own_clock_damage, 50 * 30.0, 1e-6);
  EXPECT_NEAR(played.damage, played.by_attack[0].damage + 50 * 30.0, 1e-6);
}

// Two summons on one clock are reported as two named rows, heaviest first. A
// single combined row wouldn't say which summon did the damage.
TEST(MeasureFightTest, EveryOwnClockSourceIsNamedApart) {
  Mob mob = MakeMob("Snail", 10);
  CombatParams params = MakeParams(&mob, 1.0, 10.0);
  AttackOption phoenix;
  phoenix.name = "Phoenix";
  phoenix.max_enemies = 1;
  phoenix.interval_seconds = 2.0;
  phoenix.damage_per_hit = {30.0};
  params.auto_attacks.push_back(phoenix);
  AttackOption blaster = phoenix;
  blaster.name = "Arrow Blaster";
  blaster.interval_seconds = 5.0;
  blaster.damage_per_hit = {80.0};
  params.auto_attacks.push_back(blaster);

  Sequence played = MeasureFight(params, 100.0);
  ASSERT_EQ(played.own_clock_by_source.size(), 2u);
  EXPECT_EQ(played.own_clock_by_source[0].first, "Arrow Blaster");
  EXPECT_NEAR(played.own_clock_by_source[0].second, 20 * 80.0, 1e-6);
  EXPECT_EQ(played.own_clock_by_source[1].first, "Phoenix");
  EXPECT_NEAR(played.own_clock_by_source[1].second, 50 * 30.0, 1e-6);
  EXPECT_NEAR(played.own_clock_by_source[0].second +
                  played.own_clock_by_source[1].second,
              played.own_clock_damage, 1e-6);
}

// Damage that rides on an attack, such as a Final Attack or the burn it left,
// is included in that attack's total and also reported separately within it.
TEST(MeasureFightTest, WhatRidesASwingIsSplitOutOfIt) {
  Mob mob = MakeMob("Snail", 10);
  CombatParams params = MakeParams(&mob, 1.0, 10.0);
  FinalAttackRoll follow;
  follow.chance = 1.0;
  follow.damage = {4.0};
  params.attacks[0].final_attack_rolls.push_back(follow);
  params.attacks[0].final_attack_damage = {4.0};
  DotApplication burn;
  burn.damage = {1.0};
  burn.interval_seconds = 1.0;
  burn.duration_seconds = 100.0;
  burn.slot = 0;
  params.attacks[0].dots.push_back(burn);

  Sequence played = MeasureFight(params, 100.0, 1);
  EXPECT_NEAR(played.by_attack[0].final_attack_damage, 100 * 4.0, 1e-6);
  EXPECT_NEAR(played.by_attack[0].burn_damage, 99 * 1.0, 1e-6);
  // Both are already included in the attack's own total.
  EXPECT_NEAR(played.by_attack[0].damage,
              100 * 10.0 + played.by_attack[0].final_attack_damage +
                  played.by_attack[0].burn_damage,
              1e-6);
}

// A buff active for two seconds out of every ten reports an uptime of one
// fifth, which is what a pulse gated on it is worth.
TEST(MeasureFightTest, ABuffReportsTheShareItStood) {
  Mob mob = MakeMob("Snail", 10);
  CombatParams params = MakeParams(&mob, 1.0, 10.0);
  BuffOption buff;
  buff.duration_seconds = 2.0;
  buff.cooldown_seconds = 10.0;
  params.buffs.push_back(std::move(buff));
  AttackSet set;
  set.attacks = params.attacks;
  params.buffed[1] = std::move(set);

  Sequence played = MeasureFight(params, 1000.0);
  ASSERT_EQ(played.buff_uptime.size(), 1u);
  EXPECT_NEAR(played.buff_uptime[0], 0.2, 0.01);
}

// A hold's length depends on the target's HP. Against kMeasuredMobHp the orb
// runs all twelve pulses; against a real monster it stops at its minimum of
// five and measures at a fraction of its worth.
TEST(MeasureFightTest, AHoldRunsItsFullLengthAgainstAMeasurementDummy) {
  auto measure = [](int64_t hp) {
    Mob mob = MakeMob("Dummy", hp);
    CombatParams params = MakeParams(&mob, 0.0, 0.0);
    AttackOption& orb = params.attacks[0];
    orb.channel.pulses = 12;
    orb.channel.min_pulses = 5;
    orb.channel.pulse_seconds = 0.15;
    orb.channel.finish_seconds = 0.2;
    orb.channel.min_seconds = 0.96;
    orb.groups.push_back({{10.0}, SwingRolls{}});
    orb.damage_per_hit = {12 * 10.0};
    orb.swing_seconds = HoldSeconds(orb.channel, orb.channel.pulses);
    return MeasureFight(params, 96.0).damage;
  };
  EXPECT_NEAR(measure(kMeasuredMobHp), 48 * 120.0, 1e-6);  // 2s a hold
  EXPECT_NEAR(measure(1), 100 * 50.0, 1e-6);               // 0.96s a hold
}

TEST(MeasureFightTest, NothingToFightMeasuresNothing) {
  CombatParams params;
  Sequence played = MeasureFight(params, 100.0);
  EXPECT_EQ(played.damage, 0.0);
  EXPECT_EQ(played.main_attack, -1);
}

}  // namespace
}  // namespace ms
