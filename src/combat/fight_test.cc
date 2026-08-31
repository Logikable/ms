#include "src/combat/fight.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <set>
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
  params.encounter = map;
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

// A swing that rolls: every line lands somewhere between the mastery floor and
// full, and crits on top of that. Wide open or barely at all, it has to kill
// at the rate the unrolled swing did -- the only thing rolling costs is
// overkill on the killing blow, and against a mob eighty swings deep that is a
// fraction of one kill.
TEST(CombatSimTest, ARollingSwingKillsAtTheRateItsAverageWould) {
  // A deep roster and a respawn that never comes: what limits the kills has to
  // be the damage, or every run simply empties the map and agrees about
  // nothing.
  // The HP is many swings deep and no multiple of one: a mob that dies on an
  // exact swing count would lose a whole swing to the smallest jitter, which
  // is granularity rather than anything the roll did.
  Mob mob = MakeMob("Snail", 2013);
  double kills[3] = {0.0, 0.0, 0.0};
  for (int run = 0; run < 3; ++run) {
    CombatParams params = MakeParams(1.0, 1e9, {MakeType(&mob, 25.0, 400)});
    if (run > 0) {
      // Four lines of a quarter of the damage apiece: the same 25 on average
      // either way. Run 1 rolls wide open and crit-heavy, run 2 barely at all.
      HitGroup group;
      group.damage = {25.0};
      group.rolls.lines = 4;
      group.rolls.mastery = run == 1 ? 0.4 : 0.99;
      group.rolls.crit_rate = run == 1 ? 0.5 : 0.0;
      group.rolls.crit_dmg = 1.0;
      params.attacks[0].groups.push_back(group);
    }
    CombatSim sim;
    for (int step = 0; step < 20000; ++step) {
      sim.Advance(params, 1.0);
      kills[run] += sim.kills_this_step()[0];
    }
  }
  ASSERT_GT(kills[0], 0.0);
  EXPECT_NEAR(kills[1] / kills[0], 1.0, 0.01);
  EXPECT_NEAR(kills[2] / kills[0], 1.0, 0.01);
}

// The same swing landing differently twice, which is the whole point: without
// this the roll could be a constant and every average above would still hold.
TEST(CombatSimTest, ARollingSwingDoesNotLandTheSameTwice) {
  Mob mob = MakeMob("Snail", 1000000);
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&mob, 25.0, 1)});
  HitGroup group;
  group.damage = {25.0};
  group.rolls.lines = 4;
  group.rolls.mastery = 0.4;
  group.rolls.crit_rate = 0.5;
  group.rolls.crit_dmg = 1.0;
  params.attacks[0].groups.push_back(group);

  CombatSim sim;
  std::vector<double> left;
  for (int step = 0; step < 5; ++step) {
    sim.Advance(params, 1.0);
    left.push_back(sim.target_hp_fraction());
  }
  // Five swings, so four gaps -- at least one of them differs from the first.
  bool varied = false;
  for (std::size_t i = 2; i < left.size(); ++i) {
    if (std::abs((left[i - 1] - left[i]) - (left[0] - left[1])) > 1e-9) {
      varied = true;
    }
  }
  EXPECT_TRUE(varied);
}

// A Final Attack is a chance, not a fraction of a hit. It rolls once per enemy
// the swing reached, and over a long run it has to pay what the fraction did.
TEST(CombatSimTest, AFinalAttackRollsPerEnemyAndPaysItsAverage) {
  Mob mob = MakeMob("Snail", 2013);
  double kills[2] = {0.0, 0.0};
  for (int run = 0; run < 2; ++run) {
    CombatParams params = MakeParams(1.0, 1e9, {MakeType(&mob, 20.0, 400)});
    params.attacks[0].final_attack_damage = {5.0};
    if (run == 1) {
      // A quarter chance of a hit worth four times as much: the same 5 on
      // average, and mostly nothing at all.
      FinalAttackRoll roll;
      roll.chance = 0.25;
      roll.damage = {20.0};
      params.attacks[0].final_attack_rolls.push_back(roll);
    }
    CombatSim sim;
    for (int step = 0; step < 20000; ++step) {
      sim.Advance(params, 1.0);
      kills[run] += sim.kills_this_step()[0];
    }
  }
  ASSERT_GT(kills[0], 0.0);
  EXPECT_NEAR(kills[1] / kills[0], 1.0, 0.01);
}

// The burn a hand-built swing leaves: the one slot such a swing ever needs,
// what a tick of it costs one mob type, and the clock it burns on.
DotApplication MakeBurn(double damage, double interval, double duration) {
  DotApplication burn;
  burn.slot = 0;
  burn.damage.assign(1, damage);
  burn.interval_seconds = interval;
  burn.duration_seconds = duration;
  return burn;
}

// A burn is worth what it can sustain, not what one lighting of it comes to:
// a swing that relights it every second buys one tick a second however long
// the burn would have lasted. Priced in full it would look thirty times its
// worth and the fight would swing it over something five times better.
TEST(CombatSimTest, ABurnIsWeighedAtTheRateItCanBeRelit) {
  Mob mob = MakeMob("Snail", 100);
  CombatParams params = MakeParams(1.0, 1e9, {MakeType(&mob, 100.0, 40)});
  // Beside the hard swing, a feeble one leaving a burn that would last half a
  // minute. Relit every second it is worth 10 a second, not 300 a swing.
  AttackOption smoulder = params.attacks[0];
  smoulder.name = "Smoulder";
  smoulder.damage_per_hit.assign(1, 10.0);
  smoulder.dots.push_back(MakeBurn(10.0, 1.0, 30.0));
  params.dot_count = 1;
  params.attacks.push_back(std::move(smoulder));

  CombatSim sim;
  int64_t killed = 0;
  for (int step = 0; step < 30; ++step) {
    sim.Advance(params, 1.0);
    killed += sim.kills_this_step()[0];
  }
  // The hard swing kills one a second; the smoulder would manage one every
  // five. Anything near the latter means the burn was priced at its whole life.
  EXPECT_GT(killed, 20);
}

// Relighting a burn on something already burning buys nothing, so the fight
// leaves it alone and swings the harder thing until the burn nears its end.
// Priced as though every lighting were the first, the feeble swing would go
// out every time and the harder one would never be swung at all.
TEST(CombatSimTest, ABurnAlreadyStandingIsNotWorthRelighting) {
  Mob mob = MakeMob("Snail", 1000000);
  CombatParams params = MakeParams(1.0, 1e9, {MakeType(&mob, 300.0, 1)});
  // A swing that deals nothing itself and leaves a burn worth 400 a tick for
  // ten seconds: worth lighting, and worth nothing at all to light again.
  AttackOption smoulder = params.attacks[0];
  smoulder.name = "Smoulder";
  smoulder.damage_per_hit.assign(1, 0.0);
  smoulder.dots.push_back(MakeBurn(400.0, 1.0, 10.0));
  params.dot_count = 1;
  params.attacks.push_back(std::move(smoulder));

  CombatSim sim;
  for (int step = 0; step < 60; ++step) {
    sim.Advance(params, 0.5);
  }
  double taken = (1.0 - sim.target_hp_fraction()) * 1000000.0;
  // Thirty seconds of burning is 12000 whichever swing goes out, so anything
  // above it is the harder swing landing in between.
  EXPECT_GT(taken, 16000.0);
}

// A pile with room for another helping is still worth topping up, so the fight
// keeps relighting until the pile is full and only then swings elsewhere.
TEST(CombatSimTest, APileWithRoomIsStillWorthTopping) {
  Mob mob = MakeMob("Snail", 1000000);
  double taken[2] = {0.0, 0.0};
  for (int run = 0; run < 2; ++run) {
    CombatParams params = MakeParams(1.0, 1e9, {MakeType(&mob, 300.0, 1)});
    AttackOption smoulder = params.attacks[0];
    smoulder.name = "Smoulder";
    smoulder.damage_per_hit.assign(1, 0.0);
    DotApplication burn = MakeBurn(400.0, 1.0, 10.0);
    burn.max_stacks = run == 0 ? 1 : 3;
    smoulder.dots.push_back(burn);
    params.dot_count = 1;
    params.attacks.push_back(std::move(smoulder));

    CombatSim sim;
    for (int step = 0; step < 60; ++step) {
      sim.Advance(params, 0.5);
    }
    taken[run] = (1.0 - sim.target_hp_fraction()) * 1000000.0;
  }
  // Three helpings burn for three times as much as one, and the fight only
  // gets them by spending swings the single-helping run had no reason to.
  EXPECT_GT(taken[1], taken[0] * 1.5);
}

// A burn afflicts as surely as the ice does: GMS names the same five
// conditions on Storm Magic and on Burning Magic, and the F/P's half of them
// is the burn. Nothing is frozen here and the gate still pays.
TEST(CombatSimTest, ABurnAfflictsTheMonsterTheIceWouldHave) {
  Mob mob = MakeMob("Snail", 1000000);
  CombatParams params = MakeParams(1.0, 1e9, {MakeType(&mob, 100.0, 1)});
  params.dot_count = 1;
  params.attacks[0].fd_when_afflicted = 0.5;

  CombatSim cold;
  cold.Advance(params, 1.0);
  EXPECT_DOUBLE_EQ(cold.damage_this_step(), 100.0);  // nothing on it yet

  params.attacks[0].dots.push_back(MakeBurn(0.0, 1.0, 10.0));
  CombatSim lit;
  lit.Advance(params, 1.0);
  EXPECT_DOUBLE_EQ(lit.damage_this_step(), 100.0);  // the swing that lights it
  lit.Advance(params, 1.0);
  EXPECT_DOUBLE_EQ(lit.damage_this_step(), 150.0);  // and every one after
}

// Elemental Drain counts the burns standing on the GROUP, not the one being
// hit: eight monsters carrying one apiece are eight. The count is capped, and
// a rate with no cap behind it buys nothing.
TEST(CombatSimTest, TheDrainCountsEveryBurningMonsterUpToItsCap) {
  Mob mob = MakeMob("Snail", 1000000);
  CombatParams params = MakeParams(1.0, 1e9, {MakeType(&mob, 100.0, 8)}, 8);
  params.dot_count = 1;
  params.attacks[0].dots.push_back(MakeBurn(0.0, 1.0, 10.0));
  params.attacks[0].fd_per_dot = 0.1;

  CombatSim uncounted;
  uncounted.Advance(params, 1.0);
  uncounted.Advance(params, 1.0);
  EXPECT_DOUBLE_EQ(uncounted.damage_this_step(), 800.0) << "no cap, no count";

  params.attacks[0].dot_count_cap = 3;
  CombatSim capped;
  capped.Advance(params, 1.0);
  EXPECT_DOUBLE_EQ(capped.damage_this_step(), 800.0);  // lights them
  capped.Advance(params, 1.0);
  // Eight alight, three of them counted: 8 x 100 x 1.3.
  EXPECT_DOUBLE_EQ(capped.damage_this_step(), 1040.0);
}

// A burn ticks on its own clock for as long as it was lit for, and then stops
// -- it is not a second attack that runs forever.
TEST(CombatSimTest, ABurnTicksForItsDurationAndNoLonger) {
  Mob mob = MakeMob("Snail", 10000);
  // A slow swing, so one cast's burn runs out well before the next lights it.
  CombatParams params = MakeParams(10.0, 1e9, {MakeType(&mob, 0.0, 1)});
  params.dot_count = 1;
  params.attacks[0].dots.push_back(MakeBurn(100.0, 1.0, 5.0));

  CombatSim sim;
  // Through the first cast at 10s and the five ticks behind it.
  for (int step = 0; step < 32; ++step) {
    sim.Advance(params, 0.5);
  }
  EXPECT_NEAR(sim.target_hp_fraction(), 0.95, 1e-9);
  // Six more seconds with the burn out and the next cast still to come.
  for (int step = 0; step < 8; ++step) {
    sim.Advance(params, 0.5);
  }
  EXPECT_NEAR(sim.target_hp_fraction(), 0.95, 1e-9)
      << "the burn kept ticking past the seconds it was lit for";
}

// Lighting a burn again refreshes it rather than adding a second one, so
// swinging twice as often buys none of it. It must also not put the tick clock
// back, or a swing faster than the interval would refresh it out of ever
// ticking.
TEST(CombatSimTest, ABurnDoesNotStackWithItself) {
  Mob mob = MakeMob("Snail", 100000);
  double left[2] = {0.0, 0.0};
  for (int run = 0; run < 2; ++run) {
    CombatParams params =
        MakeParams(run == 0 ? 1.0 : 0.5, 1e9, {MakeType(&mob, 0.0, 1)});
    params.dot_count = 1;
    params.attacks[0].dots.push_back(MakeBurn(100.0, 1.0, 5.0));
    CombatSim sim;
    for (int step = 0; step < 400; ++step) {
      sim.Advance(params, 0.1);
    }
    left[run] = sim.target_hp_fraction();
  }
  // Forty seconds of burning either way, to within the one tick the earlier
  // first swing buys the faster run.
  EXPECT_LT(left[0], 1.0);
  EXPECT_NEAR(left[0], left[1], 100.0 / 100000.0 + 1e-9);
}

// A strike the swing sets off waits out its own clock: a swing a second and a
// wait of five buys one strike in five, not one a swing. And it goes out only
// with the swing that carries it -- the other swing sets nothing off.
TEST(CombatSimTest, ASideStrikeGoesOutOnItsOwnWait) {
  Mob mob = MakeMob("Snail", 1000000);
  CombatParams params = MakeParams(1.0, 1e9, {MakeType(&mob, 0.0, 1)});
  AttackOption strike;
  strike.max_enemies = 1;
  strike.damage_per_hit.assign(1, 1000.0);
  strike.cooldown_seconds = 5.0;
  params.attacks[0].side = std::make_shared<const AttackOption>(strike);

  CombatSim sim;
  for (int step = 0; step < 100; ++step) {
    sim.Advance(params, 0.5);
  }
  // Fifty seconds of one-second swings: ten strikes at a wait of five, and the
  // swing itself deals nothing.
  double taken = (1.0 - sim.target_hp_fraction()) * 1000000.0;
  EXPECT_NEAR(taken, 10.0 * 1000.0, 1000.0);
}

// A poison piles helpings up to its limit and no further, each ticking for the
// whole damage. Three of them are worth three times one, and the swings after
// the third buy nothing but the duration.
TEST(CombatSimTest, APoisonPilesUpToItsLimit) {
  Mob mob = MakeMob("Snail", 1000000);
  double taken[2] = {0.0, 0.0};
  for (int run = 0; run < 2; ++run) {
    CombatParams params = MakeParams(1.0, 1e9, {MakeType(&mob, 0.0, 1)});
    params.dot_count = 1;
    DotApplication burn = MakeBurn(100.0, 1.0, 5.0);
    burn.max_stacks = run == 0 ? 1 : 3;
    params.attacks[0].dots.push_back(burn);
    CombatSim sim;
    for (int step = 0; step < 200; ++step) {
      sim.Advance(params, 0.5);
    }
    taken[run] = 1.0 - sim.target_hp_fraction();
  }
  // A swing a second saturates the pile in three, so all but the first two
  // seconds of a hundred tick three times over.
  EXPECT_GT(taken[0], 0.0);
  EXPECT_NEAR(taken[1] / taken[0], 3.0, 0.05);
}

// A poison is rolled for on each enemy the swing reached, so half of them
// burn. What is asked of the roll is only that it thins the burn -- a poison
// that always took hold would be the burn above.
TEST(CombatSimTest, APoisonIsRolledForPerEnemy) {
  Mob mob = MakeMob("Snail", 1000000);
  CombatParams params = MakeParams(1.0, 1e9, {MakeType(&mob, 0.0, 8)}, 8);
  params.dot_count = 1;
  DotApplication burn = MakeBurn(100.0, 1.0, 1.0);
  burn.chance = 0.5;
  params.attacks[0].dots.push_back(burn);

  CombatSim sim;
  for (int step = 0; step < 400; ++step) {
    sim.Advance(params, 0.5);
  }
  // One tick per swing per enemy at certainty; half of them at half a chance.
  // Loose bounds: what is being caught is a roll that never fires or never
  // misses, not the shape of the distribution.
  double burned = (1.0 - sim.target_hp_fraction()) * 1000000.0;
  EXPECT_GT(burned, 100.0 * 200.0 * 0.3);
  EXPECT_LT(burned, 100.0 * 200.0 * 0.7);
}

// The burn takes its damage from the character as they stood when it was lit,
// and keeps it: a buff that drops halfway through does not thin what is
// already burning.
TEST(CombatSimTest, ABurnKeepsTheDamageItWasLitWith) {
  Mob mob = MakeMob("Snail", 10000);
  CombatParams params = MakeParams(10.0, 1e9, {MakeType(&mob, 0.0, 1)});
  params.dot_count = 1;
  params.attacks[0].dots.push_back(MakeBurn(100.0, 1.0, 5.0));

  CombatSim sim;
  for (int step = 0; step < 23; ++step) {
    sim.Advance(params, 0.5);  // the cast at 10s, and one tick after it
  }
  ASSERT_NEAR(sim.target_hp_fraction(), 0.99, 1e-9);
  // Whatever the character is worth now, the four ticks still owed were
  // priced when the burn landed.
  params.attacks[0].dots[0].damage.assign(1, 1.0);
  for (int step = 0; step < 10; ++step) {
    sim.Advance(params, 0.5);
  }
  EXPECT_NEAR(sim.target_hp_fraction(), 0.95, 1e-9);
}

// Blizzard's passive falls on one enemy however many the swing reached, so
// four times the reach is not four times the Final Attack. The ordinary bank
// beside it scales with the reach, which is what tells the two apart.
TEST(CombatSimTest, ASingleEnemyFinalAttackDoesNotScaleWithTheReach) {
  // One hit of the Final Attack kills outright, so kills count the hits.
  Mob mob = MakeMob("Snail", 100);
  int64_t killed[2][2] = {{0, 0}, {0, 0}};
  for (int single = 0; single < 2; ++single) {
    for (int reach = 1; reach <= 4; reach += 3) {
      CombatParams params =
          MakeParams(1.0, 1e9, {MakeType(&mob, 0.0, 40)}, reach);
      std::vector<double>& bank =
          single == 1 ? params.attacks[0].single_final_attack_damage
                      : params.attacks[0].final_attack_damage;
      bank.assign(params.types.size(), 100.0);
      CombatSim sim;
      for (int step = 0; step < 5; ++step) {
        sim.Advance(params, 1.0);
        killed[single][reach == 1 ? 0 : 1] += sim.kills_this_step()[0];
      }
    }
  }
  ASSERT_GT(killed[0][0], 0);
  // The ordinary bank pays per enemy reached; the single-enemy one does not.
  EXPECT_EQ(killed[0][1], 4 * killed[0][0]);
  EXPECT_EQ(killed[1][1], killed[1][0]);
}

// An arrow that gains as it travels: the enemy it reaches first takes the
// plain damage, and each one after takes 15% more than the last, compounding.
// Six of them and the last takes 1.15^5 -- twice the first, which is the
// doubling GMS names beside the 15%.
TEST(CombatSimTest, APiercingSwingCompoundsAsItGoes) {
  std::vector<Mob> mobs;
  for (int i = 0; i < 6; ++i) {
    mobs.push_back(MakeMob("M" + std::to_string(i), 10000));
  }
  std::vector<TypeSpec> specs;
  for (const Mob& mob : mobs) {
    specs.push_back(MakeType(&mob, 100.0, 1));
  }
  CombatParams params = MakeParams(1.0, 1e9, specs, /*reach=*/6);
  params.attacks[0].pierce_gain_pct = 0.15;

  CombatSim sim;
  sim.Advance(params, 1.0);
  ASSERT_EQ(sim.engaged_groups().size(), 6u);
  std::vector<double> lost;
  for (const EngagedGroup& group : sim.engaged_groups()) {
    lost.push_back((1.0 - group.hp_fraction) * 10000.0);
  }
  std::sort(lost.begin(), lost.end());
  for (int step = 0; step < 6; ++step) {
    EXPECT_NEAR(lost[step], 100.0 * std::pow(1.15, step), 1e-6)
        << "step " << step;
  }
}

// The rate a swing is chosen on carries the escalation too, averaged over the
// mobs it would reach. Six of them make the piercing swing worth 8.75 hits
// rather than 6, which is what wins it the pick over a flatter, harder one.
TEST(CombatSimTest, APiercingSwingIsChosenForWhatItsGainIsWorth) {
  std::vector<Mob> mobs;
  for (int i = 0; i < 6; ++i) {
    mobs.push_back(MakeMob("M" + std::to_string(i), 1000000));
  }
  std::vector<TypeSpec> specs;
  for (const Mob& mob : mobs) {
    specs.push_back(MakeType(&mob, 100.0, 1));
  }
  CombatParams params = MakeParams(1.0, 1e9, specs, /*reach=*/6);
  params.attacks[0].name = "Piercing Arrow";
  params.attacks[0].pierce_gain_pct = 0.15;
  // Harder on every mob it reaches and gaining nothing: 840 a swing against
  // the arrow's 875, so it wins only if the gain is left out of the reckoning.
  AttackOption flat;
  flat.name = "Bolt Burst";
  flat.max_enemies = 6;
  flat.swing_seconds = 1.0;
  flat.damage_per_hit.assign(6, 140.0);
  params.attacks.push_back(std::move(flat));

  CombatSim sim;
  sim.Advance(params, 0.1);
  EXPECT_EQ(sim.attack_name(), "Piercing Arrow");

  // Take the gain away and the flatter swing wins, which is what says the
  // pick above was made on the gain rather than on anything else.
  params.attacks[0].pierce_gain_pct = 0.0;
  CombatSim without;
  without.Advance(params, 0.1);
  EXPECT_EQ(without.attack_name(), "Bolt Burst");
}

// Which enemy the arrow meets first is drawn fresh, so the gain does not
// always fall on the same end of the queue. Without this the front mob would
// take the plain hit every swing of the fight.
TEST(CombatSimTest, APiercingSwingDrawsTheOrderItTravelsIn) {
  std::vector<Mob> mobs;
  for (int i = 0; i < 4; ++i) {
    mobs.push_back(MakeMob("M" + std::to_string(i), 1000000));
  }
  std::vector<TypeSpec> specs;
  for (const Mob& mob : mobs) {
    specs.push_back(MakeType(&mob, 100.0, 1));
  }
  CombatParams params = MakeParams(1.0, 1e9, specs, /*reach=*/4);
  params.attacks[0].pierce_gain_pct = 0.15;

  CombatSim sim;
  std::vector<double> first;
  for (int swing = 0; swing < 20; ++swing) {
    sim.Advance(params, 1.0);
    first.push_back(sim.engaged_groups()[0].hp_fraction);
  }
  // Twenty swings, so nineteen gaps: the mob at the front cannot have taken
  // the same share of the swing every time.
  bool varied = false;
  for (std::size_t i = 2; i < first.size(); ++i) {
    if (std::abs((first[i - 1] - first[i]) - (first[0] - first[1])) > 1e-12) {
      varied = true;
    }
  }
  EXPECT_TRUE(varied);
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
// of it, hitting `reach` mobs for `damage` apiece. With `marks` the count runs
// against each mob struck instead, and the form lands on top of the strike
// that set it off rather than in place of the swing.
void SetEmpoweredForm(AttackOption& attack, int every, double damage,
                      int reach = 1, bool marks = false) {
  std::shared_ptr<AttackOption> form = std::make_shared<AttackOption>();
  form->name = "Empowered " + attack.name;
  form->max_enemies = reach;
  form->swing_seconds = attack.swing_seconds;
  form->damage_per_hit.assign(attack.damage_per_hit.size(), damage);
  attack.empowered_every = every;
  attack.brands_enemies = marks;
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

// A step wider than the swing lands every swing it covered, rather than one
// and a growing residue. The residue used to pin the charge bar full: a 120ms
// key-down skill under the TUI's 150ms frame gave back 120ms of the 150 it
// took, so the phase climbed past a whole swing and stayed there.
TEST(CombatSimTest, AStepWiderThanTheSwingLandsEverySwingItCovers) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(0.6, 100.0, {MakeType(&snail, 1.0, 1)});
  // A 120ms key-down skill beside the bare poke, worth more per second so the
  // chooser takes it. The poke stays the slow one, since it is what the step
  // is clamped against.
  AttackOption fast = params.attacks.front();
  fast.swing_seconds = 0.12;
  fast.damage_per_hit[0] = 10.0;
  params.attacks.push_back(std::move(fast));

  sim.Advance(params, 0.15);  // one swing lands, 30ms carries
  EXPECT_NEAR(sim.attack_fraction(), 0.25, 1e-9);
  EXPECT_NEAR(sim.target_hp_fraction(), 0.99, 1e-9);

  sim.Advance(params, 0.6);  // the 30ms plus 600ms is five more
  EXPECT_NEAR(sim.attack_fraction(), 0.25, 1e-9);
  EXPECT_NEAR(sim.target_hp_fraction(), 0.94, 1e-9);
}

// Same rule for a skill on its own clock, which RunDots and RunRegen already
// followed.
TEST(CombatSimTest, AStepWiderThanTheIntervalFiresEveryCastItCovers) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(10.0, 100.0, {MakeType(&snail, 0.0, 1)});
  AddAutoAttack(params, /*interval=*/0.2, /*damage=*/10.0);

  sim.Advance(params, 1.0);  // five casts, not one
  EXPECT_NEAR(sim.target_hp_fraction(), 0.95, 1e-9);
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

// A frozen monster is stopped where it stands, so the beats that fall while
// the ice holds land nothing on the player. Frostprey is what buys this for a
// character with no ice swing of their own.
TEST(CombatSimTest, AFrozenMobLandsNoHit) {
  Mob snail = MakeMob("Snail", 100000);
  CombatParams params = MakeParams(0.5, 1000.0, {MakeType(&snail, 1.0, 1)});
  GivePlayerHp(params, 1000, /*interval=*/1.0, /*damage=*/10.0);

  CombatSim thawed;
  for (int i = 0; i < 50; ++i) {
    thawed.Advance(params, 0.1);
  }
  EXPECT_LT(thawed.player_hp(), 1000);

  params.attacks[0].freeze_seconds = 2.0;  // relaid by every swing
  CombatSim frozen;
  for (int i = 0; i < 50; ++i) {
    frozen.Advance(params, 0.1);
  }
  EXPECT_EQ(frozen.player_hp(), 1000);

  params.attacks[0].freeze_seconds = 0.0;  // the ice runs out and is not relaid
  for (int i = 0; i < 40; ++i) {
    frozen.Advance(params, 0.1);
  }
  EXPECT_LT(frozen.player_hp(), 1000);
}

// Holy Fountain: healing on a clock of its own, costing no swing and asking
// for no hit. It runs against the damage coming in rather than instead of it,
// and it arrives in pulses -- nothing until the interval is up, then the whole
// helping at once.
TEST(CombatSimTest, AFountainPoursOnItsOwnClock) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(10.0, 1000.0, {MakeType(&snail, 1.0, 1)});
  GivePlayerHp(params, 100, /*interval=*/1.0, /*damage=*/10.0);
  params.regen_pulses = {{0.20, 0, 5.0}};  // 20 HP every 5s against 10 a hit

  // Four hits in, the fountain has poured nothing: the pulse is not owed until
  // its interval is up. A rate would have paid 16 HP by here.
  for (int i = 0; i < 4; ++i) {
    sim.Advance(params, 1.0);
  }
  EXPECT_EQ(sim.player_hp(), 60);
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.player_hp(), 70);
}

// The Evil Eye's aura, which pours a flat amount rather than a share of the
// pool -- and pours it beside the share where a fountain states both.
TEST(CombatSimTest, AFountainPoursItsFlatHalfToo) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(10.0, 1000.0, {MakeType(&snail, 1.0, 1)});
  GivePlayerHp(params, 1000, /*interval=*/1.0, /*damage=*/100.0);
  params.regen_pulses = {{0.02, 24, 2.0}};  // 24 HP and 20 more every 2s

  sim.Advance(params, 2.0);
  EXPECT_EQ(sim.player_hp(), 944);
}

// One step wider than the interval owes every pulse it covered, the way a burn
// ticks for each one it outlasted.
TEST(CombatSimTest, AFountainPoursEveryPulseAWideStepCovered) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(10.0, 1000.0, {MakeType(&snail, 1.0, 1)});
  GivePlayerHp(params, 1000, /*interval=*/1.0, /*damage=*/100.0);
  params.regen_pulses = {{0.02, 0, 2.0}};  // 20 HP every 2s

  sim.Advance(params, 2.0);  // one hit, one pulse
  EXPECT_EQ(sim.player_hp(), 920);
  sim.Advance(params, 6.0);  // one hit again, but three pulses
  EXPECT_EQ(sim.player_hp(), 880);
}

// Two fountains on two clocks, which is what a Bishop carries. Neither waits
// on the other, and a step both come due on pays both.
TEST(CombatSimTest, TwoFountainsPourOnSeparateClocks) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(10.0, 1000.0, {MakeType(&snail, 1.0, 1)});
  GivePlayerHp(params, 1000, /*interval=*/1.0, /*damage=*/100.0);
  params.regen_pulses = {{0.02, 0, 2.0}, {0.03, 0, 3.0}};

  for (int i = 0; i < 2; ++i) {
    sim.Advance(params, 1.0);
  }
  EXPECT_EQ(sim.player_hp(), 820);  // the 2s one, alone
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.player_hp(), 750);  // the 3s one, alone
  for (int i = 0; i < 3; ++i) {
    sim.Advance(params, 1.0);
  }
  EXPECT_EQ(sim.player_hp(), 520);  // both, on the same step
}

// It stops at the pool rather than running past it, the way every other heal
// here does.
TEST(CombatSimTest, AFountainNeverFillsPastTheHpPool) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(10.0, 1000.0, {MakeType(&snail, 1.0, 1)});
  GivePlayerHp(params, 100, /*interval=*/1000.0, /*damage=*/10.0);
  params.regen_pulses = {{0.50, 0, 1.0}};

  sim.Advance(params, 10.0);
  EXPECT_EQ(sim.player_hp(), 100);
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

// A swing can heal on its own account, and what it heals lands on top of what
// the character recovers on any swing at all: Angel Ray pays both.
TEST(CombatSimTest, ASwingsOwnRecoveryPaysBesideTheCharacters) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 1.0, 1)});
  GivePlayerHp(params, 100, /*interval=*/1.0, /*damage=*/20.0);
  params.hp_recover_pct = 0.05;
  params.attacks[0].hp_recover_pct = 0.05;

  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.player_hp(), 90);
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.player_hp(), 80);
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
  params.player_level = 30;

  sim.Advance(params, 1.0);
  ASSERT_EQ(sim.player_hp(), 90);

  // The level lands and the pool grows. Left alone, the bar would read 90 of
  // 200 -- less than half full, having lost one hit.
  params.player_level = 31;
  params.max_player_hp = 200;
  sim.Advance(params, 0.5);
  EXPECT_EQ(sim.player_hp(), 200);
  EXPECT_EQ(sim.player_max_hp(), 200);
}

// A skill point into a passive that carries max HP widens the pool at the same
// level. It is not a level-up and must not heal -- the player would otherwise
// have a free full heal for every point they had left to spend.
TEST(CombatSimTest, AWiderPoolAtTheSameLevelDoesNotHeal) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(10.0, 1000.0, {MakeType(&snail, 1.0, 1)});
  GivePlayerHp(params, 100, /*interval=*/1.0, /*damage=*/10.0);
  params.player_level = 30;

  sim.Advance(params, 1.0);
  ASSERT_EQ(sim.player_hp(), 90);

  params.max_player_hp = 200;
  sim.Advance(params, 0.5);
  EXPECT_EQ(sim.player_hp(), 90);
  EXPECT_EQ(sim.player_max_hp(), 200);
}

// And a pool that shrank -- an unequipped hat -- takes the player down with
// it, rather than leaving them holding HP their stats do not give them. It
// holds for a character with no fountain at all: the clamp belongs to the
// pool, not to the healing.
TEST(CombatSimTest, ANarrowerPoolTakesTheOverflowWithIt) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(10.0, 1000.0, {MakeType(&snail, 1.0, 1)});
  GivePlayerHp(params, 100, /*interval=*/1.0, /*damage=*/10.0);
  params.player_level = 30;

  sim.Advance(params, 1.0);
  ASSERT_EQ(sim.player_hp(), 90);

  params.max_player_hp = 50;
  sim.Advance(params, 0.5);
  EXPECT_EQ(sim.player_hp(), 50);
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

// A second half that reaches fewer enemies than the first lands on that many
// of them, healthiest first -- the shape Piercing Arrow II's fragment has.
TEST(CombatSimTest, TheOpeningHitCanLandOnSeveralMobs) {
  Mob snail = MakeMob("Snail", 100);
  Mob boar = MakeMob("Boar", 1000);
  Mob ogre = MakeMob("Ogre", 10000);
  CombatSim sim;
  CombatParams params =
      MakeParams(1.0, 1000.0,
                 {MakeType(&snail, 10.0, 1), MakeType(&boar, 10.0, 1),
                  MakeType(&ogre, 10.0, 1)},
                 /*reach=*/3);
  AddLead(params, /*damage=*/100.0);
  params.attacks[0].lead_enemies = 2;

  sim.Advance(params, 1.0);
  // The ogre and the boar are the two healthiest, so the fragment finds them
  // and the snail takes the spread alone.
  const EngagedGroup* snails = FindGroup(sim.engaged_groups(), "Snail");
  const EngagedGroup* boars = FindGroup(sim.engaged_groups(), "Boar");
  const EngagedGroup* ogres = FindGroup(sim.engaged_groups(), "Ogre");
  ASSERT_NE(snails, nullptr);
  ASSERT_NE(boars, nullptr);
  ASSERT_NE(ogres, nullptr);
  EXPECT_NEAR(snails->hp_fraction, 0.90, 1e-9);
  EXPECT_NEAR(boars->hp_fraction, 0.89, 1e-9);
  EXPECT_NEAR(ogres->hp_fraction, 0.989, 1e-9);
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

// Throws the swing as `hits` scattered strikes, a repeat keeping `kept` of one.
void AddScatter(CombatParams& params, int hits, double kept) {
  params.attacks[0].scatter_hits = hits;
  params.attacks[0].scatter_repeat_kept = kept;
}

// Five strikes over three enemies: every one of them takes a whole strike
// before any takes a second, and the two spare strikes go to the healthiest --
// GMS's "the flames go for the boss first", read through what this game has.
TEST(CombatSimTest, AScatteredSwingSpreadsBeforeItDoublesUp) {
  Mob snail = MakeMob("Snail", 100);
  Mob boar = MakeMob("Boar", 1000);
  Mob ogre = MakeMob("Ogre", 10000);
  CombatSim sim;
  CombatParams params =
      MakeParams(1.0, 1000.0,
                 {MakeType(&snail, 10.0, 1), MakeType(&boar, 10.0, 1),
                  MakeType(&ogre, 10.0, 1)},
                 /*reach=*/3);
  AddScatter(params, /*hits=*/5, /*kept=*/0.45);

  sim.Advance(params, 1.0);
  const EngagedGroup* snails = FindGroup(sim.engaged_groups(), "Snail");
  const EngagedGroup* boars = FindGroup(sim.engaged_groups(), "Boar");
  const EngagedGroup* ogres = FindGroup(sim.engaged_groups(), "Ogre");
  ASSERT_NE(snails, nullptr);
  ASSERT_NE(boars, nullptr);
  ASSERT_NE(ogres, nullptr);
  // The ogre and the boar take two strikes apiece, the second worth 45% of the
  // first; the snail takes the one nobody doubled up on.
  EXPECT_NEAR(ogres->hp_fraction, 1.0 - 14.5 / 10000.0, 1e-9);
  EXPECT_NEAR(boars->hp_fraction, 1.0 - 14.5 / 1000.0, 1e-9);
  EXPECT_NEAR(snails->hp_fraction, 1.0 - 10.0 / 100.0, 1e-9);
}

// With nothing to spread to, every strike lands on the one enemy there is --
// which is the whole of what the skill is for, and what a plain reading of it
// as an N-enemy swing would throw away. The rate has to say so too, or the
// fight would never reach for it.
TEST(CombatSimTest, AScatteredSwingLandsEveryStrikeOnALoneEnemy) {
  Mob snail = MakeMob("Snail", 100000);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 10.0, 1)},
                                   /*reach=*/5);
  AddScatter(params, /*hits=*/5, /*kept=*/0.45);
  params.attacks.push_back(
      MakeSkill("Plain", /*damage=*/20.0, /*cooldown=*/0.0));

  // 10 + four repeats at 4.5 is 28, against Plain's 20.
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.attack_name(), "Attack");
  EXPECT_NEAR(sim.target_hp_fraction(), 1.0 - 28.0 / 100000.0, 1e-9);
}

// A swing reaching further than it has strikes to throw touches only as many
// enemies as it threw. The burns and the freeze follow, all three being read
// off the one count.
TEST(CombatSimTest, AScatteredSwingReachesNoFurtherThanItsStrikes) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 10.0, 4)},
                                   /*reach=*/4);
  AddScatter(params, /*hits=*/2, /*kept=*/0.45);

  sim.Advance(params, 1.0);
  // Two of the four snails took a strike apiece; the other two took nothing at
  // all, so the group has lost 20 of its 4000 rather than 40.
  const EngagedGroup* snails = FindGroup(sim.engaged_groups(), "Snail");
  ASSERT_NE(snails, nullptr);
  EXPECT_NEAR(snails->hp_fraction, 1.0 - 20.0 / 4000.0, 1e-9);
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

// Creeping Toxin: the pool ticks away, and every fourth tick detonates instead.
// The same swap on the other clock, counted in pulses rather than swings.
TEST(CombatSimTest, LandsAnEmpoweredPulseOnceEveryNth) {
  Mob snail = MakeMob("Snail", 20);
  CombatSim sim;
  // A swing worth nothing, so only the summon moves the bar.
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 0.0, 1)});
  AddAutoAttack(params, /*interval=*/1.0, /*damage=*/1.0);
  SetEmpoweredForm(params.auto_attacks[0], /*every=*/4, /*damage=*/8.0);

  for (int i = 0; i < 3; ++i) {
    sim.Advance(params, 1.0);
  }
  EXPECT_NEAR(sim.target_hp_fraction(), 0.85, 1e-9);
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.target_hp_fraction(), 0.45, 1e-9)
      << "the fourth pulse detonates instead of ticking";
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.target_hp_fraction(), 0.40, 1e-9)
      << "and the count starts again from the fifth";
}

// Walking somewhere else is starting again: another map's attacks were another
// map's indices, so the count toward the bigger form starts from nothing.
TEST(CombatSimTest, WalkingToAnotherMapForgetsTheEmpoweredCount) {
  Mob snail = MakeMob("Snail", 20);
  CombatSim sim;
  CombatParams field = MakeParams(1.0, 1000.0, {MakeType(&snail, 1.0, 1)});
  SetEmpoweredForm(field.attacks[0], /*every=*/4, /*damage=*/10.0);
  for (int i = 0; i < 3; ++i) {
    sim.Advance(field, 1.0);
  }

  CombatParams forest = field;
  forest.encounter = "forest";
  // The swing that would have been the fourth is the first here, so it is an
  // ordinary one. Carrying the count over would take half the mob's HP.
  sim.Advance(forest, 1.0);
  EXPECT_NEAR(sim.target_hp_fraction(), 0.95, 1e-9);
  for (int i = 0; i < 3; ++i) {
    sim.Advance(forest, 1.0);
  }
  EXPECT_NEAR(sim.target_hp_fraction(), 0.35, 1e-9)
      << "the fourth swing here lands the empowered form";
}

// And the same for the wait on the strike a swing sets off. It used to be the
// one clock BeginMapIfChanged did not reset, so a Night Lord walking away kept
// Showdown's wait while the shuriken's own went back to nothing.
TEST(CombatSimTest, WalkingToAnotherMapForgetsTheSideStrikesWait) {
  Mob mob = MakeMob("Snail", 1000000);
  CombatParams field = MakeParams(1.0, 1e9, {MakeType(&mob, 0.0, 1)});
  AttackOption strike;
  strike.max_enemies = 1;
  strike.damage_per_hit.assign(1, 1000.0);
  strike.cooldown_seconds = 100.0;
  field.attacks[0].side = std::make_shared<const AttackOption>(strike);

  CombatSim sim;
  sim.Advance(field, 1.0);  // the strike goes out and starts its long wait

  CombatParams forest = field;
  forest.encounter = "forest";
  sim.Advance(forest, 1.0);
  // A fresh map is a fresh wait, so the strike goes out again at once. Keeping
  // the wait would leave the mob untouched: the swing itself deals nothing.
  double taken = (1.0 - sim.target_hp_fraction()) * 1000000.0;
  EXPECT_NEAR(taken, 1000.0, 1e-6);
}

// The two clocks count apart. A swing landing its empowered form must not
// bring the summon's round forward, or the Sniper's Piercing Arrow would set
// off a pool it has nothing to do with.
TEST(CombatSimTest, EachClockCountsItsOwnEmpoweredRound) {
  Mob snail = MakeMob("Snail", 20);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 1.0, 1)});
  SetEmpoweredForm(params.attacks[0], /*every=*/2, /*damage=*/3.0);
  AddAutoAttack(params, /*interval=*/1.0, /*damage=*/1.0);
  SetEmpoweredForm(params.auto_attacks[0], /*every=*/4, /*damage=*/40.0);

  // Three seconds in: the swing has landed its form once, on the second of
  // three, for 1 + 3 + 1. The summon has pulsed three times and must still be
  // waiting for its fourth, so all three are worth 1.
  for (int i = 0; i < 3; ++i) {
    sim.Advance(params, 1.0);
  }
  EXPECT_NEAR(sim.target_hp_fraction(), 0.60, 1e-9);
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

// Divine Judgment: Blast brands what it strikes, and the brand belongs to the
// enemy. A monster stepping into a fight already in progress starts its own
// count from nothing rather than inheriting where the swing had got to.
TEST(CombatSimTest, ABrandRidesTheEnemyRatherThanTheSwing) {
  Mob soft = MakeMob("Soft", 20);
  Mob tough = MakeMob("Tough", 20);
  CombatSim sim;
  CombatParams params = MakeParams(
      1.0, 1000.0, {MakeType(&soft, 20.0, 1), MakeType(&tough, 1.0, 1)});
  SetEmpoweredForm(params.attacks[0], /*every=*/2, /*damage=*/6.0, /*reach=*/1,
                   /*marks=*/true);

  sim.Advance(params, 1.0);
  ASSERT_EQ(sim.target_name(), "Tough") << "the soft one dies to one strike";
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.target_hp_fraction(), 0.95, 1e-9)
      << "the newcomer's first strike is an ordinary one, not the swing's 2nd";
  sim.Advance(params, 1.0);
  // 19 - (1 + 6) of 20. Replacing the strike rather than riding it would leave
  // 13, and inheriting the swing's count would have gone off a swing sooner.
  EXPECT_NEAR(sim.target_hp_fraction(), 0.60, 1e-9)
      << "the mark goes off on the second strike IT has taken, on top of it";
}

// Marking enemies, the form never takes the whole swing: it goes off on the
// mobs whose marks came due, and everything else the swing reached takes its
// ordinary strike beside them.
TEST(CombatSimTest, AMarkGoesOffOnlyOnTheEnemyThatEarnedIt) {
  Mob soft = MakeMob("Soft", 20);
  Mob tough = MakeMob("Tough", 20);
  CombatSim sim;
  CombatParams params = MakeParams(
      1.0, 1000.0, {MakeType(&soft, 20.0, 1), MakeType(&tough, 1.0, 2)},
      /*reach=*/2);
  SetEmpoweredForm(params.attacks[0], /*every=*/2, /*damage=*/6.0, /*reach=*/2,
                   /*marks=*/true);

  // The first swing kills the soft one and marks the tough one beside it. The
  // second reaches both tough ones: one is due, the other has only just
  // arrived.
  sim.Advance(params, 1.0);
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.target_hp_fraction(), 0.60, 1e-9)
      << "12 of 20: one ordinary strike, then a second with the mark on top";
  const EngagedGroup* group = FindGroup(sim.engaged_groups(), "Tough");
  ASSERT_NE(group, nullptr);
  EXPECT_EQ(group->count, 2);
  EXPECT_NEAR(group->hp_fraction, 0.775, 1e-9)
      << "12 and 19 of 20 apiece: only one of them was due";
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

// Final Pact: a passive that catches the hit that would have emptied the
// player. They stand where they fell with the whole pool back, and the fight
// carries on -- see AdvanceCombat for what dying costs when it is not caught.
TEST(CombatSimTest, APactCatchesTheHitThatWouldHaveKilled) {
  Mob snail = MakeMob("Snail", 1000);  // too tough to kill, so the hits keep
  CombatSim sim;                       // coming
  CombatParams params = MakeParams(10.0, 1000.0, {MakeType(&snail, 1.0, 1)});
  GivePlayerHp(params, 100, /*interval=*/1.0, /*damage=*/60.0);
  params.revive_cooldown_seconds = 10.0;

  sim.Advance(params, 1.0);
  ASSERT_EQ(sim.player_hp(), 40);

  sim.Advance(params, 1.0);
  EXPECT_FALSE(sim.died_this_step());
  EXPECT_EQ(sim.player_hp(), 100);

  // The next one inside the wait is a real death.
  sim.Advance(params, 1.0);
  ASSERT_EQ(sim.player_hp(), 40);
  sim.Advance(params, 1.0);
  EXPECT_TRUE(sim.died_this_step());
}

TEST(CombatSimTest, APactCatchesAgainOnceItsWaitIsOut) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(10.0, 1000.0, {MakeType(&snail, 1.0, 1)});
  GivePlayerHp(params, 100, /*interval=*/1.0, /*damage=*/60.0);
  params.revive_cooldown_seconds = 2.0;

  sim.Advance(params, 1.0);
  sim.Advance(params, 1.0);
  ASSERT_EQ(sim.player_hp(), 100);  // caught, and the wait starts

  sim.Advance(params, 1.0);
  ASSERT_EQ(sim.player_hp(), 40);
  sim.Advance(params, 1.0);  // two seconds on, so it catches this one too
  EXPECT_FALSE(sim.died_this_step());
  EXPECT_EQ(sim.player_hp(), 100);
}

// Gives `params` a timed buff: while it is up, every swing hits `factor` times
// as hard. Its table is the same attacks with bigger numbers in them, which is
// the shape ComputeCombatParams really builds.
void GiveBuff(CombatParams& params, double duration, double cooldown,
              double factor, double heal = 0.0, double reduction = 0.0,
              double soften = 0.0) {
  BuffOption buff;
  buff.name = "Dark Resonance";
  buff.duration_seconds = duration;
  buff.cooldown_seconds = cooldown;
  buff.heal_fraction = heal;
  buff.cooldown_reduction_seconds = reduction;
  buff.damage_taken_pct = soften;
  params.buffs.push_back(std::move(buff));
  AttackSet set;
  set.attacks = params.attacks;
  set.auto_attacks = params.auto_attacks;
  set.triggered_attacks = params.triggered_attacks;
  for (AttackOption& attack : set.attacks) {
    for (double& damage : attack.damage_per_hit) {
      damage *= factor;
    }
  }
  params.buffed.push_back(std::move(set));
}

// A cast is time the character is not swinging in: raising the buff takes its
// animation off the swing they were charging, so the step it goes up on lands
// one swing fewer.
TEST(CombatSimTest, RaisingABuffCostsTheSwingItsAnimation) {
  Mob snail = MakeMob("Snail", 1e9);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 1e9, {MakeType(&snail, 10.0, 1)});
  GiveBuff(params, /*duration=*/10.0, /*cooldown=*/60.0, /*factor=*/1.0);
  params.buffs[0].cast_seconds = 0.6;

  // A second of a one-second swing, less the six-tenths the cast took: the
  // swing the step would have landed is still four-tenths short.
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.damage_this_step(), 0.0);
  // It carries, so the swing the cast held up lands on the step after.
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.damage_this_step(), 10.0);
}

// Smokescreen's shape: a buff that costs the mob rather than paying the
// player. What it cancels lapses with it, so the hit after it is the whole
// hit again.
TEST(CombatSimTest, ABuffCanSoftenTheHitsWhileItStands) {
  Mob snail = MakeMob("Snail", 100000);
  CombatSim sim;
  CombatParams params = MakeParams(10.0, 1000.0, {MakeType(&snail, 1.0, 1)});
  GivePlayerHp(params, 100, /*interval=*/1.0, /*damage=*/10.0);
  GiveBuff(params, /*duration=*/2.0, /*cooldown=*/10.0, /*factor=*/1.0,
           /*heal=*/0.0, /*reduction=*/0.0, /*soften=*/0.5);

  // The buff answers this hit with its heal rather than softening it: it goes
  // up after the blow has landed, which is the order the whole step runs in.
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.player_hp(), 90);
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.player_hp(), 85);
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.player_hp(), 80);
  sim.Advance(params, 1.0);  // lapsed, and the wait is still running
  EXPECT_EQ(sim.player_hp(), 70);
}

// Smokescreen over a party: the Shadower's clock rather than the reader's,
// and no swing of theirs spent raising it. It softens the same way their own
// buff would and lapses the same way, so the hit after it is whole again.
TEST(CombatSimTest, APartysBuffSoftensTheHitsAndCostsNoSwing) {
  Mob snail = MakeMob("Snail", 100000);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 1.0, 1)});
  GivePlayerHp(params, 100, /*interval=*/1.0, /*damage=*/10.0);
  BuffOption ally;
  ally.name = "Smokescreen";
  ally.duration_seconds = 2.0;
  ally.cooldown_seconds = 4.0;
  ally.damage_taken_pct = 0.5;
  params.ally_buffs.push_back(std::move(ally));

  // Up after this step's blow has landed, as the character's own buffs are --
  // and the swing still lands, because nobody here cast anything.
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.player_hp(), 90);
  EXPECT_EQ(sim.damage_this_step(), 1.0);
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.player_hp(), 85);
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.player_hp(), 80);
  sim.Advance(params, 1.0);  // lapsed, and the caster's wait still running
  EXPECT_EQ(sim.player_hp(), 70);
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.player_hp(), 60);  // the wait closes, and it goes up again
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.player_hp(), 55);
}

// Two shelters are not one shelter twice over: the party's and the
// character's own multiply, the way every reduction in the game does.
TEST(CombatSimTest, APartysBuffMultipliesWithTheCharactersOwn) {
  Mob snail = MakeMob("Snail", 100000);
  CombatSim sim;
  CombatParams params = MakeParams(10.0, 1000.0, {MakeType(&snail, 1.0, 1)});
  GivePlayerHp(params, 100, /*interval=*/1.0, /*damage=*/20.0);
  GiveBuff(params, /*duration=*/10.0, /*cooldown=*/60.0, /*factor=*/1.0,
           /*heal=*/0.0, /*reduction=*/0.0, /*soften=*/0.5);
  BuffOption ally;
  ally.name = "Smokescreen";
  ally.duration_seconds = 10.0;
  ally.cooldown_seconds = 60.0;
  ally.damage_taken_pct = 0.5;
  params.ally_buffs.push_back(std::move(ally));

  sim.Advance(params, 1.0);
  ASSERT_EQ(sim.player_hp(), 80);  // both go up behind this blow
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.player_hp(), 75);  // a quarter of the hit, not none of it
}

// Holy Magic Shell's shape: a buff that cancels whole hits rather than taking
// a share off each of them, and heals the pool it is raised on.
void GiveShield(CombatParams& params, int hits, double boss_soften,
                double heal = 0.0) {
  BuffOption buff;
  buff.name = "Holy Magic Shell";
  buff.duration_seconds = 100.0;
  buff.cooldown_seconds = 1000.0;
  buff.shield_hits = hits;
  buff.boss_damage_taken_pct = boss_soften;
  buff.heal_fraction = heal;
  params.buffs.push_back(std::move(buff));
  AttackSet set;
  set.attacks = params.attacks;
  set.auto_attacks = params.auto_attacks;
  set.triggered_attacks = params.triggered_attacks;
  params.buffed.push_back(std::move(set));
}

// The shell waits for the pool to be worth filling, swallows its count of
// hits whole, and falls the moment the last of them is spent -- with its
// clock nowhere near run out.
TEST(CombatSimTest, AShellBlocksWholeHitsUntilItsCountRunsOut) {
  Mob snail = MakeMob("Snail", 1e9);
  CombatSim sim;
  CombatParams params = MakeParams(10.0, 1000.0, {MakeType(&snail, 1.0, 1)});
  GivePlayerHp(params, 100, /*interval=*/1.0, /*damage=*/40.0);
  GiveShield(params, /*hits=*/2, /*boss_soften=*/0.5);

  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.player_hp(), 60);  // full pool: nothing worth raising it for
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.player_hp(), 20);  // low enough, so it goes up behind the blow
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.player_hp(), 20);
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.player_hp(), 20);  // the second block, and the shell falls
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.player_hp(), 0);
  EXPECT_TRUE(sim.died_this_step());
}

// A boss's hit is the one a shell cannot swallow: it costs its share less and
// spends no block, so the shell is still whole however many land.
TEST(CombatSimTest, AShellBluntsABossHitInsteadOfBlockingIt) {
  Mob zakum = MakeMob("Zakum", 1e9);
  zakum.set_boss(true);
  CombatSim sim;
  CombatParams params = MakeParams(10.0, 1000.0, {MakeType(&zakum, 1.0, 1)});
  GivePlayerHp(params, 1000, /*interval=*/1.0, /*damage=*/40.0);
  GiveShield(params, /*hits=*/2, /*boss_soften=*/0.5);

  // Raised on the first step rather than held for a low pool: against a boss
  // one blow is the whole fight, so it goes up the moment it comes round.
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.player_hp(), 960);
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.player_hp(), 940);
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.player_hp(), 920);
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.player_hp(), 900);  // still blunting: no block was ever spent
}

// One shell over the whole party: an ally's cast heals this character and
// blocks their hits, on the caster's clock and costing them no swing.
TEST(CombatSimTest, APartysShellHealsAndBlocksForEverybodyUnderIt) {
  Mob snail = MakeMob("Snail", 1e9);
  CombatSim sim;
  CombatParams params = MakeParams(10.0, 1000.0, {MakeType(&snail, 1.0, 1)});
  GivePlayerHp(params, 100, /*interval=*/1.0, /*damage=*/40.0);
  BuffOption ally;
  ally.name = "Holy Magic Shell";
  ally.duration_seconds = 100.0;
  ally.cooldown_seconds = 1000.0;
  ally.shield_hits = 2;
  ally.heal_fraction = 0.5;
  params.ally_buffs.push_back(std::move(ally));

  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.player_hp(), 60);
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.player_hp(), 70);  // the blow lands, then the heal answers it
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.player_hp(), 70);
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.player_hp(), 70);  // the second block, and the shell falls
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.player_hp(), 30);
}

// Two shells are not one shell twice over: a hit spends a block off one of
// them, so the party's and the character's own last twice as long between
// them rather than being burned two at a time.
TEST(CombatSimTest, OneHitSpendsOneBlockHoweverManyShellsStand) {
  Mob snail = MakeMob("Snail", 1e9);
  CombatSim sim;
  CombatParams params = MakeParams(10.0, 1000.0, {MakeType(&snail, 1.0, 1)});
  GivePlayerHp(params, 100, /*interval=*/1.0, /*damage=*/40.0);
  GiveShield(params, /*hits=*/1, /*boss_soften=*/0.0);
  BuffOption ally;
  ally.name = "Holy Magic Shell";
  ally.duration_seconds = 100.0;
  ally.cooldown_seconds = 1000.0;
  ally.shield_hits = 1;
  params.ally_buffs.push_back(std::move(ally));

  sim.Advance(params, 1.0);
  ASSERT_EQ(sim.player_hp(), 60);
  sim.Advance(params, 1.0);
  ASSERT_EQ(sim.player_hp(), 20);  // both go up behind this blow
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.player_hp(), 20);  // the character's own block pays for it
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.player_hp(), 20);  // and the party's for the next
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.player_hp(), 0);
}

// Puncture's shape: a weaker swing that leaves a wound, and a harder one the
// fight would otherwise never put down. The buff is laid by the weak swing
// rather than raised on a wait, and while it stands every swing hits `factor`
// times as hard.
void GiveWound(CombatParams& params, double duration, double factor) {
  params.attacks.push_back(MakeSkill("Puncture", 5.0, /*cooldown=*/0.0));
  params.attacks.push_back(MakeSkill("Raging Blow", 20.0, /*cooldown=*/0.0));
  BuffOption buff;
  buff.name = "Puncture";
  buff.duration_seconds = duration;
  buff.laid_by_attack = 1;  // where Puncture's swing sits in params.attacks
  params.buffs.push_back(std::move(buff));
  AttackSet set;
  set.attacks = params.attacks;
  set.auto_attacks = params.auto_attacks;
  set.triggered_attacks = params.triggered_attacks;
  for (AttackOption& attack : set.attacks) {
    for (double& damage : attack.damage_per_hit) {
      damage *= factor;
    }
  }
  params.buffed.push_back(std::move(set));
}

// The whole of the mechanism: the fight spends a swing laying the wound, then
// goes back to the swing that hits hardest, and comes back when it lapses.
//
// Read off the damage rather than attack_name(), which names the swing being
// charged NEXT -- the strike re-aims before the step ends.
TEST(CombatSimTest, TheFightSpendsASwingToLayALapsedBuff) {
  Mob snail = MakeMob("Snail", 10000);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 10.0, 1)});
  GiveWound(params, /*duration=*/3.0, /*factor=*/2.0);

  // 5: Puncture, the weakest swing on offer, and landing bare -- the wound it
  // leaves is not up while it is being laid.
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.target_hp_fraction(), 0.9995, 1e-9);
  // 40 twice: the hardest swing, doubled by the wound now standing.
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.target_hp_fraction(), 0.9955, 1e-9);
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.target_hp_fraction(), 0.9915, 1e-9);
  // Three seconds up, so the wound has lapsed -- but the swing aimed while it
  // still stood is committed to and finishes, landing 20 rather than 40.
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.target_hp_fraction(), 0.9895, 1e-9);
  // Only then is it laid again, for another 5.
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.target_hp_fraction(), 0.9890, 1e-9);
}

// The wound itself: a pulse that waits for the buff its skill lays, so it
// ticks only where one was left rather than from the moment the skill is
// learned.
TEST(CombatSimTest, APulseGatedOnABuffWaitsForItToBeLaid) {
  Mob snail = MakeMob("Snail", 10000);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 10.0, 1)});
  AddAutoAttack(params, /*interval=*/1.0, /*damage=*/100.0);
  GiveWound(params, /*duration=*/3.0, /*factor=*/1.0);
  params.auto_attacks[0].name = "Puncture";
  params.auto_attacks[0].needs_buff = 0;
  params.buffed[0].auto_attacks = params.auto_attacks;

  // 5 for the swing that lays it and nothing from the pulse: no wound stood
  // when the step began.
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.target_hp_fraction(), 0.9995, 1e-9);
  // Now it ticks, beside the 20 the hardest swing lands.
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.target_hp_fraction(), 0.9875, 1e-9);
}

// Cry Valhalla's shape: a pulse that lands several strikes at once and runs
// out before the buff behind it does. Twelve strikes over four ticks here, at
// three a tick -- and the fifth tick, still inside the window, lands nothing.
TEST(CombatSimTest, ACappedPulseFallsSilentBeforeTheBuffLapses) {
  Mob snail = MakeMob("Snail", 100000);
  CombatSim sim;
  CombatParams params = MakeParams(1000.0, 1000.0, {MakeType(&snail, 10.0, 1)});
  AddAutoAttack(params, /*interval=*/1.0, /*damage=*/100.0);
  params.auto_attacks[0].name = "Cry Valhalla";
  params.auto_attacks[0].strikes_per_pulse = 3;
  params.auto_attacks[0].max_pulses = 4;
  GiveBuff(params, /*duration=*/8.0, /*cooldown=*/1000.0, /*factor=*/1.0);
  params.auto_attacks[0].needs_buff = 0;
  params.buffed[0].auto_attacks = params.auto_attacks;

  // Three strikes of 100 a tick, four ticks: 1200 of the snail's 100000.
  for (int step = 0; step < 4; ++step) {
    sim.Advance(params, 1.0);
  }
  ASSERT_NEAR(sim.target_hp_fraction(), 0.988, 1e-9);
  // Four more seconds of a buff that is still up, and nothing more lands.
  for (int step = 0; step < 4; ++step) {
    sim.Advance(params, 1.0);
  }
  EXPECT_NEAR(sim.target_hp_fraction(), 0.988, 1e-9);
}

// The count is per raising, not per fight: the next window is worth the whole
// twelve again.
TEST(CombatSimTest, ACappedPulseIsWorthItsWholeCountAgainNextWindow) {
  Mob snail = MakeMob("Snail", 100000);
  CombatSim sim;
  CombatParams params = MakeParams(1000.0, 1000.0, {MakeType(&snail, 10.0, 1)});
  AddAutoAttack(params, /*interval=*/1.0, /*damage=*/100.0);
  params.auto_attacks[0].name = "Cry Valhalla";
  params.auto_attacks[0].strikes_per_pulse = 3;
  params.auto_attacks[0].max_pulses = 2;
  GiveBuff(params, /*duration=*/3.0, /*cooldown=*/6.0, /*factor=*/1.0);
  params.auto_attacks[0].needs_buff = 0;
  params.buffed[0].auto_attacks = params.auto_attacks;

  // Two ticks of 300 while the first window stands, then nothing until it
  // comes round on the seventh second and pays another two.
  for (int step = 0; step < 6; ++step) {
    sim.Advance(params, 1.0);
  }
  ASSERT_NEAR(sim.target_hp_fraction(), 0.994, 1e-9);
  for (int step = 0; step < 2; ++step) {
    sim.Advance(params, 1.0);
  }
  EXPECT_NEAR(sim.target_hp_fraction(), 0.988, 1e-9);
}

// The other half of the rule: a wound still standing is left alone. Nothing
// re-lays it early, so the hard swing keeps every turn after the first.
TEST(CombatSimTest, ABuffStillStandingIsNotLaidAgain) {
  Mob snail = MakeMob("Snail", 10000);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 10.0, 1)});
  GiveWound(params, /*duration=*/1000.0, /*factor=*/2.0);

  for (int step = 0; step < 6; ++step) {
    sim.Advance(params, 1.0);
  }
  // 5 to lay it, then 40 five times over.
  EXPECT_NEAR(sim.target_hp_fraction(), 0.9795, 1e-9);
}

// Dark Resonance's shape, and the one thing that makes a timed buff a
// mechanism rather than another passive: it lapses before it comes round
// again, so the fight swings buffed for part of the time and bare for the
// rest.
TEST(CombatSimTest, ABuffLandsHarderUntilItLapses) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 10.0, 1)});
  GiveBuff(params, /*duration=*/2.0, /*cooldown=*/5.0, /*factor=*/2.0);

  sim.Advance(params, 1.0);  // up from the first step, so 20 rather than 10
  EXPECT_NEAR(sim.target_hp_fraction(), 0.98, 1e-9);
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.target_hp_fraction(), 0.96, 1e-9);
  // Two seconds on it lapses, and the same swing is worth half of what it was.
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.target_hp_fraction(), 0.95, 1e-9);
}

TEST(CombatSimTest, ABuffComesBackWhenItsWaitIsOut) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 10.0, 1)});
  GiveBuff(params, /*duration=*/2.0, /*cooldown=*/5.0, /*factor=*/2.0);

  for (int step = 0; step < 5; ++step) {
    sim.Advance(params, 1.0);
  }
  // 20, 20, then 10 three times: the buff is spent and the wait is not out.
  ASSERT_NEAR(sim.target_hp_fraction(), 0.93, 1e-9);
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.target_hp_fraction(), 0.91, 1e-9);
}

// What the player buys by swinging fast, beyond the damage: the buff comes
// round sooner. Five seconds of waiting, less a second for every swing landed
// in the meantime, is a buff back in four steps rather than six.
TEST(CombatSimTest, AttackingShortensTheWaitForTheNextBuff) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 10.0, 1)});
  GiveBuff(params, /*duration=*/2.0, /*cooldown=*/5.0, /*factor=*/2.0,
           /*heal=*/0.0, /*reduction=*/1.0);

  for (int step = 0; step < 3; ++step) {
    sim.Advance(params, 1.0);
  }
  ASSERT_NEAR(sim.target_hp_fraction(), 0.95, 1e-9);  // 20, 20, 10
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.target_hp_fraction(), 0.93, 1e-9);  // up again already
}

TEST(CombatSimTest, ABuffHealsTheShareItPromisesWhenItGoesUp) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(10.0, 1000.0, {MakeType(&snail, 1.0, 1)});
  GivePlayerHp(params, 100, /*interval=*/1.0, /*damage=*/60.0);
  GiveBuff(params, /*duration=*/1.0, /*cooldown=*/1000.0, /*factor=*/1.0,
           /*heal=*/0.5);

  // The hit lands first, then the buff goes up and hands half the pool back.
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.player_hp(), 90);
  // And only when it goes up: the next hit is not healed.
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.player_hp(), 30);
}

// Mortal Blow's shape: a chance rolled once for the whole swing that lands a
// share of it again on ONE enemy, and hands a slice of the pool back when it
// does. Certain and doubled, so the roll cannot hide behind the noise.
TEST(CombatSimTest, AChanceCanLandOneEnemyHarderAndPayTheHitBack) {
  Mob snail = MakeMob("Snail", 1000);
  CombatParams params =
      MakeParams(1.0, 1000.0, {MakeType(&snail, 10.0, 4)}, /*reach=*/2);
  GivePlayerHp(params, 100, /*interval=*/1000.0, /*damage=*/0.0);
  CombatSim plain;
  plain.Advance(params, 1.0);
  // Two enemies, ten apiece, averaged into the one bar the pair share.
  ASSERT_EQ(plain.engaged_groups().size(), 1u);
  ASSERT_NEAR(plain.engaged_groups()[0].hp_fraction, 0.99, 1e-9);

  params.attacks[0].procs.push_back(
      {/*chance=*/1.0, /*damage_pct=*/1.0, /*hp_recover_pct=*/0.25});
  CombatSim rolled;
  rolled.Advance(params, 1.0);
  // The front one takes its ten twice over; the one behind it takes ten still.
  ASSERT_EQ(rolled.engaged_groups().size(), 1u);
  EXPECT_NEAR(rolled.engaged_groups()[0].hp_fraction, 0.985, 1e-9);
  EXPECT_EQ(rolled.player_hp(), 100);  // already full, so the quarter is capped

  GivePlayerHp(params, 100, /*interval=*/0.5, /*damage=*/50.0);
  CombatSim healed;
  healed.Advance(params, 1.0);
  // Fifty taken, then twenty-five put back by the swing that landed.
  EXPECT_EQ(healed.player_hp(), 75);
}

// A buff bought with landed hits rather than with a wait: it goes up on the
// hit that finishes the count, and nothing counts while it stands -- so its
// uptime is what the character's firing rate buys and no more.
TEST(CombatSimTest, ABuffCanWaitOnLandedHitsRatherThanOnAClock) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 10.0, 1)});
  BuffOption buff;
  buff.name = "Mortal Blow";
  buff.duration_seconds = 2.0;
  buff.charge_lines = 3;
  params.buffs.push_back(std::move(buff));
  AttackSet set;
  set.attacks = params.attacks;
  set.attacks[0].damage_per_hit[0] = 100.0;
  params.buffed.push_back(std::move(set));

  // Three swings to charge it, each landing the plain ten.
  for (int step = 0; step < 3; ++step) {
    sim.Advance(params, 1.0);
  }
  EXPECT_NEAR(sim.target_hp_fraction(), 0.97, 1e-9);
  // It stands now, and the next two swings land under it.
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.target_hp_fraction(), 0.87, 1e-9);
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.target_hp_fraction(), 0.77, 1e-9);
  // Those two landed while it stood, so neither counted toward the next one:
  // three more plain swings are owed before it comes back.
  for (int step = 0; step < 3; ++step) {
    sim.Advance(params, 1.0);
  }
  EXPECT_NEAR(sim.target_hp_fraction(), 0.74, 1e-9);
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.target_hp_fraction(), 0.64, 1e-9);
}

// Freezing Crush's shape: an ice swing leaves a stack per line AND four
// seconds of ice on what it hit, and a lightning swing spends one stack per
// line and hits harder for every stack it went in holding. The lightning swing
// is the harder of the two on its own, so the chooser would take it every time
// and neither the pile nor the ice would exist -- what makes it build is the
// credit an ice swing gets for what it leaves behind.
//
// The two are separate questions: the pile says how much a stack is worth, the
// ice says whether it is collected at all.
void GiveFreezeStacks(CombatParams& params, int cap) {
  params.freeze_cap = cap;
  AttackOption ice = MakeSkill("Cold Beam", 10.0, /*cooldown=*/0.0);
  ice.lines = 2;
  ice.freeze_build = 2;
  ice.freeze_seconds = 4.0;
  params.attacks.push_back(std::move(ice));
  AttackOption bolt = MakeSkill("Thunder Bolt", 11.0, /*cooldown=*/0.0);
  bolt.lines = 2;
  bolt.freeze_spends = true;
  bolt.freeze_fd_per_stack = 0.5;
  params.attacks.push_back(std::move(bolt));
}

TEST(CombatSimTest, TheIceSwingBuildsThePileTheLightningSwingSpends) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 0.0, 1)});
  GiveFreezeStacks(params, /*cap=*/2);

  // Ice first: 10 of its own, plus the two stacks it leaves, which are worth
  // half of the lightning swing apiece.
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.attack_name(), "Thunder Bolt");  // aimed next, pile full
  EXPECT_NEAR(sim.target_hp_fraction(), 0.99, 1e-9);
  // Lightning next, at 11 doubled by the two stacks it spends.
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.target_hp_fraction(), 0.968, 1e-9);
  // And back, because the pile is empty again.
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.target_hp_fraction(), 0.958, 1e-9);
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.target_hp_fraction(), 0.936, 1e-9);
}

TEST(CombatSimTest, WithNoPileToBuildTheHarderSwingSimplyWins) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 0.0, 1)});
  GiveFreezeStacks(params, /*cap=*/0);

  for (int step = 0; step < 4; ++step) {
    sim.Advance(params, 1.0);
  }
  EXPECT_EQ(sim.attack_name(), "Thunder Bolt");
  EXPECT_NEAR(sim.target_hp_fraction(), 0.956, 1e-9);  // 11 four times
}

// The critical damage a held stack grants rides EVERY swing, ice and lightning
// alike: a frozen enemy is frozen whichever element is hitting it.
TEST(CombatSimTest, AHeldStackLiftsTheIceSwingItCameFrom) {
  Mob snail = MakeMob("Snail", 1000);
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 0.0, 1)});
  GiveFreezeStacks(params, /*cap=*/4);
  // No lightning swing at all, so nothing ever spends what the ice leaves.
  params.attacks.pop_back();
  params.attacks[1].freeze_crit_gain = 0.25;

  CombatSim sim;
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.target_hp_fraction(), 0.99, 1e-9);  // 10, nothing held yet
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.target_hp_fraction(), 0.975, 1e-9);  // 10 x 1.5, two held
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.target_hp_fraction(), 0.955, 1e-9);  // 10 x 2.0, four held
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.target_hp_fraction(), 0.935, 1e-9);  // capped, so no more
}

// Lightning Orb's shape: a swing that is held, pulsing at 10 damage every
// 0.15s for up to 12 pulses, and ending on a 50-damage burst that costs 0.2s.
// The floor is 0.96s, which five pulses and the finish fit inside.
AttackOption MakeHeldSwing() {
  AttackOption orb = MakeSkill("Lightning Orb", 0.0, /*cooldown=*/0.0);
  orb.channel.pulses = 12;
  orb.channel.min_pulses = 5;
  orb.channel.pulse_seconds = 0.15;
  orb.channel.finish_seconds = 0.2;
  orb.channel.min_seconds = 0.96;
  orb.groups.push_back({{10.0}, SwingRolls{}});
  orb.groups.push_back({{50.0}, SwingRolls{}});
  orb.damage_per_hit = {12 * 10.0 + 50.0};
  orb.swing_seconds = HoldSeconds(orb.channel, orb.channel.pulses);
  return orb;
}

// Steps the fight in slices and totals what the character dealt. Advance
// clamps one call to a single swing of the bare poke, so a hold longer than
// that has to be walked rather than jumped.
double RunFor(CombatSim& sim, const CombatParams& params, double seconds) {
  double damage = 0.0;
  for (double step = 0.0; step + 1e-9 < seconds; step += 0.01) {
    sim.Advance(params, 0.01);
    damage += sim.damage_this_step();
  }
  return damage;
}

// A boss is never within reach of the finish, so the orb is held to the end:
// twelve pulses and the burst, over the whole two seconds.
TEST(CombatSimTest, AHoldRunsToTheEndAgainstSomethingThatSurvivesIt) {
  Mob boss = MakeMob("Zakum", 1000000);
  CombatParams params = MakeParams(1.0, 0.0, {MakeType(&boss, 0.0, 1)});
  params.attacks.push_back(MakeHeldSwing());

  CombatSim sim;
  EXPECT_DOUBLE_EQ(RunFor(sim, params, 1.95), 0.0);  // still being held
  EXPECT_EQ(sim.attack_name(), "Lightning Orb");
  EXPECT_DOUBLE_EQ(RunFor(sim, params, 0.1), 170.0);  // 12 x 10, then 50
}

// Against something the burst alone nearly kills, the hold is let go at its
// floor: five pulses and the finish, in 0.96s rather than 2s.
TEST(CombatSimTest, AHoldIsLetGoOnceMorePulsesWouldBuyNothing) {
  Mob snail = MakeMob("Snail", 60);
  CombatParams params = MakeParams(1.0, 0.0, {MakeType(&snail, 0.0, 1)});
  params.attacks.push_back(MakeHeldSwing());

  CombatSim sim;
  EXPECT_DOUBLE_EQ(RunFor(sim, params, 0.95), 0.0);
  EXPECT_DOUBLE_EQ(RunFor(sim, params, 0.02), 100.0);  // 5 x 10, then 50
  EXPECT_TRUE(sim.roster().empty());
}

// The hold is the shelter: what the player takes while the key is down is cut
// by half, and back to full the moment the swing lands.
TEST(CombatSimTest, AHoldShelttersThePlayerWhileItRuns) {
  Mob biter = MakeMob("Biter", 1000000);
  CombatParams params = MakeParams(1.0, 0.0, {MakeType(&biter, 0.0, 1)});
  GivePlayerHp(params, 1000, /*interval=*/0.5, /*damage=*/100.0);
  AttackOption orb = MakeHeldSwing();
  orb.channel.damage_taken_pct = 0.5;
  params.attacks.push_back(std::move(orb));

  CombatSim sim;
  // Three hits land inside the two-second hold, each halved.
  RunFor(sim, params, 1.6);
  EXPECT_EQ(sim.player_hp(), 1000 - 150);
}

// Glacial Fury's half of the pile: magic attack per held stack, and only an
// ice swing collects it.
TEST(CombatSimTest, GlacialFurysMagicAttackRidesTheIceSwingAlone) {
  Mob snail = MakeMob("Snail", 1000);
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 0.0, 1)});
  GiveFreezeStacks(params, /*cap=*/4);
  // No lightning swing, so the pile only ever grows: what is being read is
  // what the ice swing collects for holding it.
  params.attacks.pop_back();
  params.attacks[1].freeze_matt_gain = 0.25;

  CombatSim sim;
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.target_hp_fraction(), 0.99, 1e-9);  // 10, nothing held yet
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.target_hp_fraction(), 0.975, 1e-9);  // 10 x 1.5, two held
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.target_hp_fraction(), 0.955, 1e-9);  // 10 x 2.0, four held
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.target_hp_fraction(), 0.935, 1e-9);  // capped, so no more
}

// Storm Magic's half: final damage while the pile stands, taken whole however
// deep it is rather than climbing with it.
TEST(CombatSimTest, StormMagicStandsOnAnyStackHoweverDeep) {
  Mob snail = MakeMob("Snail", 1000);
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 0.0, 1)});
  GiveFreezeStacks(params, /*cap=*/4);
  // No lightning swing, so nothing ever spends what the ice leaves.
  params.attacks.pop_back();
  params.attacks[1].fd_when_afflicted = 0.5;

  CombatSim sim;
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.target_hp_fraction(), 0.99, 1e-9);  // 10, nothing held yet
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.target_hp_fraction(), 0.975, 1e-9);  // 10 x 1.5, two held
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.target_hp_fraction(), 0.96, 1e-9);  // the same half at four
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.target_hp_fraction(), 0.945, 1e-9);  // and no more at the cap
}

// The share of one mob's HP left, by name -- the queue is shuffled as it fills,
// so the roster's order says nothing about which monster is which.
double LeftOn(const CombatSim& sim, const std::string& name) {
  for (const MobStatus& mob : sim.roster()) {
    if (mob.name == name) {
      return mob.hp_fraction;
    }
  }
  return -1.0;
}

// Shatter's half: what a held stack ignores of the enemy's defence, priced per
// mob type -- against a monster carrying none it buys nothing at all.
TEST(CombatSimTest, ShattersDefenceRideIsPricedPerMobType) {
  Mob armoured = MakeMob("Armoured", 1000);
  Mob bare = MakeMob("Bare", 1000);
  CombatParams params = MakeParams(
      1.0, 1000.0, {MakeType(&armoured, 0.0, 1), MakeType(&bare, 0.0, 1)});
  GiveFreezeStacks(params, /*cap=*/4);
  params.attacks.pop_back();  // no lightning swing, so the pile only grows
  AttackOption& ice = params.attacks[1];
  ice.max_enemies = 2;
  ice.damage_per_hit = {10.0, 10.0};
  ice.freeze_ied_gain = {0.25, 0.0};

  CombatSim sim;
  sim.Advance(params, 1.0);  // 10 apiece, nothing held yet
  sim.Advance(params, 1.0);  // two held: 15 on the armoured one, 10 on the bare
  EXPECT_NEAR(LeftOn(sim, "Armoured"), 0.975, 1e-9);
  EXPECT_NEAR(LeftOn(sim, "Bare"), 0.98, 1e-9);
}

// The ice is the permission and the pile is the amount: a character holding a
// full pile against a monster no swing has frozen collects none of it.
TEST(CombatSimTest, AStackIsWorthNothingOnAMonsterNothingFroze) {
  Mob snail = MakeMob("Snail", 1000);
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 0.0, 1)});
  GiveFreezeStacks(params, /*cap=*/4);
  params.attacks.pop_back();  // no lightning swing, so the pile only grows
  params.attacks[1].freeze_crit_gain = 0.25;
  params.attacks[1].fd_when_afflicted = 0.5;
  params.attacks[1].freeze_seconds = 0.0;  // it makes stacks and no ice

  CombatSim sim;
  for (int step = 0; step < 4; ++step) {
    sim.Advance(params, 1.0);
  }
  // Four swings of a flat 10, however deep the pile got.
  EXPECT_NEAR(sim.target_hp_fraction(), 0.96, 1e-9);
}

// The ice outlives the swing that laid it: everything the character swings
// afterwards collects on it until the monster thaws, which is the whole of the
// alternation. Storm Magic rides the bare poke here for the same reason it
// rides every swing -- it is the character's, not the skill's.
TEST(CombatSimTest, TheIceOutlastsTheSwingThatLaidIt) {
  Mob snail = MakeMob("Snail", 1000000);
  CombatParams params = MakeParams(1.0, 0.0, {MakeType(&snail, 10.0, 1)});
  GiveFreezeStacks(params, /*cap=*/4);
  params.attacks.pop_back();  // no lightning swing to spend the pile
  params.attacks[1].freeze_seconds = 2.5;
  params.attacks[1].cooldown_seconds = 100.0;  // one cast, then the poke
  params.attacks[0].fd_when_afflicted = 1.0;
  params.attacks[1].fd_when_afflicted = 1.0;

  CombatSim sim;
  sim.Advance(params, 1.0);
  EXPECT_DOUBLE_EQ(sim.damage_this_step(), 10.0);  // ice, on a thawed monster
  sim.Advance(params, 1.0);
  EXPECT_DOUBLE_EQ(sim.damage_this_step(), 20.0);  // the poke, on 1.5s of ice
  sim.Advance(params, 1.0);
  EXPECT_DOUBLE_EQ(sim.damage_this_step(), 20.0);  // and on the last 0.5s
  sim.Advance(params, 1.0);
  EXPECT_DOUBLE_EQ(sim.damage_this_step(), 10.0);  // thawed, so a flat poke
}

// The pile is deeper while the buff raising it stands, and the fight reads the
// cap for the buffs standing rather than one number for the whole encounter.
TEST(CombatSimTest, ABuffDeepensThePile) {
  Mob snail = MakeMob("Snail", 1000);
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 0.0, 1)});
  GiveFreezeStacks(params, /*cap=*/2);
  EXPECT_EQ(params.FreezeCap(0), 2);

  AttackSet deeper;
  deeper.attacks = params.attacks;
  deeper.freeze_cap = 10;
  params.buffed.push_back(std::move(deeper));
  EXPECT_EQ(params.FreezeCap(1), 10);
  // Out of range is no buffs at all, exactly as the attack tables read.
  EXPECT_EQ(params.FreezeCap(2), 2);
}

// A boss fight is the roster it opened with: nothing refills, and an emptied
// queue stays empty however long the fight runs on.
TEST(CombatSimTest, NoRespawnSecondsMeansNothingComesBack) {
  Mob mob = MakeMob("Arm", 10);
  CombatParams params = MakeParams(1.0, 0.0, {MakeType(&mob, 100.0, 2)});
  CombatSim sim;
  for (int i = 0; i < 100; ++i) {
    sim.Advance(params, 1.0);
  }
  EXPECT_TRUE(sim.respawning());
  EXPECT_TRUE(sim.roster().empty());
}

// The roster is one entry per mob rather than the merged window, and each
// entry keeps its id as the ones beside it die -- what pins one of Zakum's
// arms to one panel.
TEST(CombatSimTest, TheRosterHoldsEveryMobAndKeepsItsIds) {
  Mob mob = MakeMob("Arm", 100, 110);
  CombatParams params = MakeParams(1.0, 0.0, {MakeType(&mob, 60.0, 3)});
  CombatSim sim;
  sim.Advance(params, 0.0);
  ASSERT_EQ(sim.roster().size(), 3u);
  EXPECT_EQ(sim.roster()[0].name, "Arm");
  EXPECT_DOUBLE_EQ(sim.roster()[1].hp_fraction, 1.0);
  std::vector<int> ids;
  for (const MobStatus& status : sim.roster()) {
    ids.push_back(status.id);
  }
  EXPECT_EQ(std::set<int>(ids.begin(), ids.end()).size(), 3u);

  // Two swings kill the front mob and a third wounds the next; the survivors
  // keep the ids they had.
  for (int i = 0; i < 3; ++i) {
    sim.Advance(params, 1.0);
  }
  ASSERT_EQ(sim.roster().size(), 2u);
  EXPECT_EQ(sim.roster()[0].id, ids[1]);
  EXPECT_EQ(sim.roster()[1].id, ids[2]);
  EXPECT_LT(sim.roster()[0].hp_fraction, 1.0);
  EXPECT_DOUBLE_EQ(sim.roster()[1].hp_fraction, 1.0);
}

// The encounter name is what the fight watches to know it is somewhere else,
// so a boss phase turning over rebuilds the roster the way a map change does.
TEST(CombatSimTest, ANewEncounterNameRefillsTheQueue) {
  Mob arm = MakeMob("Arm", 100);
  Mob body = MakeMob("Body", 500);
  CombatParams first = MakeParams(1.0, 0.0, {MakeType(&arm, 1000.0, 1)});
  CombatSim sim;
  sim.Advance(first, 1.0);
  EXPECT_TRUE(sim.roster().empty());

  CombatParams second =
      MakeParams(1.0, 0.0, {MakeType(&body, 10.0, 1)}, 1, "phase2");
  sim.Advance(second, 0.0);
  ASSERT_EQ(sim.roster().size(), 1u);
  EXPECT_EQ(sim.roster()[0].name, "Body");
}

// The record is off unless it is asked for: the sims step the fight millions
// of times and draw none of it.
TEST(CombatSimTest, NothingIsRecordedUnlessItIsAskedFor) {
  Mob mob = MakeMob("Snail", 1000000);
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&mob, 25.0, 1)});
  CombatSim sim;
  sim.Advance(params, 1.0);
  EXPECT_TRUE(sim.damage_lines_this_step().empty());
}

// The whole contract of the record: one line per hit, all of one swing on one
// monster under one event, against the monster that actually took it, and
// summing to what that monster lost.
TEST(CombatSimTest, ASwingIsRecordedLineByLine) {
  Mob mob = MakeMob("Snail", 1000000);
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&mob, 25.0, 1)});
  params.record_damage_lines = true;
  HitGroup group;
  group.damage = {25.0};
  group.rolls.lines = 4;
  group.rolls.mastery = 0.4;
  group.rolls.crit_rate = 0.5;
  group.rolls.crit_dmg = 1.0;
  params.attacks[0].groups.push_back(group);

  CombatSim sim;
  sim.Advance(params, 1.0);
  double before = sim.target_hp_fraction();
  sim.Advance(params, 1.0);

  const std::vector<DamageLine>& lines = sim.damage_lines_this_step();
  ASSERT_EQ(lines.size(), 4u);
  ASSERT_EQ(sim.roster().size(), 1u);
  double total = 0.0;
  for (const DamageLine& line : lines) {
    EXPECT_EQ(line.mob_id, sim.roster()[0].id);
    EXPECT_EQ(line.event, lines[0].event);
    EXPECT_GT(line.damage, 0.0);
    total += line.damage;
  }
  EXPECT_NEAR(total, (before - sim.target_hp_fraction()) * mob.max_hp(), 1e-6);
}

// Two monsters, one swing: each keeps its own stack, so the numbers can be
// drawn where the damage landed.
TEST(CombatSimTest, EachMonsterOfASwingGetsItsOwnEvent) {
  Mob mob = MakeMob("Snail", 1000000);
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&mob, 25.0, 2)}, 2);
  params.record_damage_lines = true;
  CombatSim sim;
  sim.Advance(params, 1.0);
  sim.Advance(params, 1.0);

  const std::vector<DamageLine>& lines = sim.damage_lines_this_step();
  ASSERT_EQ(lines.size(), 2u);
  EXPECT_NE(lines[0].mob_id, lines[1].mob_id);
  EXPECT_NE(lines[0].event, lines[1].event);
}

// A crit is told apart from a plain line, which is the only thing the colour
// of a number depends on.
TEST(CombatSimTest, ACritIsRecordedAsOne) {
  Mob mob = MakeMob("Snail", 100000000);
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&mob, 25.0, 1)});
  params.record_damage_lines = true;
  HitGroup group;
  group.damage = {25.0};
  group.rolls.lines = 8;
  group.rolls.mastery = 1.0;
  group.rolls.crit_rate = 0.5;
  group.rolls.crit_dmg = 1.0;
  params.attacks[0].groups.push_back(group);

  CombatSim sim;
  bool crit_seen = false;
  bool plain_seen = false;
  for (int step = 0; step < 20; ++step) {
    sim.Advance(params, 1.0);
    for (const DamageLine& line : sim.damage_lines_this_step()) {
      crit_seen = crit_seen || line.crit;
      plain_seen = plain_seen || !line.crit;
      // Nothing spreads, so a plain line is the swing's 25 over its eight
      // lines and the half-again the crit rate already averaged in, and a
      // crit is exactly twice that.
      double plain = 25.0 / (8 * 1.5);
      EXPECT_NEAR(line.damage, line.crit ? 2 * plain : plain, 1e-9);
    }
  }
  EXPECT_TRUE(crit_seen);
  EXPECT_TRUE(plain_seen);
}

// What did the damage rides every line, so a caller drawing them can tell a
// swing from what fires beside it.
TEST(CombatSimTest, EveryLineSaysWhatDidIt) {
  Mob mob = MakeMob("Snail", 1000000);
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&mob, 25.0, 1)});
  params.record_damage_lines = true;
  params.dot_count = 1;
  DotApplication burn;
  burn.damage = {40.0};
  burn.interval_seconds = 0.25;
  burn.duration_seconds = 10.0;
  burn.slot = 0;
  params.attacks[0].dots.push_back(burn);
  AttackOption summon;
  summon.name = "Summon";
  summon.interval_seconds = 0.5;
  summon.damage_per_hit = {10.0};
  params.auto_attacks.push_back(summon);

  CombatSim sim;
  sim.Advance(params, 1.0);  // the swing lights the burn
  sim.Advance(params, 1.0);  // and everything lands together

  bool swing = false;
  bool own_clock = false;
  bool burned = false;
  for (const DamageLine& line : sim.damage_lines_this_step()) {
    swing = swing || line.source == DamageSource{DamageOrigin::kSwing, 0};
    own_clock =
        own_clock || line.source == DamageSource{DamageOrigin::kOwnClock, 0};
    burned = burned || line.source == DamageSource{DamageOrigin::kBurn, 0};
  }
  EXPECT_TRUE(swing);
  EXPECT_TRUE(own_clock);
  EXPECT_TRUE(burned);
}

// A burn ticks between the swings rather than with one, so it is its own
// landing and gets its own stack of numbers.
TEST(CombatSimTest, ABurnTickIsItsOwnEvent) {
  Mob mob = MakeMob("Snail", 1000000);
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&mob, 25.0, 1)});
  params.record_damage_lines = true;
  params.dot_count = 1;
  DotApplication burn;
  burn.damage = {40.0};
  burn.interval_seconds = 0.25;
  burn.duration_seconds = 10.0;
  burn.slot = 0;
  params.attacks[0].dots.push_back(burn);

  CombatSim sim;
  sim.Advance(params, 1.0);  // the swing lights it
  sim.Advance(params, 0.6);  // two ticks, no swing

  const std::vector<DamageLine>& lines = sim.damage_lines_this_step();
  ASSERT_EQ(lines.size(), 2u);
  EXPECT_NE(lines[0].event, lines[1].event);
  EXPECT_DOUBLE_EQ(lines[0].damage, 40.0);
  EXPECT_DOUBLE_EQ(lines[1].damage, 40.0);
}

// Damage is counted whether or not the lines are being recorded, and it counts
// what the swing rolled rather than what the mob had left.
TEST(CombatSimTest, DamageThisStepCountsOverkill) {
  Mob mob = MakeMob("Snail", 10);
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&mob, 250.0, 1)});

  CombatSim sim;
  sim.Advance(params, 1.0);
  EXPECT_DOUBLE_EQ(sim.damage_this_step(), 250.0);
  EXPECT_EQ(sim.kills_this_step()[0], 1);

  // A step that swings at nothing does no damage, and the count does not
  // carry over from the step that did.
  sim.Advance(params, 0.1);
  EXPECT_DOUBLE_EQ(sim.damage_this_step(), 0.0);
}

// A burn ticking between swings is damage the character dealt.
TEST(CombatSimTest, DamageThisStepCountsBurnTicks) {
  Mob mob = MakeMob("Snail", 1000000);
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&mob, 25.0, 1)});
  params.dot_count = 1;
  DotApplication burn;
  burn.damage = {40.0};
  burn.interval_seconds = 0.25;
  burn.duration_seconds = 10.0;
  burn.slot = 0;
  params.attacks[0].dots.push_back(burn);

  CombatSim sim;
  sim.Advance(params, 1.0);  // the swing lights it
  sim.Advance(params, 0.6);  // two ticks, no swing
  EXPECT_DOUBLE_EQ(sim.damage_this_step(), 80.0);
}

// The beat is flagged on the step it comes round, and on no other.
TEST(CombatSimTest, TheRespawnBeatIsFlaggedOnItsStep) {
  Mob mob = MakeMob("Snail", 100);
  CombatParams params = MakeParams(1.0, 2.0, {MakeType(&mob, 10.0, 1)});

  CombatSim sim;
  sim.Advance(params, 0.5);
  EXPECT_FALSE(sim.respawned_this_step());
  sim.Advance(params, 1.0);
  EXPECT_FALSE(sim.respawned_this_step());
  sim.Advance(params, 1.0);
  EXPECT_TRUE(sim.respawned_this_step());
  sim.Advance(params, 0.5);
  EXPECT_FALSE(sim.respawned_this_step());
}

}  // namespace
}  // namespace ms
