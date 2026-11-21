// The hooks a boss fight's rules drive: parking a monster out of reach, the
// damage it takes, its HP floor, shields and revival.
#include <gtest/gtest.h>

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

TEST(FightRulesTest, AShieldCanTakeHpPastMax) {
  Mob a = MakeMob("A", 1000);
  CombatParams params = MakeParams({&a}, 500.0, /*reach=*/1);
  CombatSim sim;
  sim.Advance(params, 0.0);
  EXPECT_TRUE(sim.AddHp(params, IdOf(sim, "A"), 0.25));
  EXPECT_FALSE(sim.AddHp(params, 12345, 0.25));
  sim.Advance(params, 1.0);
  sim.Advance(params, 1.0);
  // 1250 HP outlasts two 500 hits.
  EXPECT_DOUBLE_EQ(HpByName(sim)["A"], 0.25);
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

}  // namespace
}  // namespace ms
