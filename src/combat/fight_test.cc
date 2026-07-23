#include "src/combat/fight.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "src/combat/encounter.h"
#include "src/protos/mob.pb.h"

namespace ms {
namespace {

Mob MakeMob(const std::string& name, int max_hp, int level = 0) {
  Mob mob;
  mob.set_name(name);
  mob.set_max_hp(max_hp);
  mob.set_level(level);
  return mob;
}

CombatType MakeType(const Mob* mob, double damage, int simultaneous) {
  CombatType type;
  type.mob = mob;
  type.damage_per_hit = damage;
  type.simultaneous = simultaneous;
  return type;
}

CombatParams MakeParams(double swing, double respawn,
                        std::vector<CombatType> types,
                        const std::string& map = "field") {
  CombatParams params;
  params.active = true;
  params.map = map;
  params.swing_seconds = swing;
  params.respawn_seconds = respawn;
  params.types = std::move(types);
  return params;
}

const EngagedGroup* FindGroup(const std::vector<EngagedGroup>& groups,
                              const std::string& name) {
  for (const EngagedGroup& g : groups) {
    if (g.name == name) {
      return &g;
    }
  }
  return nullptr;
}

TEST(CombatSimTest, InactiveParamsLeaveSimIdle) {
  CombatSim sim;
  sim.Advance(CombatParams{}, 1.0);
  EXPECT_FALSE(sim.active());
  EXPECT_FALSE(sim.respawning());
  EXPECT_TRUE(sim.target_name().empty());
}

TEST(CombatSimTest, ChargesAttackBarThenLandsAHit) {
  Mob snail = MakeMob("Snail", 10);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 100.0, {MakeType(&snail, 4.0, 1)});

  sim.Advance(params, 0.5);
  EXPECT_TRUE(sim.active());
  EXPECT_EQ(sim.target_name(), "Snail");
  EXPECT_NEAR(sim.attack_fraction(), 0.5, 1e-9);
  EXPECT_DOUBLE_EQ(sim.target_hp_fraction(), 1.0);  // no hit landed yet

  sim.Advance(params, 0.5);  // swing completes -> one hit
  EXPECT_NEAR(sim.attack_fraction(), 0.0, 1e-9);
  EXPECT_NEAR(sim.target_hp_fraction(), 0.6, 1e-9);  // 10 - 4 = 6
}

TEST(CombatSimTest, KillingTheLastMobEntersRespawning) {
  Mob snail = MakeMob("Snail", 10);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 100.0, {MakeType(&snail, 10.0, 1)});

  sim.Advance(params, 1.0);  // one-shot the only mob
  EXPECT_TRUE(sim.respawning());
  EXPECT_TRUE(sim.target_name().empty());
}

TEST(CombatSimTest, ReportsTheTargetLevelWhileFightingAndZeroWhileRespawning) {
  Mob snail = MakeMob("Snail", 10, 5);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 100.0, {MakeType(&snail, 10.0, 1)});

  sim.Advance(params, 0.5);  // mid-swing, mob still up
  EXPECT_EQ(sim.target_level(), 5);

  sim.Advance(params, 0.5);  // swing lands, one-shots the only mob
  EXPECT_TRUE(sim.respawning());
  EXPECT_EQ(sim.target_level(), 0);
}

TEST(CombatSimTest, RecordsKillsForTheStepTheyHappen) {
  Mob snail = MakeMob("Snail", 10);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 100.0, {MakeType(&snail, 10.0, 2)});

  sim.Advance(params, 0.5);  // still charging, no kill yet
  ASSERT_EQ(sim.kills_this_step().size(), 1u);
  EXPECT_EQ(sim.kills_this_step()[0], 0);

  sim.Advance(params, 0.5);  // swing completes -> one kill
  EXPECT_EQ(sim.kills_this_step()[0], 1);

  sim.Advance(params, 0.5);  // still charging the next swing, no new kill
  EXPECT_EQ(sim.kills_this_step()[0], 0);
}

TEST(CombatSimTest, AdvancesToTheNextMobAfterAKill) {
  Mob snail = MakeMob("Snail", 10);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 100.0, {MakeType(&snail, 10.0, 2)});

  sim.Advance(params, 1.0);  // kill first of two
  EXPECT_FALSE(sim.respawning());
  EXPECT_DOUBLE_EQ(sim.target_hp_fraction(), 1.0);  // next mob, full HP

  sim.Advance(params, 0.9);  // charging, no swing yet
  EXPECT_DOUBLE_EQ(sim.target_hp_fraction(), 1.0);

  sim.Advance(params, 1.0);  // kill the second -> queue empty
  EXPECT_TRUE(sim.respawning());
}

TEST(CombatSimTest, SingleTargetReachLeavesTheSecondMobUntouched) {
  Mob snail = MakeMob("Snail", 10);
  CombatSim sim;
  // Reach 1 (the default): a swing hits only the front mob, so the second is
  // still at full HP after the first dies.
  CombatParams params = MakeParams(1.0, 100.0, {MakeType(&snail, 10.0, 2)});

  sim.Advance(params, 1.0);  // one-shot the front mob
  EXPECT_EQ(sim.kills_this_step()[0], 1);
  EXPECT_DOUBLE_EQ(sim.target_hp_fraction(), 1.0);  // the next is full, unhit
}

TEST(CombatSimTest, MultiTargetSwingHitsAndKillsSeveralAtOnce) {
  Mob snail = MakeMob("Snail", 10);
  CombatSim sim;
  // A 3-way attack over three mobs: one swing hits and one-shots all three.
  CombatParams params = MakeParams(1.0, 100.0, {MakeType(&snail, 10.0, 3)});
  params.attack_targets = 3;

  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.kills_this_step()[0], 3);  // three kills on one swing
  EXPECT_TRUE(sim.respawning());           // queue cleared
}

TEST(CombatSimTest, MultiTargetReachIsCappedByRemainingMobs) {
  Mob snail = MakeMob("Snail", 10);
  CombatSim sim;
  // Reach 6 but only two mobs are up, so the swing hits (and clears) just two.
  CombatParams params = MakeParams(1.0, 100.0, {MakeType(&snail, 10.0, 2)});
  params.attack_targets = 6;

  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.kills_this_step()[0], 2);
  EXPECT_TRUE(sim.respawning());
}

TEST(CombatSimTest, MultiTargetDrainsTheWindowInParallel) {
  Mob snail = MakeMob("Snail", 10);
  CombatSim sim;
  // Two mobs, a 2-way attack, 6 damage: each needs two hits. The first swing
  // leaves both alive at partial HP; the second kills both on the same swing.
  CombatParams params = MakeParams(1.0, 100.0, {MakeType(&snail, 6.0, 2)});
  params.attack_targets = 2;

  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.kills_this_step()[0], 0);           // both at 4 HP, alive
  EXPECT_DOUBLE_EQ(sim.target_hp_fraction(), 0.4);  // 10 - 6

  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.kills_this_step()[0], 2);  // both die together
  EXPECT_TRUE(sim.respawning());
}

TEST(CombatSimTest, EngagedGroupsAverageASingleTypesWindow) {
  Mob snail = MakeMob("Snail", 20, 3);
  CombatSim sim;
  // Five mobs, reach 3: only the front three are engaged, so the bar merges
  // three of them -- not all five.
  CombatParams params = MakeParams(1.0, 100.0, {MakeType(&snail, 4.0, 5)});
  params.attack_targets = 3;

  sim.Advance(params, 1.0);  // front three to 16/20 = 0.8
  ASSERT_EQ(sim.engaged_groups().size(), 1u);
  EXPECT_EQ(sim.engaged_groups()[0].name, "Snail");
  EXPECT_EQ(sim.engaged_groups()[0].level, 3);
  EXPECT_EQ(sim.engaged_groups()[0].count, 3);
  EXPECT_NEAR(sim.engaged_groups()[0].hp_fraction, 0.8, 1e-9);
}

TEST(CombatSimTest, EngagedGroupsMergeTheWindowByType) {
  Mob snail = MakeMob("Snail", 100, 1);
  Mob slug = MakeMob("Slug", 100, 2);
  CombatSim sim;
  // Two of each, reach 4 hits the whole queue. Same-type mobs entered together
  // and take the same damage, so each type merges to one bar at a shared HP.
  CombatParams params = MakeParams(
      1.0, 100.0, {MakeType(&snail, 50.0, 2), MakeType(&slug, 20.0, 2)});
  params.attack_targets = 4;

  sim.Advance(params, 1.0);  // Snails -> 0.5, Slugs -> 0.8
  const std::vector<EngagedGroup>& groups = sim.engaged_groups();
  ASSERT_EQ(groups.size(), 2u);
  const EngagedGroup* snails = FindGroup(groups, "Snail");
  const EngagedGroup* slugs = FindGroup(groups, "Slug");
  ASSERT_NE(snails, nullptr);
  ASSERT_NE(slugs, nullptr);
  EXPECT_EQ(snails->count, 2);
  EXPECT_NEAR(snails->hp_fraction, 0.5, 1e-9);
  EXPECT_EQ(slugs->count, 2);
  EXPECT_NEAR(slugs->hp_fraction, 0.8, 1e-9);
}

TEST(CombatSimTest, RefillsAtTheRespawnBeat) {
  Mob snail = MakeMob("Snail", 30);
  CombatSim sim;
  // 30 HP / 10 dmg = 3 hits to clear the lone mob; respawn beat at 5s.
  CombatParams params = MakeParams(1.0, 5.0, {MakeType(&snail, 10.0, 1)});

  sim.Advance(params, 1.0);  // hp 20
  sim.Advance(params, 1.0);  // hp 10
  sim.Advance(params, 1.0);  // hp 0 -> respawning; respawn_phase = 3
  EXPECT_TRUE(sim.respawning());
  sim.Advance(params, 1.0);  // respawn_phase = 4, still idle
  EXPECT_TRUE(sim.respawning());
  sim.Advance(params, 1.0);  // respawn_phase = 5 -> refill, then one hit
  EXPECT_FALSE(sim.respawning());
  EXPECT_NEAR(sim.target_hp_fraction(), 20.0 / 30.0, 1e-9);
}

TEST(CombatSimTest, MovingToAnotherMapRestartsTheFightThere) {
  Mob snail = MakeMob("Snail", 30);
  Mob slug = MakeMob("Slug", 30);
  CombatSim sim;
  CombatParams here =
      MakeParams(1.0, 100.0, {MakeType(&snail, 10.0, 1)}, "field");
  CombatParams there =
      MakeParams(1.0, 100.0, {MakeType(&slug, 10.0, 1)}, "other_field");

  sim.Advance(here, 1.0);  // engage the Snail: hp 20
  EXPECT_EQ(sim.target_name(), "Snail");
  EXPECT_NEAR(sim.target_hp_fraction(), 20.0 / 30.0, 1e-9);

  // The move re-engages from the new map: a fresh Slug, whose 30 HP takes this
  // step's hit rather than carrying the Snail's damage -- and the kill credited
  // to the new map's lone type, not a stale index from the old roster.
  sim.Advance(there, 1.0);
  EXPECT_EQ(sim.target_name(), "Slug");
  EXPECT_NEAR(sim.target_hp_fraction(), 20.0 / 30.0, 1e-9);

  sim.Advance(there, 1.0);
  sim.Advance(there, 1.0);  // hp 0 -> the Slug dies
  EXPECT_EQ(sim.kills_this_step()[0], 1);
}

TEST(CombatSimTest, ShufflingTheRosterSpreadsKillsAcrossTypes) {
  Mob snail = MakeMob("Snail", 10);
  Mob blue = MakeMob("Blue Snail", 10);
  CombatSim sim;
  // Six mobs but only a couple die before the 5s beat refills the whole roster,
  // so the player never clears it. Were the fight order fixed, only the front
  // type would ever be reached; the shuffle must let both types die over beats.
  CombatParams params = MakeParams(
      1.0, 5.0, {MakeType(&snail, 10.0, 3), MakeType(&blue, 10.0, 3)});

  int64_t snail_kills = 0;
  int64_t blue_kills = 0;
  for (int step = 0; step < 200; ++step) {
    sim.Advance(params, 1.0);
    snail_kills += sim.kills_this_step()[0];
    blue_kills += sim.kills_this_step()[1];
  }

  EXPECT_GT(snail_kills, 0);
  EXPECT_GT(blue_kills, 0);
}

TEST(CombatSimTest, ClampsLargeGapsToOneSwing) {
  Mob snail = MakeMob("Snail", 100);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 100.0, {MakeType(&snail, 4.0, 1)});
  sim.Advance(params, 1000.0);  // huge gap -> at most one swing of damage
  EXPECT_NEAR(sim.target_hp_fraction(), 0.96, 1e-9);  // 100 - 4 = 96
}

}  // namespace
}  // namespace ms
