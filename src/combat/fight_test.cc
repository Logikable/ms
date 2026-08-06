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

// A mob type together with the damage one swing does to it. Damage lives on
// the attack rather than the type now, but a test reads better stating the two
// side by side.
struct TypeSpec {
  const Mob* mob = nullptr;
  double damage = 0.0;
  int simultaneous = 0;
};

TypeSpec MakeType(const Mob* mob, double damage, int simultaneous) {
  return TypeSpec{mob, damage, simultaneous};
}

// Params with a single attack of the given reach -- the common case. Tests
// that need a choice between attacks push more onto params.attacks.
CombatParams MakeParams(double swing, double respawn,
                        std::vector<TypeSpec> specs, int reach = 1,
                        const std::string& map = "field") {
  CombatParams params;
  params.active = true;
  params.map = map;
  params.swing_seconds = swing;
  params.respawn_seconds = respawn;
  AttackOption attack;
  attack.max_enemies = reach;
  for (const TypeSpec& spec : specs) {
    CombatType type;
    type.mob = spec.mob;
    type.simultaneous = spec.simultaneous;
    params.types.push_back(type);
    attack.damage_per_hit.push_back(spec.damage);
  }
  params.attacks.push_back(std::move(attack));
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
  CombatParams params =
      MakeParams(1.0, 100.0, {MakeType(&snail, 10.0, 3)}, /*reach=*/3);

  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.kills_this_step()[0], 3);  // three kills on one swing
  EXPECT_TRUE(sim.respawning());           // queue cleared
}

TEST(CombatSimTest, MultiTargetReachIsCappedByRemainingMobs) {
  Mob snail = MakeMob("Snail", 10);
  CombatSim sim;
  // Reach 6 but only two mobs are up, so the swing hits (and clears) just two.
  CombatParams params =
      MakeParams(1.0, 100.0, {MakeType(&snail, 10.0, 2)}, /*reach=*/6);

  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.kills_this_step()[0], 2);
  EXPECT_TRUE(sim.respawning());
}

TEST(CombatSimTest, MultiTargetDrainsTheWindowInParallel) {
  Mob snail = MakeMob("Snail", 10);
  CombatSim sim;
  // Two mobs, a 2-way attack, 6 damage: each needs two hits. The first swing
  // leaves both alive at partial HP; the second kills both on the same swing.
  CombatParams params =
      MakeParams(1.0, 100.0, {MakeType(&snail, 6.0, 2)}, /*reach=*/2);

  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.kills_this_step()[0], 0);           // both at 4 HP, alive
  EXPECT_DOUBLE_EQ(sim.target_hp_fraction(), 0.4);  // 10 - 6

  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.kills_this_step()[0], 2);  // both die together
  EXPECT_TRUE(sim.respawning());
}

TEST(CombatSimTest, NamesNoSwingWhileRespawning) {
  Mob snail = MakeMob("Snail", 10);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 100.0, {MakeType(&snail, 10.0, 1)});

  sim.Advance(params, 0.5);  // mob up, a swing is coming
  EXPECT_EQ(sim.attack_name(), "Attack");

  sim.Advance(params, 0.5);  // one-shots the only mob
  ASSERT_TRUE(sim.respawning());
  EXPECT_TRUE(sim.attack_name().empty());
}

TEST(CombatSimTest, PicksTheAttackThatLandsTheMostOnTheQueue) {
  Mob snail = MakeMob("Snail", 100);
  CombatSim sim;
  // A wide, weak swing against a narrow, strong one, over four mobs: 4 x 5 = 20
  // beats 1 x 12, so the wide one takes it.
  CombatParams params = MakeParams(1.0, 100.0, {MakeType(&snail, 12.0, 4)});
  params.attacks[0].name = "Attack";
  AttackOption wide;
  wide.name = "Sweep";
  wide.max_enemies = 4;
  wide.damage_per_hit = {5.0};
  params.attacks.push_back(wide);

  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.attack_name(), "Sweep");
  // All four took the 5, rather than one taking 12.
  ASSERT_EQ(sim.engaged_groups().size(), 1u);
  EXPECT_EQ(sim.engaged_groups()[0].count, 4);
  EXPECT_NEAR(sim.engaged_groups()[0].hp_fraction, 0.95, 1e-9);
}

TEST(CombatSimTest, FallsBackToTheStrongSwingOnTheLastMob) {
  Mob snail = MakeMob("Snail", 100);
  CombatSim sim;
  // The same pair of attacks, but only one mob is up: the wide swing's reach is
  // worth nothing, so 5 loses to 12 and the narrow one takes over.
  CombatParams params = MakeParams(1.0, 100.0, {MakeType(&snail, 12.0, 1)});
  params.attacks[0].name = "Attack";
  AttackOption wide;
  wide.name = "Sweep";
  wide.max_enemies = 4;
  wide.damage_per_hit = {5.0};
  params.attacks.push_back(wide);

  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.attack_name(), "Attack");
  EXPECT_NEAR(sim.target_hp_fraction(), 0.88, 1e-9);  // took the 12
}

TEST(CombatSimTest, TheChoiceChangesAsTheQueueThins) {
  Mob snail = MakeMob("Snail", 10);
  CombatSim sim;
  // Two mobs, both one-shot by the wide swing. It clears them on the first
  // swing, and with the queue empty the narrow swing is what is charging next.
  CombatParams params = MakeParams(1.0, 100.0, {MakeType(&snail, 12.0, 2)});
  params.attacks[0].name = "Attack";
  AttackOption wide;
  wide.name = "Sweep";
  wide.max_enemies = 4;
  wide.damage_per_hit = {10.0};
  params.attacks.push_back(wide);

  sim.Advance(params, 0.5);  // charging, two mobs up: the sweep wins
  EXPECT_EQ(sim.attack_name(), "Sweep");

  sim.Advance(params, 0.5);  // it lands and clears both
  EXPECT_EQ(sim.kills_this_step()[0], 2);
  EXPECT_TRUE(sim.respawning());
}

TEST(CombatSimTest, EngagedGroupsAverageASingleTypesWindow) {
  Mob snail = MakeMob("Snail", 20, 3);
  CombatSim sim;
  // Five mobs, reach 3: only the front three are engaged, so the bar merges
  // three of them -- not all five.
  CombatParams params =
      MakeParams(1.0, 100.0, {MakeType(&snail, 4.0, 5)}, /*reach=*/3);

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
      1.0, 100.0, {MakeType(&snail, 50.0, 2), MakeType(&slug, 20.0, 2)},
      /*reach=*/4);

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

TEST(CombatSimTest, ARespawnBeatAddsOnlyTheMissingMobs) {
  Mob snail = MakeMob("Snail", 10);
  Mob slug = MakeMob("Slug", 100);
  CombatSim sim;
  // One swing hits both: the Snail dies outright, the Slug drops to 90%. The
  // beat at t=1.5 then falls between swings, with only the Snail to replace.
  CombatParams params = MakeParams(
      1.0, 1.5, {MakeType(&snail, 10.0, 1), MakeType(&slug, 10.0, 1)},
      /*reach=*/2);

  sim.Advance(params, 0.5);
  sim.Advance(params, 0.5);  // t=1: the swing lands, leaving the Slug alone
  ASSERT_EQ(sim.engaged_groups().size(), 1u);

  sim.Advance(params, 0.5);  // t=1.5: the beat
  const std::vector<EngagedGroup>& groups = sim.engaged_groups();
  ASSERT_EQ(groups.size(), 2u);
  const EngagedGroup* snails = FindGroup(groups, "Snail");
  const EngagedGroup* slugs = FindGroup(groups, "Slug");
  ASSERT_NE(snails, nullptr);
  ASSERT_NE(slugs, nullptr);
  // The Snail is a new spawn, so it arrives whole.
  EXPECT_NEAR(snails->hp_fraction, 1.0, 1e-9);
  // The Slug was never dead. A beat that rebuilt the roster would have healed
  // it back to full.
  EXPECT_NEAR(slugs->hp_fraction, 0.9, 1e-9);
}

TEST(CombatSimTest, AMobSlowerToKillThanTheRespawnBeatStillDies) {
  Mob slug = MakeMob("Slug", 100);
  CombatSim sim;
  // Ten swings to kill, with a beat every five. The damage has to survive the
  // beats or the mob can never be killed at all.
  CombatParams params = MakeParams(1.0, 5.0, {MakeType(&slug, 10.0, 1)});

  int64_t kills = 0;
  for (int i = 0; i < 10; ++i) {
    sim.Advance(params, 1.0);
    kills += sim.kills_this_step()[0];
  }
  EXPECT_EQ(kills, 1);
}

TEST(CombatSimTest, ARespawnBeatMidFightLeavesTheSwingCharging) {
  Mob snail = MakeMob("Snail", 100);
  CombatSim sim;
  // 10 dmg against 100 HP, so the mob is still up when the beat lands. Swing
  // 2s, beat 3s, stepped 0.5s at a time -- the beat at t=3 catches a swing
  // half charged.
  CombatParams params = MakeParams(2.0, 3.0, {MakeType(&snail, 10.0, 1)});

  for (int i = 0; i < 5; ++i) {
    sim.Advance(params, 0.5);  // t=2.5: swing landed at 2, phase back to 0.5
  }
  ASSERT_NEAR(sim.attack_fraction(), 0.25, 1e-9);

  sim.Advance(params, 0.5);  // t=3: the beat, then another half second
  EXPECT_FALSE(sim.respawning());
  // 0.5s of charge survived the beat and 0.5s was added on top. A beat that
  // restarted the swing would read 0.25 here.
  EXPECT_NEAR(sim.attack_fraction(), 0.5, 1e-9);
}

TEST(CombatSimTest, ARespawnBeatAfterAClearStartsAFreshSwing) {
  Mob snail = MakeMob("Snail", 10);
  CombatSim sim;
  // One swing clears the map. Stepping 0.75s against a 1s swing leaves 0.25s
  // of overshoot behind, which the idle stretch must not bank.
  CombatParams params = MakeParams(1.0, 3.0, {MakeType(&snail, 10.0, 1)});

  sim.Advance(params, 0.75);
  sim.Advance(params, 0.75);  // t=1.5: the swing lands and clears the map
  ASSERT_TRUE(sim.respawning());
  sim.Advance(params, 0.75);  // t=2.25: still idle, nothing charging
  ASSERT_TRUE(sim.respawning());

  sim.Advance(params, 0.75);  // t=3: the beat, then a fresh swing begins
  EXPECT_FALSE(sim.respawning());
  // Exactly the 0.75s since the beat. Carrying the overshoot would have put
  // the swing over the line and landed a hit already.
  EXPECT_NEAR(sim.attack_fraction(), 0.75, 1e-9);
  EXPECT_NEAR(sim.target_hp_fraction(), 1.0, 1e-9);
}

TEST(CombatSimTest, MovingToAnotherMapRestartsTheFightThere) {
  Mob snail = MakeMob("Snail", 30);
  Mob slug = MakeMob("Slug", 30);
  CombatSim sim;
  CombatParams here =
      MakeParams(1.0, 100.0, {MakeType(&snail, 10.0, 1)}, /*reach=*/1, "field");
  CombatParams there = MakeParams(1.0, 100.0, {MakeType(&slug, 10.0, 1)},
                                  /*reach=*/1, "other_field");

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

// Gives `params` a player with a pool to lose, hit every `interval` seconds
// for `damage` by whichever mob is at the front. Every type hits alike unless
// a test says otherwise.
void GivePlayerHp(CombatParams& params, int max_hp, double interval,
                  double damage) {
  params.max_player_hp = max_hp;
  params.hit_seconds = interval;
  for (CombatType& type : params.types) {
    type.damage_to_player = damage;
  }
}

TEST(CombatSimTest, TheEngagedMobHitsBackOnItsOwnClock) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  // A mob far too tough to kill, so nothing interrupts the incoming hits.
  CombatParams params = MakeParams(10.0, 1000.0, {MakeType(&snail, 1.0, 1)});
  GivePlayerHp(params, 100, /*interval=*/1.0, /*damage=*/10.0);

  sim.Advance(params, 0.5);  // mid-interval, nothing has landed
  EXPECT_EQ(sim.player_hp(), 100);
  EXPECT_DOUBLE_EQ(sim.player_hp_fraction(), 1.0);

  sim.Advance(params, 0.5);  // the interval closes
  EXPECT_EQ(sim.player_hp(), 90);
  EXPECT_DOUBLE_EQ(sim.player_hp_fraction(), 0.9);

  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.player_hp(), 80);
}

TEST(CombatSimTest, OnlyOneMobHitsBackHoweverManyAreOnTheMap) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  // Five of them standing there, and the player takes one hit, not five. A
  // crowd is a crowd, not five attackers.
  CombatParams params = MakeParams(10.0, 1000.0, {MakeType(&snail, 1.0, 5)});
  GivePlayerHp(params, 100, /*interval=*/1.0, /*damage=*/10.0);

  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.player_hp(), 90);
}

TEST(CombatSimTest, DamageTakenFollowsTheMobInFront) {
  Mob snail = MakeMob("Snail", 1000);
  Mob ogre = MakeMob("Ogre", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(
      10.0, 1000.0, {MakeType(&snail, 1.0, 1), MakeType(&ogre, 1.0, 1)});
  params.max_player_hp = 100;
  params.hit_seconds = 1.0;
  params.types[0].damage_to_player = 5.0;
  params.types[1].damage_to_player = 50.0;

  // Which of the two the queue puts in front is its own business -- it
  // shuffles the arrivals -- but the hit the player takes has to be that
  // one's, and not the other's.
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.player_hp(), sim.target_name() == "Snail" ? 95 : 50);
}

TEST(CombatSimTest, AnEmptyMapHasNothingToHitThePlayerWith) {
  Mob snail = MakeMob("Snail", 10);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 10.0, 1)});
  GivePlayerHp(params, 100, /*interval=*/1.0, /*damage=*/10.0);

  sim.Advance(params, 1.0);  // the snail hits, then the swing clears the map
  ASSERT_TRUE(sim.respawning());
  EXPECT_EQ(sim.player_hp(), 90);

  for (int i = 0; i < 5; ++i) {  // idling well past several intervals
    sim.Advance(params, 1.0);
  }
  EXPECT_EQ(sim.player_hp(), 90);
}

TEST(CombatSimTest, ClearingTheMapHealsThePlayer) {
  Mob snail = MakeMob("Snail", 10);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 3.0, {MakeType(&snail, 10.0, 1)});
  GivePlayerHp(params, 100, /*interval=*/1.0, /*damage=*/10.0);

  sim.Advance(params, 0.5);
  sim.Advance(params, 0.5);  // a hit lands, then the swing clears the map
  ASSERT_TRUE(sim.respawning());
  ASSERT_EQ(sim.player_hp(), 90);

  for (int i = 0; i < 4; ++i) {  // idle until the beat at 3.0s refills the map
    sim.Advance(params, 0.5);
  }
  EXPECT_FALSE(sim.respawning());
  EXPECT_EQ(sim.player_hp(), 100);
}

TEST(CombatSimTest, TheHitClockDoesNotBankTimeWhileTheMapIsEmpty) {
  Mob snail = MakeMob("Snail", 10);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 3.0, {MakeType(&snail, 10.0, 1)});
  GivePlayerHp(params, 100, /*interval=*/1.0, /*damage=*/10.0);

  sim.Advance(params, 0.5);
  sim.Advance(params, 0.5);  // hit, then the map is cleared
  ASSERT_EQ(sim.player_hp(), 90);
  for (int i = 0; i < 4; ++i) {  // two idle seconds, then the refill
    sim.Advance(params, 0.5);
  }
  ASSERT_FALSE(sim.respawning());
  ASSERT_EQ(sim.player_hp(), 100);

  // Nine tenths of a second of fighting since the map refilled. Had the two
  // idle seconds counted toward the mob's clock, a hit would have landed by
  // now -- several, in fact.
  sim.Advance(params, 0.4);
  EXPECT_EQ(sim.player_hp(), 100);
}

TEST(CombatSimTest, ARespawnBeatMidFightDoesNotHeal) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  // The mob outlasts the beat, so the top-up is more monsters arriving rather
  // than the player's breather.
  CombatParams params = MakeParams(10.0, 2.0, {MakeType(&snail, 1.0, 2)});
  GivePlayerHp(params, 100, /*interval=*/1.0, /*damage=*/10.0);

  sim.Advance(params, 1.0);
  sim.Advance(params, 1.0);  // the beat lands here, with both mobs still up
  ASSERT_FALSE(sim.respawning());
  EXPECT_EQ(sim.player_hp(), 80);
}

TEST(CombatSimTest, ChangingMapHealsThePlayer) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(10.0, 1000.0, {MakeType(&snail, 1.0, 1)});
  GivePlayerHp(params, 100, /*interval=*/1.0, /*damage=*/10.0);

  sim.Advance(params, 1.0);
  sim.Advance(params, 1.0);
  ASSERT_EQ(sim.player_hp(), 80);

  CombatParams elsewhere = MakeParams(10.0, 1000.0, {MakeType(&snail, 1.0, 1)},
                                      /*reach=*/1, /*map=*/"elsewhere");
  GivePlayerHp(elsewhere, 100, /*interval=*/1.0, /*damage=*/10.0);
  sim.Advance(elsewhere, 0.5);
  EXPECT_EQ(sim.player_hp(), 100);
}

TEST(CombatSimTest, PlayerHpStopsAtZero) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(10.0, 1000.0, {MakeType(&snail, 1.0, 1)});
  GivePlayerHp(params, 100, /*interval=*/1.0, /*damage=*/60.0);

  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.player_hp(), 40);

  sim.Advance(params, 1.0);  // more than the 40 that is left
  EXPECT_EQ(sim.player_hp(), 0);
  EXPECT_DOUBLE_EQ(sim.player_hp_fraction(), 0.0);
}

TEST(CombatSimTest, ASliverOfHpStillReadsAsOne) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(10.0, 1000.0, {MakeType(&snail, 1.0, 1)});
  GivePlayerHp(params, 100, /*interval=*/1.0, /*damage=*/99.5);

  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.player_hp(), 1);  // 0.5 left, which is not death
}

TEST(CombatSimTest, InactiveParamsLeaveThePlayerWithNoHpToShow) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(10.0, 1000.0, {MakeType(&snail, 1.0, 1)});
  GivePlayerHp(params, 100, /*interval=*/1.0, /*damage=*/10.0);
  sim.Advance(params, 1.0);
  ASSERT_EQ(sim.player_hp(), 90);

  sim.Advance(CombatParams{}, 1.0);
  EXPECT_EQ(sim.player_hp(), 0);
  EXPECT_DOUBLE_EQ(sim.player_hp_fraction(), 0.0);
}

}  // namespace
}  // namespace ms
