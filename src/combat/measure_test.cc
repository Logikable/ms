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

// One attack landing `damage` on one monster every `swing` seconds, reaching
// `reach` of them at once.
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
  ASSERT_EQ(played.damage_by_attack.size(), 1u);
  EXPECT_NEAR(played.damage_by_attack[0], played.damage, 1e-6);
}

// The crowd is what the caller asks for, whatever the map it came from holds.
TEST(MeasureFightTest, TheCrowdIsTheOneAskedFor) {
  Mob mob = MakeMob("Snail", 10);
  CombatParams params = MakeParams(&mob, 1.0, 10.0, /*reach=*/6);

  EXPECT_NEAR(MeasureFight(params, 10.0, 1).damage, 10 * 10.0, 1e-6);
  EXPECT_NEAR(MeasureFight(params, 10.0, 4).damage, 10 * 40.0, 1e-6);
  // The swing reaches six, so a wider crowd buys it nothing more.
  EXPECT_NEAR(MeasureFight(params, 10.0, 12).damage, 10 * 60.0, 1e-6);
}

// Two runs of one build agree exactly, which is the whole point: a sim ranking
// a scroll wants the scroll, not the dice.
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

// A summon runs beside the swing rather than instead of it, and is reported
// apart so a caller can say where the damage went.
TEST(MeasureFightTest, AnOwnClockCastIsCountedApart) {
  Mob mob = MakeMob("Snail", 10);
  CombatParams params = MakeParams(&mob, 1.0, 10.0);
  AttackOption summon;
  summon.max_enemies = 1;
  summon.interval_seconds = 2.0;
  summon.damage_per_hit = {30.0};
  params.auto_attacks.push_back(summon);

  Sequence played = MeasureFight(params, 100.0);
  EXPECT_NEAR(played.damage_by_attack[0], 100 * 10.0, 1e-6);
  EXPECT_NEAR(played.own_clock_damage, 50 * 30.0, 1e-6);
  EXPECT_NEAR(played.damage, played.damage_by_attack[0] + 50 * 30.0, 1e-6);
}

// A buff that stands for two seconds in every ten reads as a fifth of the run,
// which is what a pulse gated on it is worth.
TEST(MeasureFightTest, ABuffReportsTheShareItStood) {
  Mob mob = MakeMob("Snail", 10);
  CombatParams params = MakeParams(&mob, 1.0, 10.0);
  BuffOption buff;
  buff.duration_seconds = 2.0;
  buff.cooldown_seconds = 10.0;
  params.buffs.push_back(std::move(buff));
  AttackSet set;
  set.attacks = params.attacks;
  params.buffed.push_back(std::move(set));

  Sequence played = MeasureFight(params, 1000.0);
  ASSERT_EQ(played.buff_uptime.size(), 1u);
  EXPECT_NEAR(played.buff_uptime[0], 0.2, 0.01);
}

// A hold is sized to the HP in front of it, so a dummy decides how much of one
// the sim gets to see. At kMeasuredMobHp the orb runs all twelve pulses; stood
// up with a real monster's HP the same swing is let go at its floor of five,
// and the skill measures a fraction of what it is worth.
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
