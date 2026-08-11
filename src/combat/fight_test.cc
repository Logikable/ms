#include "src/combat/fight.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
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
  params.respawn_seconds = respawn;
  AttackOption attack;
  attack.max_enemies = reach;
  attack.swing_seconds = swing;
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

// A swing worth three pokes, held back by a three-second cooldown -- so it
// lands once every four swings rather than every one.
AttackOption MakeBurst() {
  AttackOption burst;
  burst.name = "Burst";
  burst.max_enemies = 1;
  burst.damage_per_hit = {30.0};
  burst.swing_seconds = 1.0;
  burst.cooldown_seconds = 3.0;
  return burst;
}

// A learned swing, harder than the poke and held back for a moment after it
// lands so that something else has to fill the gap.
AttackOption MakeSkill(const std::string& name, double damage,
                       double cooldown) {
  AttackOption skill;
  skill.name = name;
  skill.max_enemies = 1;
  skill.damage_per_hit = {damage};
  skill.swing_seconds = 1.0;
  skill.cooldown_seconds = cooldown;
  return skill;
}

// Adds a skill that fires on its own clock, hitting `reach` mobs for `damage`
// apiece every `interval` seconds.
void AddAutoAttack(CombatParams& params, double interval, double damage,
                   int reach = 1) {
  AttackOption cast;
  cast.name = "Evil Eye Shock";
  cast.max_enemies = reach;
  cast.interval_seconds = interval;
  cast.damage_per_hit.assign(params.types.size(), damage);
  params.auto_attacks.push_back(std::move(cast));
}

// Adds a skill clocked by swings landed rather than by seconds, hitting
// `reach` mobs for `damage` apiece every `attacks` of them.
void AddTriggeredAttack(CombatParams& params, int attacks, double damage,
                        int reach = 1) {
  AttackOption cast;
  cast.name = "Speed Mirage";
  cast.max_enemies = reach;
  cast.attacks_per_cast = attacks;
  cast.damage_per_hit.assign(params.types.size(), damage);
  params.triggered_attacks.push_back(std::move(cast));
}

// Gives `attack` a bigger form that takes the place of every `every`th swing
// of it, hitting `reach` mobs for `damage` apiece.
void SetEmpoweredForm(AttackOption& attack, int every, double damage,
                      int reach = 1) {
  std::shared_ptr<AttackOption> form = std::make_shared<AttackOption>();
  form->name = "Empowered " + attack.name;
  form->max_enemies = reach;
  form->swing_seconds = attack.swing_seconds;
  form->damage_per_hit.assign(attack.damage_per_hit.size(), damage);
  attack.empowered_every = every;
  attack.empowered = form;
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

TEST(CombatSimTest, ReportsTheTargetLevelOnlyWhileFighting) {
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

TEST(CombatSimTest, ASingleTargetSwingSparesTheSecond) {
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
  wide.swing_seconds = 1.0;  // as quick as the poke, so only damage decides
  wide.damage_per_hit = {5.0};
  params.attacks.push_back(wide);

  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.attack_name(), "Sweep");
  // All four took the 5, rather than one taking 12.
  ASSERT_EQ(sim.engaged_groups().size(), 1u);
  EXPECT_EQ(sim.engaged_groups()[0].count, 4);
  EXPECT_NEAR(sim.engaged_groups()[0].hp_fraction, 0.95, 1e-9);
}

// A swing is worth what it lands per second, not per swing: a skill hitting
// half again as hard but taking twice as long is the worse choice.
TEST(CombatSimTest, PrefersTheFasterSwingWhenItLandsMorePerSecond) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 100.0, {MakeType(&snail, 10.0, 1)});
  params.attacks[0].name = "Attack";
  AttackOption heavy;
  heavy.name = "Heavy";
  heavy.max_enemies = 1;
  heavy.damage_per_hit = {15.0};  // 50% harder
  heavy.swing_seconds = 2.0;      // but twice as slow: 7.5/s against 10/s
  params.attacks.push_back(heavy);

  sim.Advance(params, 0.1);
  EXPECT_EQ(sim.attack_name(), "Attack");
}

// And the same skill wins once its animation is quick enough to pay for
// itself, which is the whole reason the delay is per skill.
TEST(CombatSimTest, TheSlowerSwingWinsWhenItHitsHardEnough) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 100.0, {MakeType(&snail, 10.0, 1)});
  params.attacks[0].name = "Attack";
  AttackOption heavy;
  heavy.name = "Heavy";
  heavy.max_enemies = 1;
  heavy.damage_per_hit = {25.0};  // 25/s against 10/s
  heavy.swing_seconds = 2.0;
  params.attacks.push_back(heavy);

  sim.Advance(params, 0.1);
  EXPECT_EQ(sim.attack_name(), "Heavy");
}

// Final Attack rides the swing, so it is part of what the swing is worth. It
// does not depend on which skill set it off, which means a slower swing
// spreads the same extra hit over more seconds -- and that alone can decide
// the choice.
TEST(CombatSimTest, TheChoiceCountsTheFinalAttackThatFollowsIt) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  // Counting it: the poke is 30/s against the heavy swing's 25/s. Ignoring it:
  // 10/s against 15/s, and the heavy swing would win instead.
  CombatParams params = MakeParams(1.0, 100.0, {MakeType(&snail, 10.0, 1)});
  params.attacks[0].name = "Attack";
  params.attacks[0].final_attack_damage = {20.0};
  AttackOption heavy;
  heavy.name = "Heavy";
  heavy.max_enemies = 1;
  heavy.damage_per_hit = {30.0};
  heavy.swing_seconds = 2.0;
  heavy.final_attack_damage = {20.0};
  params.attacks.push_back(heavy);

  sim.Advance(params, 0.1);
  EXPECT_EQ(sim.attack_name(), "Attack");
}

// A swing three times the poke would simply replace it, so what a cooldown is
// worth is the swings it is NOT there for.
TEST(CombatSimTest, ACooldownKeepsTheBestSwingOffTheMenu) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 100.0, {MakeType(&snail, 10.0, 1)});
  params.attacks[0].name = "Attack";
  params.attacks.push_back(MakeBurst());

  for (int i = 0; i < 3; ++i) {
    sim.Advance(params, 1.0);
  }
  // One burst and then two pokes: 50, not the 90 three bursts would be.
  EXPECT_NEAR(sim.target_hp_fraction(), 0.95, 1e-9);
}

TEST(CombatSimTest, ACooldownSwingComesBackWhenItRunsOut) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 100.0, {MakeType(&snail, 10.0, 1)});
  params.attacks[0].name = "Attack";
  params.attacks.push_back(MakeBurst());

  for (int i = 0; i < 4; ++i) {
    sim.Advance(params, 1.0);
  }
  // The cooldown started on the first swing, so three seconds later the fourth
  // is a burst again: 30 + 10 + 10 + 30.
  EXPECT_NEAR(sim.target_hp_fraction(), 0.92, 1e-9);
}

// Unlike a summon's clock, which earns no free cast on an empty map: a player
// waiting out a respawn really does have their cooldown back when the mobs
// land. Here the burst kills the only mob, and the four seconds of empty map
// are exactly the recharge -- so the mob that respawns dies to a burst too.
TEST(CombatSimTest, ACooldownRunsDownOnAnEmptyMap) {
  Mob snail = MakeMob("Snail", 30);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 4.0, {MakeType(&snail, 10.0, 1)});
  params.attacks[0].name = "Attack";
  params.attacks.push_back(MakeBurst());

  sim.Advance(params, 1.0);
  ASSERT_EQ(sim.kills_this_step()[0], 1);  // the burst lands and kills
  sim.Advance(params, 1.0);
  sim.Advance(params, 1.0);
  ASSERT_TRUE(sim.respawning());  // three seconds with nothing to hit
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.kills_this_step()[0], 1)
      << "the respawned mob survived, so the burst was still recharging";
}

// The pair of skills a wizard alternates between: each one out of reach for a
// moment after it lands, so the other one is what there is.
CombatParams MakeAlternatingParams(const Mob& snail) {
  CombatParams params = MakeParams(1.0, 100.0, {MakeType(&snail, 1.0, 1)});
  params.attacks[0].name = "Attack";
  params.attacks.push_back(MakeSkill("Ice", 10.0, 0.5));
  params.attacks.push_back(MakeSkill("Bolt", 8.0, 0.5));
  return params;
}

// The commitment is what makes a short cooldown alternate a pair of skills:
// without it the harder one comes back mid-animation and simply takes the
// swing, and the other is bought and never seen.
TEST(CombatSimTest, ASwingUnderwayIsNotDisplacedByABetterOne) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeAlternatingParams(snail);

  for (int i = 0; i < 4; ++i) {
    sim.Advance(params, 0.25);  // Ice, the harder of the two, lands first
  }
  ASSERT_NEAR(sim.target_hp_fraction(), 0.99, 1e-9);
  ASSERT_EQ(sim.attack_name(), "Bolt");

  for (int i = 0; i < 3; ++i) {
    sim.Advance(params, 0.25);  // Ice is off cooldown half way through this
  }
  EXPECT_EQ(sim.attack_name(), "Bolt") << "Ice took a swing already underway";
  sim.Advance(params, 0.25);
  EXPECT_NEAR(sim.target_hp_fraction(), 0.982, 1e-9);  // 10 and then 8
}

TEST(CombatSimTest, TwoRechargingSkillsTakeTurns) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeAlternatingParams(snail);

  for (int i = 0; i < 4; ++i) {
    sim.Advance(params, 1.0);
  }
  // Ice, Bolt, Ice, Bolt: 36, not the 40 four Ices would be.
  EXPECT_NEAR(sim.target_hp_fraction(), 0.964, 1e-9);
}

// The poke is the exception to the commitment: it is what the character is
// left with while everything else recharges, and holding them to it would
// cost them the skill for the rest of the animation.
TEST(CombatSimTest, TheFallbackPokeYieldsAsSoonAsTheSkillIsBack) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 100.0, {MakeType(&snail, 1.0, 1)});
  params.attacks[0].name = "Attack";
  params.attacks.push_back(MakeSkill("Ice", 10.0, 0.5));

  for (int i = 0; i < 4; ++i) {
    sim.Advance(params, 0.25);
  }
  ASSERT_EQ(sim.attack_name(), "Attack") << "nothing else is up to swing";
  for (int i = 0; i < 4; ++i) {
    sim.Advance(params, 0.25);
  }
  EXPECT_NEAR(sim.target_hp_fraction(), 0.98, 1e-9);  // 10 twice, not 10 and 1
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
  wide.swing_seconds = 1.0;  // as quick as the poke, so only damage decides
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
  wide.swing_seconds = 1.0;  // as quick as the poke, so only damage decides
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

TEST(CombatSimTest, AMobSlowerThanTheBeatStillDies) {
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

TEST(CombatSimTest, ABeatMidFightKeepsTheSwingCharging) {
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

// Spirit Blade: a share of every hit comes straight back out of whoever
// landed it. Off the mob in front, since that is the one that swung.
TEST(CombatSimTest, ReflectionHurtsTheMobThatHits) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(100.0, 1000.0, {MakeType(&snail, 1.0, 1)});
  GivePlayerHp(params, 100, /*interval=*/1.0, /*damage=*/10.0);
  params.damage_reflect_pct = 5.0;

  // The swing is 100 seconds off, so every point the snail loses is reflected.
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.player_hp(), 90);
  EXPECT_DOUBLE_EQ(sim.target_hp_fraction(), 0.95);
}

TEST(CombatSimTest, ReflectionCanFinishAMob) {
  Mob snail = MakeMob("Snail", 40);
  CombatSim sim;
  CombatParams params = MakeParams(100.0, 1000.0, {MakeType(&snail, 1.0, 1)});
  GivePlayerHp(params, 100, /*interval=*/1.0, /*damage=*/10.0);
  params.damage_reflect_pct = 5.0;

  // A kill is a kill however it happened: the reward layer pays for this one
  // exactly as it pays for a swing's.
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.kills_this_step()[0], 1);
  EXPECT_TRUE(sim.respawning());
}

TEST(CombatSimTest, NoReflectionWithoutTheSkill) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(100.0, 1000.0, {MakeType(&snail, 1.0, 1)});
  GivePlayerHp(params, 100, /*interval=*/1.0, /*damage=*/10.0);

  sim.Advance(params, 1.0);
  EXPECT_DOUBLE_EQ(sim.target_hp_fraction(), 1.0);
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

// A passive that heals on attack pays per landed swing, costing none of them,
// so it stacks with the beat rather than replacing it -- and it cannot carry
// the pool past full.
TEST(CombatSimTest, RecoveryOnAttackRidesTheSwing) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 1.0, 1)});
  GivePlayerHp(params, 100, /*interval=*/1.0, /*damage=*/20.0);
  params.hp_recover_pct = 0.05;

  sim.Advance(params, 1.0);  // one hit taken, one swing landed
  EXPECT_EQ(sim.player_hp(), 85);
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.player_hp(), 70);
  for (int i = 0; i < 20; ++i) {  // swinging alone cannot overfill the pool
    sim.Advance(params, 1.0);
  }
  EXPECT_LE(sim.player_hp(), 100);
}

// Nothing to hit is nothing to heal off. A cleared map already gives the pool
// back on the beat, and a swing at empty air must not pay twice for it.
TEST(CombatSimTest, RecoveryOnAttackPaysNothingOnAnEmptyMap) {
  Mob snail = MakeMob("Snail", 10);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 10.0, 1)});
  GivePlayerHp(params, 100, /*interval=*/1.0, /*damage=*/10.0);
  params.hp_recover_pct = 0.05;

  sim.Advance(params, 0.5);
  sim.Advance(params, 0.5);  // a hit lands, then the swing clears the map
  ASSERT_TRUE(sim.respawning());
  int cleared = sim.player_hp();
  for (int i = 0; i < 6; ++i) {  // idling well short of the 1000s beat
    sim.Advance(params, 0.5);
  }
  EXPECT_EQ(sim.player_hp(), cleared);
}

TEST(CombatSimTest, TheHitClockWaitsOnAnEmptyMap) {
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

TEST(CombatSimTest, ARespawnBeatMidFightHealsASlice) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  // The mobs outlast the beat, so the top-up is more monsters arriving rather
  // than the player's breather -- and the slice comes back regardless, which
  // is what lets a map be held rather than only cleared.
  CombatParams params = MakeParams(10.0, 2.0, {MakeType(&snail, 1.0, 2)});
  GivePlayerHp(params, 100, /*interval=*/1.0, /*damage=*/30.0);
  params.beat_heal_fraction = 0.1;

  sim.Advance(params, 1.0);
  ASSERT_EQ(sim.player_hp(), 70);
  sim.Advance(params, 1.0);  // the beat lands here, with both mobs still up
  ASSERT_FALSE(sim.respawning());
  EXPECT_EQ(sim.player_hp(), 50);  // 70, +10 from the beat, -30 from the hit
}

TEST(CombatSimTest, ABeatCannotHealPastFull) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(10.0, 2.0, {MakeType(&snail, 1.0, 2)});
  GivePlayerHp(params, 100, /*interval=*/1.0, /*damage=*/5.0);
  params.beat_heal_fraction = 0.1;

  sim.Advance(params, 1.0);
  ASSERT_EQ(sim.player_hp(), 95);
  // The beat's tenth is more than the one hit took, and the surplus goes
  // nowhere: the player is left one hit down, not banking healing for later.
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.player_hp(), 95);
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

TEST(CombatSimTest, LevellingUpFillsTheWiderPool) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(10.0, 1000.0, {MakeType(&snail, 1.0, 1)});
  GivePlayerHp(params, 100, /*interval=*/1.0, /*damage=*/10.0);

  sim.Advance(params, 1.0);
  ASSERT_EQ(sim.player_hp(), 90);

  // The level lands and the pool grows. Left alone, the bar would read 90 of
  // 200 -- less than half full, having lost one hit.
  params.max_player_hp = 200;
  sim.Advance(params, 0.5);
  EXPECT_EQ(sim.player_hp(), 200);
  EXPECT_EQ(sim.player_max_hp(), 200);
}

TEST(CombatSimTest, InactiveParamsShowNoPlayerHp) {
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

// --- skills that fire on their own clock ---

TEST(CombatSimTest, AnAutoAttackFiresOnItsOwnClock) {
  Mob snail = MakeMob("Snail", 100);
  CombatSim sim;
  // A swing far too slow to interfere, so what lands is the cast alone.
  CombatParams params = MakeParams(1000.0, 1000.0, {MakeType(&snail, 0.0, 1)});
  AddAutoAttack(params, /*interval=*/2.0, /*damage=*/25.0);

  sim.Advance(params, 1.0);  // mid-interval
  EXPECT_NEAR(sim.target_hp_fraction(), 1.0, 1e-9);
  sim.Advance(params, 1.0);  // the interval closes
  EXPECT_NEAR(sim.target_hp_fraction(), 0.75, 1e-9);
  sim.Advance(params, 2.0);
  EXPECT_NEAR(sim.target_hp_fraction(), 0.50, 1e-9);
}

TEST(CombatSimTest, AnAutoAttackKillsAreRewardedLikeAnySwing) {
  Mob snail = MakeMob("Snail", 10);
  CombatSim sim;
  CombatParams params = MakeParams(1000.0, 1000.0, {MakeType(&snail, 0.0, 1)});
  AddAutoAttack(params, /*interval=*/1.0, /*damage=*/50.0);

  sim.Advance(params, 1.0);
  ASSERT_EQ(sim.kills_this_step().size(), 1u);
  EXPECT_EQ(sim.kills_this_step()[0], 1);
}

TEST(CombatSimTest, AnAutoAttackReachesWhatItsSkillSays) {
  Mob snail = MakeMob("Snail", 100);
  CombatSim sim;
  CombatParams params = MakeParams(1000.0, 1000.0, {MakeType(&snail, 0.0, 5)});
  AddAutoAttack(params, /*interval=*/1.0, /*damage=*/100.0, /*reach=*/3);

  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.kills_this_step()[0], 3);  // three of the five, not one
}

TEST(CombatSimTest, ATriggeredAttackFiresOnTheFourthSwing) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  // A swing that does nothing, so what lands on the mob is the volley alone.
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 0.0, 1)});
  AddTriggeredAttack(params, /*attacks=*/4, /*damage=*/100.0);

  for (int i = 0; i < 3; ++i) {
    sim.Advance(params, 1.0);
    EXPECT_NEAR(sim.target_hp_fraction(), 1.0, 1e-9) << "swing " << i + 1;
  }
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.target_hp_fraction(), 0.90, 1e-9);
  // And again four swings later, not every swing from here on. Stepped one
  // swing at a time: a single long Advance is clamped to one of them.
  for (int i = 0; i < 3; ++i) {
    sim.Advance(params, 1.0);
    EXPECT_NEAR(sim.target_hp_fraction(), 0.90, 1e-9) << "swing " << i + 5;
  }
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.target_hp_fraction(), 0.80, 1e-9);
}

// The whole reason the weight exists: a swing landing seven times as often is
// worth a seventh, so the volley comes at the same rate either way.
TEST(CombatSimTest, ARapidSwingTakesSevenTimesAsManyToFireIt) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 0.0, 1)});
  params.attacks[0].count_weight = 1.0 / 7.0;
  AddTriggeredAttack(params, /*attacks=*/4, /*damage=*/100.0);

  // 27 swings is a hair under the 4 attacks it takes.
  for (int i = 0; i < 27; ++i) {
    sim.Advance(params, 1.0);
  }
  EXPECT_NEAR(sim.target_hp_fraction(), 1.0, 1e-9);
  // The 28th closes it. A seventh cannot be written exactly, so this is also
  // the assertion that the counter is nudged rather than compared bare.
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.target_hp_fraction(), 0.90, 1e-9);
}

// The remainder carries rather than resetting, or a swing worth a fraction
// would throw the rest away and never come round at all.
TEST(CombatSimTest, TheSwingCountCarriesItsRemainder) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 0.0, 1)});
  params.attacks[0].count_weight = 3.0;
  AddTriggeredAttack(params, /*attacks=*/2, /*damage=*/100.0);

  // Worth three where two are needed: it fires, and the spare one is still
  // there to be half of the next.
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.target_hp_fraction(), 0.90, 1e-9);
  sim.Advance(params, 1.0);  // 1 carried + 3 = 4, so two more casts
  EXPECT_NEAR(sim.target_hp_fraction(), 0.70, 1e-9);
}

TEST(CombatSimTest, ATriggeredAttackReachesWhatItsSkillSays) {
  Mob snail = MakeMob("Snail", 10);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 0.0, 6)});
  AddTriggeredAttack(params, /*attacks=*/1, /*damage=*/100.0, /*reach=*/6);

  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.kills_this_step()[0], 6);
}

// A healing cast is not an attack, so it credits nothing -- a character
// spending swings staying alive is not also building a volley.
TEST(CombatSimTest, AHealingCastCreditsNothing) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 0.0, 1)});
  GivePlayerHp(params, 100, /*interval=*/1.0, /*damage=*/10.0);
  AttackOption heal;
  heal.name = "Heal";
  heal.swing_seconds = 1.0;
  heal.heal_fraction = 0.05;
  heal.damage_per_hit.assign(params.types.size(), 0.0);
  params.attacks.push_back(std::move(heal));
  AddTriggeredAttack(params, /*attacks=*/4, /*damage=*/100.0);

  // Beaten below a quarter of the pool, at which point every swing goes on the
  // cast -- and the mob's HP never moves however many of them are spent.
  for (int i = 0; i < 40; ++i) {
    sim.Advance(params, 1.0);
  }
  ASSERT_LT(sim.player_hp_fraction(), 0.25);
  double before = sim.target_hp_fraction();
  for (int i = 0; i < 20; ++i) {
    sim.Advance(params, 1.0);
  }
  EXPECT_NEAR(sim.target_hp_fraction(), before, 1e-9);
}

// The swing is chosen after the casts land, so a skill that thins the map out
// changes what the character reaches for next.
TEST(CombatSimTest, TheSwingIsPickedAfterTheCasts) {
  Mob snail = MakeMob("Snail", 10);
  CombatSim sim;
  CombatParams params = MakeParams(1000.0, 1000.0, {MakeType(&snail, 0.0, 2)});
  AddAutoAttack(params, /*interval=*/1.0, /*damage=*/100.0, /*reach=*/2);

  sim.Advance(params, 1.0);
  EXPECT_TRUE(sim.respawning());  // the cast emptied the map
  EXPECT_TRUE(sim.attack_name().empty());
}

TEST(CombatSimTest, AnAutoAttackClockWaitsWhileTheMapIsEmpty) {
  Mob snail = MakeMob("Snail", 10);
  CombatSim sim;
  // A swing slow enough never to land, so the casts are the only thing that
  // kills and the timing is entirely theirs.
  CombatParams params = MakeParams(1000.0, 5.0, {MakeType(&snail, 0.0, 1)});
  AddAutoAttack(params, /*interval=*/3.0, /*damage=*/50.0);

  for (int i = 0; i < 3; ++i) {
    sim.Advance(params, 1.0);
  }
  ASSERT_EQ(sim.kills_this_step()[0], 1);  // t=3: the cast lands
  ASSERT_TRUE(sim.respawning());

  // t=4 is idle and buys the summon nothing. The beat at t=5 refills the map
  // and the clock runs again from there, so the next cast is due at t=7. Had
  // the idle second counted toward it, it would have come at t=6.
  for (int i = 0; i < 3; ++i) {
    sim.Advance(params, 1.0);
  }
  EXPECT_EQ(sim.kills_this_step()[0], 0);  // t=6
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.kills_this_step()[0], 1);  // t=7
}

TEST(CombatSimTest, AnAutoAttackWithNoIntervalNeverFires) {
  Mob snail = MakeMob("Snail", 100);
  CombatSim sim;
  CombatParams params = MakeParams(1000.0, 1000.0, {MakeType(&snail, 0.0, 1)});
  AddAutoAttack(params, /*interval=*/0.0, /*damage=*/100.0);

  sim.Advance(params, 100.0);
  EXPECT_NEAR(sim.target_hp_fraction(), 1.0, 1e-9);
}

TEST(CombatSimTest, AnAutoAttackIsNeverChosenAsTheSwing) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 1.0, 1)});
  // A cast that hits vastly harder than the swing still does not become it:
  // the charge bar names what the character is swinging, not what a summon
  // is about to do.
  AddAutoAttack(params, /*interval=*/1.0, /*damage=*/500.0);

  sim.Advance(params, 0.5);
  EXPECT_EQ(sim.attack_name(), "Attack");
}

// --- Final Attack ---

// Gives the swing a Final Attack worth `damage` against each enemy it reaches.
void AddFinalAttack(CombatParams& params, double damage) {
  params.attacks[0].final_attack_damage.assign(params.types.size(), damage);
}

TEST(CombatSimTest, FinalAttackAddsToTheSwing) {
  Mob snail = MakeMob("Snail", 100);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 10.0, 1)});
  AddFinalAttack(params, /*damage=*/15.0);

  sim.Advance(params, 1.0);
  // 10 from the swing and 15 following it, on the one mob in front.
  EXPECT_NEAR(sim.target_hp_fraction(), 0.75, 1e-9);
}

// A Final Attack rolls separately against every enemy the swing reached, so a
// wide swing sets it off as many times as it had targets.
TEST(CombatSimTest, FinalAttackFollowsTheSwingOntoEveryEnemy) {
  Mob snail = MakeMob("Snail", 100);
  CombatSim sim;
  CombatParams params =
      MakeParams(1.0, 1000.0, {MakeType(&snail, 10.0, 3)}, /*reach=*/3);
  AddFinalAttack(params, /*damage=*/40.0);

  sim.Advance(params, 1.0);
  // All three took the swing's 10 and the 40 following it.
  const std::vector<EngagedGroup>& groups = sim.engaged_groups();
  ASSERT_EQ(groups.size(), 1u);
  EXPECT_EQ(groups[0].count, 3);
  EXPECT_NEAR(groups[0].hp_fraction, 0.5, 1e-9);
}

// A swing that outruns the queue sets it off once per mob actually there, not
// once per target it could have reached.
TEST(CombatSimTest, FinalAttackStopsWithTheSwing) {
  Mob snail = MakeMob("Snail", 100);
  CombatSim sim;
  CombatParams params =
      MakeParams(1.0, 1000.0, {MakeType(&snail, 10.0, 2)}, /*reach=*/6);
  AddFinalAttack(params, /*damage=*/40.0);

  sim.Advance(params, 1.0);
  const std::vector<EngagedGroup>& groups = sim.engaged_groups();
  ASSERT_EQ(groups.size(), 1u);
  EXPECT_EQ(groups[0].count, 2);
  EXPECT_NEAR(groups[0].hp_fraction, 0.5, 1e-9);
}

TEST(CombatSimTest, FinalAttackCanBeWhatKillsTheFrontMob) {
  Mob snail = MakeMob("Snail", 100);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 10.0, 2)});
  AddFinalAttack(params, /*damage=*/95.0);

  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.kills_this_step()[0], 1);
}

// A summon is not the character swinging, so nothing follows it.
TEST(CombatSimTest, ACastOnItsOwnClockSetsOffNoFinalAttack) {
  Mob snail = MakeMob("Snail", 100);
  CombatSim sim;
  CombatParams params = MakeParams(1000.0, 1000.0, {MakeType(&snail, 0.0, 1)});
  AddFinalAttack(params, /*damage=*/50.0);
  AddAutoAttack(params, /*interval=*/1.0, /*damage=*/10.0);

  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.target_hp_fraction(), 0.90, 1e-9);  // the cast's 10, alone
}

TEST(CombatSimTest, NoFinalAttackLeavesTheSwingAsItIs) {
  Mob snail = MakeMob("Snail", 100);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 10.0, 1)});

  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.target_hp_fraction(), 0.90, 1e-9);
}

// Gives the swing an opening hit worth `damage` on one enemy.
void AddLead(CombatParams& params, double damage) {
  params.attacks[0].lead_damage.assign(params.types.size(), damage);
}

// The opening hit lands once, on the healthiest of the mobs the swing reached
// -- not on all of them, and not on the front one.
TEST(CombatSimTest, TheOpeningHitPicksTheHealthiestMobItReached) {
  Mob snail = MakeMob("Snail", 100);
  Mob boar = MakeMob("Boar", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(
      1.0, 1000.0, {MakeType(&snail, 10.0, 2), MakeType(&boar, 10.0, 1)},
      /*reach=*/3);
  AddLead(params, /*damage=*/100.0);

  sim.Advance(params, 1.0);
  // All three took the spread's 10. Only the boar, on 1000 against the snails'
  // 100, also took the opening 100.
  const EngagedGroup* snails = FindGroup(sim.engaged_groups(), "Snail");
  const EngagedGroup* boars = FindGroup(sim.engaged_groups(), "Boar");
  ASSERT_NE(snails, nullptr);
  ASSERT_NE(boars, nullptr);
  EXPECT_NEAR(snails->hp_fraction, 0.90, 1e-9);
  EXPECT_NEAR(boars->hp_fraction, 0.89, 1e-9);
}

// A swing worth more than its spread alone has to be ranked on the whole of
// it, or the fight reaches for the wrong one.
TEST(CombatSimTest, TheOpeningHitCountsTowardChoosingTheSwing) {
  Mob snail = MakeMob("Snail", 100000);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 5.0, 1)});
  AddLead(params, /*damage=*/50.0);
  AttackOption plain = MakeSkill("Plain", /*damage=*/20.0, /*cooldown=*/0.0);
  params.attacks.push_back(std::move(plain));

  // The poke spreads for 5 where Plain lands 20, but its opening hit is worth
  // 50 on top -- 55 against 20, so it is what gets swung.
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.attack_name(), "Attack");
  EXPECT_NEAR(sim.target_hp_fraction(), 1.0 - 55.0 / 100000.0, 1e-9);
}

// Puts a healing cast beside the swing, worth `fraction` of the pool. It
// carries damage the fight must never land: what makes a cast harmless is the
// fight declining to strike with it, not the encounter having zeroed it.
void AddHeal(CombatParams& params, double fraction, double swing = 1.0) {
  AttackOption heal;
  heal.name = "Heal";
  heal.max_enemies = 1;
  heal.damage_per_hit.assign(params.types.size(), 1000.0);
  heal.swing_seconds = swing;
  heal.heal_fraction = fraction;
  params.attacks.push_back(std::move(heal));
}

// The whole of the cast's rule on one setup: ignored while the player is
// comfortable, taken the moment they fall under a quarter, and landing the
// pool share it is worth instead of any damage at all.
TEST(CombatSimTest, AHealingCastIsSpentOnlyOnceThePlayerIsLow) {
  Mob snail = MakeMob("Snail", 100000);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 10.0, 1)});
  GivePlayerHp(params, /*max_hp=*/100, /*interval=*/1.0, /*damage=*/10.0);
  AddHeal(params, /*fraction=*/0.5);

  // Seven seconds of being hit leaves them on 30, still above the quarter, so
  // every swing so far has been the attack: seven of them, 70 damage.
  for (int i = 0; i < 7; ++i) {
    sim.Advance(params, 1.0);
  }
  ASSERT_EQ(sim.player_hp(), 30);
  EXPECT_NEAR(sim.target_hp_fraction(), 1.0 - 70.0 / 100000.0, 1e-9);
  EXPECT_EQ(sim.attack_name(), "Attack");

  // The eighth hit takes them to 20, and that swing goes on the cast instead:
  // half the pool back, and the mob left on the seven hits it has taken.
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.player_hp(), 70);
  EXPECT_NEAR(sim.target_hp_fraction(), 1.0 - 70.0 / 100000.0, 1e-9);
}

// The cast replaces the NEXT swing, not the one already on its way: a skill
// winding up is committed to, and being in trouble is no exception to that.
TEST(CombatSimTest, AHealingCastWaitsForTheSwingAlreadyWindingUp) {
  Mob snail = MakeMob("Snail", 100000);
  CombatSim sim;
  // The poke does nothing, so the four-second skill is what gets chosen -- and
  // unlike the poke, a skill is committed to once it is winding up.
  CombatParams params =
      MakeParams(4.0, 1000.0, {MakeType(&snail, 0.0, 3)}, /*reach=*/3);
  AttackOption skill = MakeSkill("Skill", /*damage=*/10.0, /*cooldown=*/0.0);
  skill.swing_seconds = 4.0;
  skill.max_enemies = 3;
  params.attacks.push_back(std::move(skill));
  AddHeal(params, /*fraction=*/0.5, /*swing=*/4.0);
  GivePlayerHp(params, /*max_hp=*/1000, /*interval=*/3.0, /*damage=*/800.0);

  // One hit lands on the third second and takes them to a fifth of the pool,
  // with the skill three seconds into its four.
  for (int i = 0; i < 3; ++i) {
    sim.Advance(params, 1.0);
  }
  ASSERT_EQ(sim.player_hp(), 200);
  EXPECT_EQ(sim.attack_name(), "Skill");
  EXPECT_DOUBLE_EQ(sim.target_hp_fraction(), 1.0);

  // The fourth second finishes it: the skill lands rather than being dropped,
  // and only then is the cast lined up.
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.target_hp_fraction(), 1.0 - 10.0 / 100000.0, 1e-9);
  EXPECT_EQ(sim.attack_name(), "Heal");
  // The cast reaches nobody, but the window it is charging in front of is
  // still the last swing's -- the mob bars must not collapse behind it.
  ASSERT_EQ(sim.engaged_groups().size(), 1u);
  EXPECT_EQ(sim.engaged_groups().front().count, 3);
}

// The pool is the ceiling: an overheal is wasted rather than banked.
TEST(CombatSimTest, AHealingCastStopsAtAFullPool) {
  Mob snail = MakeMob("Snail", 100000);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 10.0, 1)});
  GivePlayerHp(params, /*max_hp=*/100, /*interval=*/1.0, /*damage=*/45.0);
  AddHeal(params, /*fraction=*/5.0);

  sim.Advance(params, 1.0);
  ASSERT_EQ(sim.player_hp(), 55);
  sim.Advance(params, 1.0);  // down to 10, then five pools' worth of healing
  EXPECT_EQ(sim.player_hp(), 100);
}

// A cleared map hands HP back on the beat for free, so a swing spent healing
// there would buy nothing. Reachable because a skill on its own clock can take
// the last mob before the swing is ever aimed.
TEST(CombatSimTest, AHealingCastIsNotSpentOnAnEmptyMap) {
  Mob snail = MakeMob("Snail", 10);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 0.0, 1)});
  AddAutoAttack(params, /*interval=*/1.0, /*damage=*/10.0);
  GivePlayerHp(params, /*max_hp=*/100, /*interval=*/1.0, /*damage=*/80.0);
  AddHeal(params, /*fraction=*/0.5);

  // The hit takes them under the quarter, and the cast clears the map before
  // the swing is chosen.
  sim.Advance(params, 1.0);
  ASSERT_TRUE(sim.respawning());
  EXPECT_EQ(sim.player_hp(), 20);
  EXPECT_EQ(sim.attack_name(), "");
}

// Empowered Arrows: the Sniper's Piercing Arrow is upgraded every fourth shot.
// Three ordinary swings, then the bigger one -- not the bigger one first.
TEST(CombatSimTest, LandsAnEmpoweredSwingOnceEveryNth) {
  Mob snail = MakeMob("Snail", 20);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 1.0, 1)});
  SetEmpoweredForm(params.attacks[0], /*every=*/4, /*damage=*/10.0);

  for (int i = 0; i < 3; ++i) {
    sim.Advance(params, 1.0);
  }
  EXPECT_NEAR(sim.target_hp_fraction(), 0.85, 1e-9);
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.target_hp_fraction(), 0.35, 1e-9)
      << "the fourth swing lands the empowered form";
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.target_hp_fraction(), 0.30, 1e-9)
      << "and the count starts again from the fifth";
}

// It takes the PLACE of the swing rather than riding on top of it: four swings
// are three ordinary ones and one empowered, never four and a fifth.
TEST(CombatSimTest, AnEmpoweredSwingReplacesTheOneItLandsFor) {
  Mob snail = MakeMob("Snail", 20);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 1.0, 1)});
  SetEmpoweredForm(params.attacks[0], /*every=*/4, /*damage=*/5.0);

  for (int i = 0; i < 4; ++i) {
    sim.Advance(params, 1.0);
  }
  // 1 + 1 + 1 + 5 of 20. Adding instead of replacing would leave 0.45.
  EXPECT_NEAR(sim.target_hp_fraction(), 0.60, 1e-9);
}

// The empowered form carries its own reach, which is wider than the swing it
// stands in for: on its turn it takes mobs the ordinary swing never touches.
TEST(CombatSimTest, AnEmpoweredSwingBringsItsOwnReach) {
  Mob snail = MakeMob("Snail", 10);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 1.0, 3)});
  SetEmpoweredForm(params.attacks[0], /*every=*/2, /*damage=*/10.0,
                   /*reach=*/3);

  sim.Advance(params, 1.0);  // ordinary: one mob, no kill
  EXPECT_EQ(sim.kills_this_step()[0], 0);
  sim.Advance(params, 1.0);  // empowered: all three at once
  EXPECT_EQ(sim.kills_this_step()[0], 3);
}

// The choice between swings goes on damage per second, so an attack that lands
// a much bigger form every few swings has to be weighed on the average of the
// two. Weighed on its ordinary swing alone, this one loses to the poke.
TEST(CombatSimTest, WeighsAnEmpoweredSwingIntoTheChoice) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 3.0, 1)});
  params.attacks.push_back(MakeSkill("Piercing Arrow", 2.0, /*cooldown=*/0.0));
  SetEmpoweredForm(params.attacks[1], /*every=*/4, /*damage=*/10.0);

  // 2 + (10 - 2) / 4 = 4 a swing, against the poke's 3.
  sim.Advance(params, 0.1);
  EXPECT_EQ(sim.attack_name(), "Piercing Arrow");
}

}  // namespace
}  // namespace ms
