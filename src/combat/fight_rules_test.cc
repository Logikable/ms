// The hooks a boss fight's rules drive: parking a monster out of reach, the
// damage it takes, its HP floor, shields and revival.
#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "src/combat/encounter.h"
#include "src/combat/fight.h"
#include "src/protos/mob.pb.h"

namespace ms {
namespace {

Mob MakeMob(const std::string& name, int max_hp) {
  Mob mob;
  mob.set_name(name);
  mob.set_max_hp(max_hp);
  mob.set_boss(true);
  return mob;
}

// One 1s attack dealing `damage` to each of `mobs`, one of each, nothing
// respawning.
CombatParams MakeParams(const std::vector<const Mob*>& mobs, double damage,
                        int reach) {
  CombatParams params;
  params.active = true;
  params.encounter = "arena";
  AttackOption attack;
  attack.max_enemies = reach;
  attack.swing_seconds = 1.0;
  for (const Mob* mob : mobs) {
    CombatType type;
    type.mob = mob;
    type.simultaneous = 1;
    params.types.push_back(type);
    attack.damage_per_hit.push_back(damage);
  }
  params.attacks.push_back(attack);
  return params;
}

// HP fraction by mob name, from the roster.
std::map<std::string, double> HpByName(const CombatSim& sim) {
  std::map<std::string, double> hp;
  for (const MobStatus& mob : sim.view().roster) {
    hp[mob.name] = mob.hp_fraction;
  }
  return hp;
}

int IdOf(const CombatSim& sim, const std::string& name) {
  for (const MobStatus& mob : sim.view().roster) {
    if (mob.name == name) {
      return mob.id;
    }
  }
  return -1;
}

TEST(FightRulesTest, AParkedMonsterIsNotHitButStaysInTheRoster) {
  Mob a = MakeMob("A", 1000);
  Mob b = MakeMob("B", 1000);
  CombatParams params = MakeParams({&a, &b}, 100.0, /*reach=*/2);
  CombatSim sim;
  sim.Advance(params, 0.0);
  sim.SetMobRules(params, {{IdOf(sim, "B"), {.reachable = false}}});
  sim.Advance(params, 1.0);
  EXPECT_DOUBLE_EQ(HpByName(sim)["A"], 0.9);
  EXPECT_DOUBLE_EQ(HpByName(sim)["B"], 1.0);

  sim.SetMobRules(params, {});
  sim.Advance(params, 1.0);
  EXPECT_DOUBLE_EQ(HpByName(sim)["B"], 0.9);
}

TEST(FightRulesTest, ARespawnDoesNotReplaceAParkedMonster) {
  Mob a = MakeMob("A", 1000);
  CombatParams params = MakeParams({&a}, 100.0, /*reach=*/1);
  params.respawn_seconds = 1.0;
  CombatSim sim;
  sim.Advance(params, 0.0);
  sim.SetMobRules(params, {{IdOf(sim, "A"), {.reachable = false}}});
  for (int i = 0; i < 3; ++i) {
    sim.Advance(params, 1.0);
  }
  EXPECT_EQ(sim.view().roster.size(), 1u);
}

TEST(FightRulesTest, DamageTakenScalesTheHitAndTheNumbersShown) {
  Mob a = MakeMob("A", 1000);
  CombatParams params = MakeParams({&a}, 100.0, /*reach=*/1);
  params.record_damage_lines = true;
  CombatSim sim;
  sim.Advance(params, 0.0);
  sim.SetMobRules(params, {{IdOf(sim, "A"), {.damage_taken = 0.5}}});
  sim.Advance(params, 1.0);
  EXPECT_DOUBLE_EQ(HpByName(sim)["A"], 0.95);
  double shown = 0.0;
  for (const DamageLine& line : sim.damage_lines_this_step()) {
    shown += line.damage;
  }
  EXPECT_DOUBLE_EQ(shown, 50.0);
}

TEST(FightRulesTest, AFloorHoldsHpThere) {
  Mob a = MakeMob("A", 1000);
  CombatParams params = MakeParams({&a}, 400.0, /*reach=*/1);
  CombatSim sim;
  sim.Advance(params, 0.0);
  sim.SetMobRules(params, {{IdOf(sim, "A"), {.hp_floor = 0.75}}});
  sim.Advance(params, 1.0);
  sim.Advance(params, 1.0);
  EXPECT_DOUBLE_EQ(HpByName(sim)["A"], 0.75);

  sim.SetMobRules(params, {});
  sim.Advance(params, 1.0);
  EXPECT_DOUBLE_EQ(HpByName(sim)["A"], 0.35);
}

// One target: the attack goes to the monster it can still hurt, not the one
// held at its floor in front.
TEST(FightRulesTest, AOneTargetAttackPassesOverAMonsterAtItsFloor) {
  Mob a = MakeMob("A", 1000);
  Mob b = MakeMob("B", 1000);
  CombatParams params = MakeParams({&a, &b}, 100.0, /*reach=*/1);
  CombatSim sim;
  sim.Advance(params, 0.0);
  const std::string front = sim.view().roster.front().name;
  const std::string back = front == "A" ? "B" : "A";
  sim.SetMobRules(params, {{IdOf(sim, front), {.hp_floor = 1.0}}});
  sim.Advance(params, 1.0);
  EXPECT_DOUBLE_EQ(HpByName(sim)[front], 1.0);
  EXPECT_DOUBLE_EQ(HpByName(sim)[back], 0.9);
}

// The shield soaks the first hits and shows in the roster until it breaks; it
// holds even at a floor, which only HP has.
TEST(FightRulesTest, AShieldSoaksHitsBeforeHp) {
  Mob a = MakeMob("A", 1000);
  CombatParams params = MakeParams({&a}, 200.0, /*reach=*/1);
  CombatSim sim;
  sim.Advance(params, 0.0);
  EXPECT_TRUE(sim.AddShield(params, IdOf(sim, "A"), 0.25));
  EXPECT_FALSE(sim.AddShield(params, 12345, 0.25));
  EXPECT_DOUBLE_EQ(sim.view().roster.front().shield_fraction, 0.25);
  sim.Advance(params, 1.0);
  EXPECT_DOUBLE_EQ(HpByName(sim)["A"], 1.0);
  EXPECT_DOUBLE_EQ(sim.view().roster.front().shield_fraction, 0.05);
  sim.Advance(params, 1.0);
  EXPECT_DOUBLE_EQ(HpByName(sim)["A"], 0.85);
  EXPECT_DOUBLE_EQ(sim.view().roster.front().shield_fraction, 0.0);
}

TEST(FightRulesTest, ClampingLowersAShieldToTheServers) {
  Mob a = MakeMob("A", 1000);
  CombatParams params = MakeParams({&a}, 100.0, /*reach=*/1);
  CombatSim sim;
  sim.Advance(params, 0.0);
  const int id = IdOf(sim, "A");
  sim.AddShield(params, id, 0.3);
  sim.ClampRoster(params, {}, {{id, 0.1}});
  EXPECT_DOUBLE_EQ(sim.view().roster.front().shield_fraction, 0.1);
  sim.ClampRoster(params, {}, {{id, 0.5}});
  EXPECT_DOUBLE_EQ(sim.view().roster.front().shield_fraction, 0.1);
}

TEST(FightRulesTest, ClampingReachesAParkedMonster) {
  Mob a = MakeMob("A", 1000);
  Mob b = MakeMob("B", 1000);
  CombatParams params = MakeParams({&a, &b}, 100.0, /*reach=*/2);
  CombatSim sim;
  sim.Advance(params, 0.0);
  const int b_id = IdOf(sim, "B");
  sim.SetMobRules(params, {{b_id, {.reachable = false}}});
  sim.ClampRoster(params, {{b_id, 0.0}});
  EXPECT_EQ(sim.view().roster.size(), 1u);
  EXPECT_EQ(IdOf(sim, "B"), -1);
}

TEST(FightRulesTest, ARevivedMonsterKeepsItsIdAndTakesHitsAgain) {
  Mob a = MakeMob("A", 1000);
  CombatParams params = MakeParams({&a}, 100.0, /*reach=*/1);
  CombatSim sim;
  sim.Advance(params, 0.0);
  const int id = IdOf(sim, "A");
  sim.ClampRoster(params, {{id, 0.0}});
  ASSERT_TRUE(sim.view().roster.empty());

  sim.Revive(params, id, /*type=*/0, 0.5);
  sim.Revive(params, id, /*type=*/0, 0.9);  // already standing
  ASSERT_EQ(sim.view().roster.size(), 1u);
  EXPECT_EQ(sim.view().roster.front().id, id);
  sim.Advance(params, 1.0);
  EXPECT_DOUBLE_EQ(HpByName(sim)["A"], 0.4);
}

// Gloom's guard: shut at a tenth until `opens_at`, then open for 30s, opening
// again 55s after. What the boss rules would set at `now`.
MobRule GuardRule(double opens_at, double now) {
  MobRule rule;
  rule.guard.cut = 0.1;
  rule.guard.open_seconds = 30.0;
  rule.guard.interval_seconds = 55.0;
  if (now < opens_at) {
    rule.damage_taken = 0.1;
    rule.guard.opens_in = opens_at - now;
  } else {
    rule.guard.open_left = std::max(0.0, opens_at + 30.0 - now);
    rule.guard.opens_in = opens_at + 55.0 - now;
    if (rule.guard.open_left <= 0.0) {
      rule.damage_taken = 0.1;
    }
  }
  return rule;
}

// A boss whose guard opens at `opens_at`, a 10-damage basic attack and a
// 100-damage Big Move on `cooldown`.
CombatParams MakeGuardFight(const Mob& boss, double cooldown) {
  CombatParams params = MakeParams({&boss}, 10.0, /*reach=*/1);
  AttackOption big;
  big.name = "Big Move";
  big.max_enemies = 1;
  big.damage_per_hit = {100.0};
  big.swing_seconds = 1.0;
  big.cooldown_seconds = cooldown;
  params.attacks.push_back(big);
  return params;
}

// Adds a buff on its own cooldown that doubles every attack while up.
void GiveDoubling(CombatParams& params, double duration, double cooldown,
                  double soften = 0.0) {
  BuffOption buff;
  buff.name = "Burst";
  buff.duration_seconds = duration;
  buff.cooldown_seconds = cooldown;
  buff.damage_taken_pct = soften;
  params.buffs.push_back(buff);
  AttackSet set;
  set.attacks = params.attacks;
  for (AttackOption& attack : set.attacks) {
    for (double& damage : attack.damage_per_hit) {
      damage *= 2.0;
    }
  }
  params.buffed[1] = std::move(set);
}

// Steps a fight for `seconds` under a guard opening at `opens_at`, and returns
// when the attack at `index` landed, or with `buff` when buff 0 went up. The
// first step, at 0, comes before any rules, as a boss run's countdown does.
std::vector<double> Times(const CombatParams& params, double opens_at,
                          double seconds, int index, bool buff = false) {
  const double step = 0.05;
  CombatSim sim;
  sim.Advance(params, 0.0);
  const int id = sim.view().roster.front().id;
  std::vector<double> times;
  bool was_up = false;
  int landed = 0;
  if (buff && (sim.buff_mask() & 1) != 0) {
    times.push_back(0.0);
    was_up = true;
  }
  for (int n = 0; n * step < seconds; ++n) {
    sim.SetMobRules(params, {{id, GuardRule(opens_at, n * step)}});
    sim.Advance(params, step);
    const bool up = (sim.buff_mask() & 1) != 0;
    if (buff && up && !was_up) {
      times.push_back(n * step);
    }
    was_up = up;
    if (!buff && index < static_cast<int>(sim.by_attack().size()) &&
        sim.by_attack()[index].swings > landed) {
      landed = sim.by_attack()[index].swings;
      times.push_back((n + 1) * step);
    }
  }
  return times;
}

// The move is back at 12 with the opening eight seconds off: waiting costs
// 8/12 of a use at an average 0.59 of full, and lands this one at ten times.
TEST(FightRulesTest, ACooldownWaitsForTheGuardToOpen) {
  Mob boss = MakeMob("Gloom", 100000000);
  CombatParams params = MakeGuardFight(boss, /*cooldown=*/12.0);
  std::vector<double> times = Times(params, /*opens_at=*/20.0, 25.0, 1);
  ASSERT_EQ(times.size(), 2u);
  EXPECT_NEAR(times[1], 21.0, 0.11);
}

// Fifty seconds off costs four uses for the 0.9 gained: it's used now.
TEST(FightRulesTest, AnOpeningFarOffIsNotWaitedFor) {
  Mob boss = MakeMob("Gloom", 100000000);
  CombatParams params = MakeGuardFight(boss, /*cooldown=*/12.0);
  std::vector<double> times = Times(params, /*opens_at=*/62.0, 15.0, 1);
  ASSERT_EQ(times.size(), 2u);
  EXPECT_NEAR(times[1], 13.0, 0.11);
}

// At 12 the boss has 11 HP left and has taken 1.67 a second, so it will be dead
// before the opening: it gets the move now.
TEST(FightRulesTest, NoCooldownWaitsForAnOpeningPastTheEnd) {
  Mob boss = MakeMob("Gloom", 31);
  CombatParams params = MakeGuardFight(boss, /*cooldown=*/12.0);
  std::vector<double> times = Times(params, /*opens_at=*/20.0, 15.0, 1);
  ASSERT_EQ(times.size(), 2u);
  EXPECT_NEAR(times[1], 13.0, 0.11);
}

// Back at 15 with the opening ten seconds off, a 5s buff covers half a second
// at full damage now and all five then.
TEST(FightRulesTest, ABurstBuffWaitsForTheGuardToOpen) {
  Mob boss = MakeMob("Gloom", 100000000);
  CombatParams params = MakeGuardFight(boss, /*cooldown=*/60.0);
  GiveDoubling(params, /*duration=*/5.0, /*cooldown=*/15.0);
  std::vector<double> times = Times(params, /*opens_at=*/25.0, 30.0, 0, true);
  ASSERT_EQ(times.size(), 2u);
  EXPECT_NEAR(times[1], 25.0, 0.06);
}

// A buff that also guards the player is wanted whatever the boss takes.
TEST(FightRulesTest, AGuardingBuffIsRaisedAtOnce) {
  Mob boss = MakeMob("Gloom", 100000000);
  CombatParams params = MakeGuardFight(boss, /*cooldown=*/60.0);
  GiveDoubling(params, /*duration=*/5.0, /*cooldown=*/15.0, /*soften=*/0.2);
  std::vector<double> times = Times(params, /*opens_at=*/25.0, 20.0, 0, true);
  ASSERT_EQ(times.size(), 2u);
  EXPECT_NEAR(times[1], 15.0, 0.06);
}

// Back at 30 with two seconds of an opening left, a 20s buff covers 3.8s at
// full damage; at the next opening, 27s later, all 20.
TEST(FightRulesTest, ABurstBuffWaitsOutAnOpeningEnding) {
  Mob boss = MakeMob("Gloom", 100000000);
  CombatParams params = MakeGuardFight(boss, /*cooldown=*/60.0);
  GiveDoubling(params, /*duration=*/20.0, /*cooldown=*/30.0);
  std::vector<double> times = Times(params, /*opens_at=*/2.0, 60.0, 0, true);
  ASSERT_EQ(times.size(), 2u);
  EXPECT_NEAR(times[1], 57.0, 0.06);
}
}  // namespace
}  // namespace ms
