#include "src/combat/fight.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "src/combat/damage_ledger.h"
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

// A mob type with the damage one attack does to it. Damage is stored on the
// attack, not the type, but tests read better with the two side by side.
struct TypeSpec {
  const Mob* mob = nullptr;
  double damage = 0.0;
  int simultaneous = 0;
};

TypeSpec MakeType(const Mob* mob, double damage, int simultaneous) {
  return TypeSpec{mob, damage, simultaneous};
}

// Params with a single attack of the given reach, the common case. Tests that
// need a choice of attacks push more onto params.attacks.
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

// Whether the roll is wide or narrow, a rolled attack must kill at the rate its
// average would. The only cost of rolling is overkill on the killing blow,
// which against a mob eighty attacks deep is a fraction of one kill.
TEST(CombatSimTest, ARollingSwingKillsAtTheRateItsAverageWould) {
  // Many mobs and no respawn, so damage limits the kills. The HP takes many
  // attacks and isn't a multiple of one: a mob dying on an exact attack count
  // would lose a whole attack to the smallest jitter.
  Mob mob = MakeMob("Snail", 2013);
  double kills[3] = {0.0, 0.0, 0.0};
  // Enough kills to compare rates. No more: a roll is weighed at its average,
  // not sampled, so a longer run adds no certainty.
  constexpr int kSteps = 5000;
  for (int run = 0; run < 3; ++run) {
    CombatParams params = MakeParams(1.0, 1e9, {MakeType(&mob, 25.0, 400)});
    if (run > 0) {
      // Four lines of a quarter of the damage each: 25 on average either way.
      // Run 1 rolls wide with many crits, run 2 barely varies.
      HitGroup group;
      group.damage = {25.0};
      group.rolls.lines = 4;
      group.rolls.mastery = run == 1 ? 0.4 : 0.99;
      group.rolls.crit_rate = run == 1 ? 0.5 : 0.0;
      group.rolls.crit_dmg = 1.0;
      params.attacks[0].groups.push_back(group);
    }
    CombatSim sim;
    for (int step = 0; step < kSteps; ++step) {
      sim.Advance(params, 1.0);
      kills[run] += sim.view().kills_this_step[0];
    }
  }
  ASSERT_GT(kills[0], 0.0);
  EXPECT_NEAR(kills[1] / kills[0], 1.0, 0.01);
  EXPECT_NEAR(kills[2] / kills[0], 1.0, 0.01);
}

// The same attack lands differently twice. Without this, the roll could be a
// constant and every average above would still hold.
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
    left.push_back(sim.view().target_hp_fraction);
  }
  // Five attacks, so four gaps; at least one must differ from the first.
  bool varied = false;
  for (std::size_t i = 2; i < left.size(); ++i) {
    if (std::abs((left[i - 1] - left[i]) - (left[0] - left[1])) > 1e-9) {
      varied = true;
    }
  }
  EXPECT_TRUE(varied);
}

// A Final Attack is a chance, not a fraction of a hit. It rolls once per enemy
// the attack reached, and over a long run must pay what the fraction did.
TEST(CombatSimTest, AFinalAttackRollsPerEnemyAndPaysItsAverage) {
  Mob mob = MakeMob("Snail", 2013);
  double kills[2] = {0.0, 0.0};
  constexpr int kSteps = 5000;
  for (int run = 0; run < 2; ++run) {
    CombatParams params = MakeParams(1.0, 1e9, {MakeType(&mob, 20.0, 400)});
    params.attacks[0].final_attack_damage = {5.0};
    if (run == 1) {
      // A quarter chance of a hit worth four times as much: 5 on average, and
      // mostly nothing.
      FinalAttackRoll roll;
      roll.chance = 0.25;
      roll.damage = {20.0};
      params.attacks[0].final_attack_rolls.push_back(roll);
    }
    CombatSim sim;
    for (int step = 0; step < kSteps; ++step) {
      sim.Advance(params, 1.0);
      kills[run] += sim.view().kills_this_step[0];
    }
  }
  ASSERT_GT(kills[0], 0.0);
  EXPECT_NEAR(kills[1] / kills[0], 1.0, 0.01);
}

// The burn a hand-built attack leaves: the one slot such an attack needs, the
// damage per tick to one mob type, and its timing.
DotApplication MakeBurn(double damage, double interval, double duration) {
  DotApplication burn;
  burn.slot = 0;
  burn.damage.assign(1, damage);
  burn.interval_seconds = interval;
  burn.duration_seconds = duration;
  return burn;
}

// A burn is worth what it can sustain: reapplied every second, it gets one tick
// per second however long it lasts. Priced in full, the fight would pick it
// over something five times better.
TEST(CombatSimTest, ABurnIsWeighedAtTheRateItCanBeRelit) {
  Mob mob = MakeMob("Snail", 100);
  CombatParams params = MakeParams(1.0, 1e9, {MakeType(&mob, 100.0, 40)});
  // Beside the strong attack, a weak one leaving a burn that lasts half a
  // minute. Reapplied every second, it's worth 10 per second, not 300 per
  // attack.
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
    killed += sim.view().kills_this_step[0];
  }
  // The strong attack kills one per second; the weak one would kill one every
  // five. Anything near that means the burn was priced at its full duration.
  EXPECT_GT(killed, 20);
}

// Relighting a standing burn adds nothing, so the fight uses the stronger
// attack until the burn nears its end.
TEST(CombatSimTest, ABurnAlreadyStandingIsNotWorthRelighting) {
  Mob mob = MakeMob("Snail", 1000000);
  CombatParams params = MakeParams(1.0, 1e9, {MakeType(&mob, 300.0, 1)});
  // An attack that deals nothing itself and leaves a burn worth 400 per tick
  // for ten seconds: worth applying, and worth nothing to reapply.
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
  double taken = (1.0 - sim.view().target_hp_fraction) * 1000000.0;
  // Thirty seconds of burning is 12000 whichever attack is used, so anything
  // above that is the stronger attack landing in between.
  EXPECT_GT(taken, 16000.0);
}

// A poison with room for another stack is still worth reapplying, so the fight
// keeps reapplying until it's full, and only then uses another attack.
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
    taken[run] = (1.0 - sim.view().target_hp_fraction) * 1000000.0;
  }
  // Three stacks burn three times as much as one, and the fight only gets them
  // by spending attacks the single-stack run had no reason to.
  EXPECT_GT(taken[1], taken[0] * 1.5);
}

// A burn counts as an affliction just as a freeze does. GMS lists the same five
// conditions on Storm Magic and Burning Magic, and the F/P's version is the
// burn. Nothing is frozen here, and the bonus still applies.
TEST(CombatSimTest, ABurnAfflictsTheMonsterTheIceWouldHave) {
  Mob mob = MakeMob("Snail", 1000000);
  CombatParams params = MakeParams(1.0, 1e9, {MakeType(&mob, 100.0, 1)});
  params.dot_count = 1;
  params.attacks[0].fd_when_afflicted = 0.5;

  CombatSim cold;
  cold.Advance(params, 1.0);
  EXPECT_DOUBLE_EQ(cold.view().damage_this_step, 100.0);  // nothing on it yet

  params.attacks[0].dots.push_back(MakeBurn(0.0, 1.0, 10.0));
  CombatSim lit;
  lit.Advance(params, 1.0);
  EXPECT_DOUBLE_EQ(lit.view().damage_this_step,
                   100.0);  // the swing that lights it
  lit.Advance(params, 1.0);
  EXPECT_DOUBLE_EQ(lit.view().damage_this_step, 150.0);  // and every one after
}

// Elemental Drain counts burns on the whole group, not just the enemy being
// hit: eight monsters with one each count as eight. The count is capped, and a
// rate with no cap gives nothing.
TEST(CombatSimTest, TheDrainCountsEveryBurningMonsterUpToItsCap) {
  Mob mob = MakeMob("Snail", 1000000);
  CombatParams params = MakeParams(1.0, 1e9, {MakeType(&mob, 100.0, 8)}, 8);
  params.dot_count = 1;
  params.attacks[0].dots.push_back(MakeBurn(0.0, 1.0, 10.0));
  params.attacks[0].fd_per_dot = 0.1;

  CombatSim uncounted;
  uncounted.Advance(params, 1.0);
  uncounted.Advance(params, 1.0);
  EXPECT_DOUBLE_EQ(uncounted.view().damage_this_step, 800.0)
      << "no cap, no count";

  params.attacks[0].dot_count_cap = 3;
  CombatSim capped;
  capped.Advance(params, 1.0);
  EXPECT_DOUBLE_EQ(capped.view().damage_this_step, 800.0);  // lights them
  capped.Advance(params, 1.0);
  // Eight burning, three counted: 8 x 100 x 1.3.
  EXPECT_DOUBLE_EQ(capped.view().damage_this_step, 1040.0);
}

// A burn ticks on its own clock for its duration and then stops. It isn't a
// second attack that runs forever.
TEST(CombatSimTest, ABurnTicksForItsDurationAndNoLonger) {
  Mob mob = MakeMob("Snail", 10000);
  // A slow attack, so one cast's burn ends well before the next one applies it.
  CombatParams params = MakeParams(10.0, 1e9, {MakeType(&mob, 0.0, 1)});
  params.dot_count = 1;
  params.attacks[0].dots.push_back(MakeBurn(100.0, 1.0, 5.0));

  CombatSim sim;
  // Through the first cast at 10s and its five ticks.
  for (int step = 0; step < 32; ++step) {
    sim.Advance(params, 0.5);
  }
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.95, 1e-9);
  // Six more seconds with the burn over and the next cast not yet due.
  for (int step = 0; step < 8; ++step) {
    sim.Advance(params, 0.5);
  }
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.95, 1e-9)
      << "the burn kept ticking past the seconds it was lit for";
}

// Reapplying a burn refreshes it instead of adding a second one, so attacking
// twice as often gains nothing. It also must not reset the tick timer, or an
// attack faster than the interval would keep it from ever ticking.
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
    left[run] = sim.view().target_hp_fraction;
  }
  // Forty seconds of burning either way, within the one tick the faster run's
  // earlier first attack gains.
  EXPECT_LT(left[0], 1.0);
  EXPECT_NEAR(left[0], left[1], 100.0 / 100000.0 + 1e-9);
}

// A triggered strike has its own cooldown, and fires only with the attack that
// carries it.
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
  // Fifty seconds of one-second attacks: ten strikes at a five-second cooldown,
  // and the attack itself deals nothing.
  double taken = (1.0 - sim.view().target_hp_fraction) * 1000000.0;
  EXPECT_NEAR(taken, 10.0 * 1000.0, 1000.0);
}

// A poison stacks up to its limit and no further, each stack ticking for full
// damage. Three are worth three times one, and attacks after the third only
// refresh the duration.
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
    taken[run] = 1.0 - sim.view().target_hp_fraction;
  }
  // An attack per second fills the stacks in three seconds, so all but the
  // first two of a hundred seconds tick three times.
  EXPECT_GT(taken[0], 0.0);
  EXPECT_NEAR(taken[1] / taken[0], 3.0, 0.05);
}

// A poison is rolled per enemy the attack reached, so half of them burn. The
// test only checks that the roll reduces the burn; a poison that always applied
// would be the burn above.
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
  // One tick per attack per enemy at 100%, half as many at a 50% chance. The
  // bounds are loose: the test catches a roll that never fires or never misses,
  // not the shape of the distribution.
  double burned = (1.0 - sim.view().target_hp_fraction) * 1000000.0;
  EXPECT_GT(burned, 100.0 * 200.0 * 0.3);
  EXPECT_LT(burned, 100.0 * 200.0 * 0.7);
}

// A burn uses the character's damage at the moment it was applied, and keeps
// it. A buff ending halfway through doesn't weaken a burn already running.
TEST(CombatSimTest, ABurnKeepsTheDamageItWasLitWith) {
  Mob mob = MakeMob("Snail", 10000);
  CombatParams params = MakeParams(10.0, 1e9, {MakeType(&mob, 0.0, 1)});
  params.dot_count = 1;
  params.attacks[0].dots.push_back(MakeBurn(100.0, 1.0, 5.0));

  CombatSim sim;
  for (int step = 0; step < 23; ++step) {
    sim.Advance(params, 0.5);  // the cast at 10s, and one tick after it
  }
  ASSERT_NEAR(sim.view().target_hp_fraction, 0.99, 1e-9);
  // Whatever the character is worth now, the four remaining ticks were priced
  // when the burn landed.
  params.attacks[0].dots[0].damage.assign(1, 1.0);
  for (int step = 0; step < 10; ++step) {
    sim.Advance(params, 0.5);
  }
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.95, 1e-9);
}

// Blizzard's passive hits one enemy however many the attack reached, so four
// times the reach isn't four times the Final Attack. The ordinary bucket beside
// it scales with reach, which is how the two differ.
TEST(CombatSimTest, ASingleEnemyFinalAttackDoesNotScaleWithTheReach) {
  // One Final Attack hit kills outright, so kills count the hits.
  Mob mob = MakeMob("Snail", 100);
  int64_t killed[2][2] = {{0, 0}, {0, 0}};
  for (int single = 0; single < 2; ++single) {
    for (int reach = 1; reach <= 4; reach += 3) {
      CombatParams params =
          MakeParams(1.0, 1e9, {MakeType(&mob, 0.0, 40)}, reach);
      std::vector<double>& bank =
          single == 1 ? params.attacks[0].per_swing_final_attack_damage
                      : params.attacks[0].final_attack_damage;
      bank.assign(params.types.size(), 100.0);
      CombatSim sim;
      for (int step = 0; step < 5; ++step) {
        sim.Advance(params, 1.0);
        killed[single][reach == 1 ? 0 : 1] += sim.view().kills_this_step[0];
      }
    }
  }
  ASSERT_GT(killed[0][0], 0);
  // The ordinary bucket pays per enemy reached; the single-enemy one doesn't.
  EXPECT_EQ(killed[0][1], 4 * killed[0][0]);
  EXPECT_EQ(killed[1][1], killed[1][0]);
}

// Split Shot's shape: rolled once per attack like Blizzard's, but hitting its
// own number of enemies. Four enemies behind an attack that reaches one, and
// still four behind one that reaches four: the reach is the follow-up's own.
TEST(CombatSimTest, AFinalAttackWithItsOwnReachIgnoresTheSwings) {
  // One follow-up hit kills outright, so kills count where it landed.
  Mob mob = MakeMob("Snail", 100);
  // Its own reach, and the attack's: one enemy behind a narrow attack, four
  // behind the same attack, and four behind an attack four times as wide.
  const int kOwn[] = {1, 4, 4};
  const int kSwing[] = {1, 1, 4};
  int64_t killed[3] = {0, 0, 0};
  for (int i = 0; i < 3; ++i) {
    CombatParams params =
        MakeParams(1.0, 1e9, {MakeType(&mob, 0.0, 40)}, kSwing[i]);
    params.attacks[0].per_swing_final_attack_damage.assign(params.types.size(),
                                                           100.0);
    params.attacks[0].per_swing_final_attack_enemies = kOwn[i];
    CombatSim sim;
    for (int step = 0; step < 5; ++step) {
      sim.Advance(params, 1.0);
      killed[i] += sim.view().kills_this_step[0];
    }
  }
  ASSERT_GT(killed[0], 0);
  EXPECT_EQ(killed[1], 4 * killed[0]) << "the follow-up ignored its own reach";
  EXPECT_EQ(killed[2], killed[1]) << "the follow-up took the swing's reach";
}

// Each enemy after the first takes 15% more than the last, compounding: with
// six, the last takes 1.15^5, the doubling GMS lists beside the 15%.
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
  ASSERT_EQ(sim.view().engaged_groups.size(), 6u);
  std::vector<double> lost;
  for (const EngagedGroup& group : sim.view().engaged_groups) {
    lost.push_back((1.0 - group.hp_fraction) * 10000.0);
  }
  std::sort(lost.begin(), lost.end());
  for (int step = 0; step < 6; ++step) {
    EXPECT_NEAR(lost[step], 100.0 * std::pow(1.15, step), 1e-6)
        << "step " << step;
  }
}

// The rate used to choose an attack includes the gain too, averaged over the
// mobs it would reach. Six mobs make the piercing attack worth 8.75 hits
// instead of 6, which is why it's picked over a flatter, harder one.
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
  // Harder on every mob but with no gain: 840 per attack against the arrow's
  // 875, so it wins only if the gain is ignored.
  AttackOption flat;
  flat.name = "Bolt Burst";
  flat.max_enemies = 6;
  flat.swing_seconds = 1.0;
  flat.damage_per_hit.assign(6, 140.0);
  params.attacks.push_back(std::move(flat));

  CombatSim sim;
  sim.Advance(params, 0.1);
  EXPECT_EQ(sim.view().attack_name, "Piercing Arrow");

  // Without the gain the flatter attack wins, which shows the choice above was
  // made on the gain.
  params.attacks[0].pierce_gain_pct = 0.0;
  CombatSim without;
  without.Advance(params, 0.1);
  EXPECT_EQ(without.view().attack_name, "Bolt Burst");
}

// A boss's parts don't respawn, so a narrow attack must target what will
// outlast it: four parts with three in reach clear in six attacks by picking
// the healthiest, eight in queue order.
TEST(CombatSimTest, ABossSwingPicksTheHealthiestOfTheRoster) {
  std::vector<Mob> mobs;
  for (int i = 0; i < 4; ++i) {
    mobs.push_back(MakeMob("Part" + std::to_string(i), 1000));
  }
  std::vector<TypeSpec> specs;
  for (const Mob& mob : mobs) {
    specs.push_back(MakeType(&mob, 250.0, 1));
  }
  CombatParams params = MakeParams(1.0, 1e9, specs, /*reach=*/3);
  std::vector<int> swings;
  for (bool focus : {true, false}) {
    params.focus_healthiest = focus;
    CombatSim sim;
    int taken = 0;
    while (taken < 20 && (taken == 0 || !sim.view().roster.empty())) {
      sim.Advance(params, 1.0);
      ++taken;
    }
    swings.push_back(taken);
  }
  EXPECT_EQ(swings[0], 6);
  EXPECT_EQ(swings[1], 8);
}

// The order the arrow meets enemies is random each time, so the gain doesn't
// always fall on the same end of the queue. Otherwise the front mob would take
// the plain hit on every attack.
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
    first.push_back(sim.view().engaged_groups[0].hp_fraction);
  }
  // Twenty attacks, so nineteen gaps: the front mob can't have taken the same
  // share every time.
  bool varied = false;
  for (std::size_t i = 2; i < first.size(); ++i) {
    if (std::abs((first[i - 1] - first[i]) - (first[0] - first[1])) > 1e-12) {
      varied = true;
    }
  }
  EXPECT_TRUE(varied);
}

// An attack worth three basic attacks, with a three-second cooldown, so it
// lands once every four attacks.
AttackOption MakeBurst() {
  AttackOption burst;
  burst.name = "Burst";
  burst.max_enemies = 1;
  burst.damage_per_hit = {30.0};
  burst.swing_seconds = 1.0;
  burst.cooldown_seconds = 3.0;
  return burst;
}

// A learned attack, stronger than the basic attack, with a short cooldown after
// it lands so something else has to fill the gap.
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
// each every `interval` seconds.
void AddAutoAttack(CombatParams& params, double interval, double damage,
                   int reach = 1) {
  AttackOption cast;
  cast.name = "Evil Eye Shock";
  cast.max_enemies = reach;
  cast.interval_seconds = interval;
  cast.damage_per_hit.assign(params.types.size(), damage);
  params.auto_attacks.push_back(std::move(cast));
}

// Adds a skill timed by attacks landed instead of seconds, hitting `reach` mobs
// for `damage` each every `attacks` attacks.
void AddTriggeredAttack(CombatParams& params, int attacks, double damage,
                        int reach = 1) {
  AttackOption cast;
  cast.name = "Speed Mirage";
  cast.max_enemies = reach;
  cast.attacks_per_cast = attacks;
  cast.damage_per_hit.assign(params.types.size(), damage);
  params.triggered_attacks.push_back(std::move(cast));
}

// Adds a skill timed by enemies defeated, hitting `reach` mobs for `damage`
// each every `kills` kills.
void AddKillClockedAttack(CombatParams& params, int kills, double damage,
                          int reach = 1) {
  AttackOption cast;
  cast.name = "Erda Fountain";
  cast.max_enemies = reach;
  cast.kills_per_cast = kills;
  cast.damage_per_hit.assign(params.types.size(), damage);
  params.triggered_attacks.push_back(std::move(cast));
}

// Gives `attack` a bigger form that replaces every `every`th use, hitting
// `reach` mobs for `damage`. With `marks`, the count runs per mob struck and
// the form lands on top of the triggering strike.
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
  EXPECT_TRUE(sim.view().target_name.empty());
}

TEST(CombatSimTest, ChargesAttackBarThenLandsAHit) {
  Mob snail = MakeMob("Snail", 10);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 100.0, {MakeType(&snail, 4.0, 1)});

  sim.Advance(params, 0.5);
  EXPECT_TRUE(sim.active());
  EXPECT_EQ(sim.view().target_name, "Snail");
  EXPECT_NEAR(sim.view().attack_fraction, 0.5, 1e-9);
  EXPECT_DOUBLE_EQ(sim.view().target_hp_fraction, 1.0);  // no hit landed yet

  sim.Advance(params, 0.5);  // swing completes -> one hit
  EXPECT_NEAR(sim.view().attack_fraction, 0.0, 1e-9);
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.6, 1e-9);  // 10 - 4 = 6
}

// A step longer than the attack lands every attack it covers, instead of one
// plus a growing remainder. The remainder once kept the charge bar stuck full
// when a 120ms key-down skill ran under the TUI's 150ms frame.
TEST(CombatSimTest, AStepWiderThanTheSwingLandsEverySwingItCovers) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(0.6, 100.0, {MakeType(&snail, 1.0, 1)});
  // A 120ms key-down skill beside the basic attack, worth more per second so
  // it's chosen. The basic attack stays the slow one, since the step is clamped
  // against it.
  AttackOption fast = params.attacks.front();
  fast.swing_seconds = 0.12;
  fast.damage_per_hit[0] = 10.0;
  params.attacks.push_back(std::move(fast));

  sim.Advance(params, 0.15);  // one swing lands, 30ms carries
  EXPECT_NEAR(sim.view().attack_fraction, 0.25, 1e-9);
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.99, 1e-9);

  sim.Advance(params, 0.6);  // the 30ms plus 600ms is five more
  EXPECT_NEAR(sim.view().attack_fraction, 0.25, 1e-9);
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.94, 1e-9);
}

// The same rule for a skill on its own clock, which RunDots and RunRegen
// already followed.
TEST(CombatSimTest, AStepWiderThanTheIntervalFiresEveryCastItCovers) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(10.0, 100.0, {MakeType(&snail, 0.0, 1)});
  AddAutoAttack(params, /*interval=*/0.2, /*damage=*/10.0);

  sim.Advance(params, 1.0);  // five casts, not one
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.95, 1e-9);
}

TEST(CombatSimTest, KillingTheLastMobEntersRespawning) {
  Mob snail = MakeMob("Snail", 10);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 100.0, {MakeType(&snail, 10.0, 1)});

  sim.Advance(params, 1.0);  // one-shot the only mob
  EXPECT_TRUE(sim.respawning());
  EXPECT_TRUE(sim.view().target_name.empty());
}

TEST(CombatSimTest, ReportsTheTargetLevelOnlyWhileFighting) {
  Mob snail = MakeMob("Snail", 10, 5);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 100.0, {MakeType(&snail, 10.0, 1)});

  sim.Advance(params, 0.5);  // mid-swing, mob still up
  EXPECT_EQ(sim.view().target_level, 5);

  sim.Advance(params, 0.5);  // swing lands, one-shots the only mob
  EXPECT_TRUE(sim.respawning());
  EXPECT_EQ(sim.view().target_level, 0);
}

TEST(CombatSimTest, RecordsKillsForTheStepTheyHappen) {
  Mob snail = MakeMob("Snail", 10);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 100.0, {MakeType(&snail, 10.0, 2)});

  sim.Advance(params, 0.5);  // still charging, no kill yet
  ASSERT_EQ(sim.view().kills_this_step.size(), 1u);
  EXPECT_EQ(sim.view().kills_this_step[0], 0);

  sim.Advance(params, 0.5);  // swing completes -> one kill
  EXPECT_EQ(sim.view().kills_this_step[0], 1);

  sim.Advance(params, 0.5);  // still charging the next swing, no new kill
  EXPECT_EQ(sim.view().kills_this_step[0], 0);
}

TEST(CombatSimTest, AdvancesToTheNextMobAfterAKill) {
  Mob snail = MakeMob("Snail", 10);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 100.0, {MakeType(&snail, 10.0, 2)});

  sim.Advance(params, 1.0);  // kill first of two
  EXPECT_FALSE(sim.respawning());
  EXPECT_DOUBLE_EQ(sim.view().target_hp_fraction, 1.0);  // next mob, full HP

  sim.Advance(params, 0.9);  // charging, no swing yet
  EXPECT_DOUBLE_EQ(sim.view().target_hp_fraction, 1.0);

  sim.Advance(params, 1.0);  // kill the second -> queue empty
  EXPECT_TRUE(sim.respawning());
}

TEST(CombatSimTest, ASingleTargetSwingSparesTheSecond) {
  Mob snail = MakeMob("Snail", 10);
  CombatSim sim;
  // Reach 1 (the default): an attack hits only the front mob, so the second is
  // still at full HP after the first dies.
  CombatParams params = MakeParams(1.0, 100.0, {MakeType(&snail, 10.0, 2)});

  sim.Advance(params, 1.0);  // one-shot the front mob
  EXPECT_EQ(sim.view().kills_this_step[0], 1);
  EXPECT_DOUBLE_EQ(sim.view().target_hp_fraction,
                   1.0);  // the next is full, unhit
}

TEST(CombatSimTest, MultiTargetSwingHitsAndKillsSeveralAtOnce) {
  Mob snail = MakeMob("Snail", 10);
  CombatSim sim;
  // A 3-target attack over three mobs: one attack kills all three.
  CombatParams params =
      MakeParams(1.0, 100.0, {MakeType(&snail, 10.0, 3)}, /*reach=*/3);

  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.view().kills_this_step[0], 3);  // three kills on one swing
  EXPECT_TRUE(sim.respawning());                // queue cleared
}

TEST(CombatSimTest, MultiTargetReachIsCappedByRemainingMobs) {
  Mob snail = MakeMob("Snail", 10);
  CombatSim sim;
  // Reach 6 but only two mobs are up, so the attack hits (and kills) just two.
  CombatParams params =
      MakeParams(1.0, 100.0, {MakeType(&snail, 10.0, 2)}, /*reach=*/6);

  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.view().kills_this_step[0], 2);
  EXPECT_TRUE(sim.respawning());
}

TEST(CombatSimTest, MultiTargetDrainsTheWindowInParallel) {
  Mob snail = MakeMob("Snail", 10);
  CombatSim sim;
  // Two mobs, a 2-target attack, 6 damage: each needs two hits. The first
  // attack leaves both damaged; the second kills both at once.
  CombatParams params =
      MakeParams(1.0, 100.0, {MakeType(&snail, 6.0, 2)}, /*reach=*/2);

  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.view().kills_this_step[0], 0);           // both at 4 HP, alive
  EXPECT_DOUBLE_EQ(sim.view().target_hp_fraction, 0.4);  // 10 - 6

  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.view().kills_this_step[0], 2);  // both die together
  EXPECT_TRUE(sim.respawning());
}

TEST(CombatSimTest, NamesNoSwingWhileRespawning) {
  Mob snail = MakeMob("Snail", 10);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 100.0, {MakeType(&snail, 10.0, 1)});

  sim.Advance(params, 0.5);  // mob up, a swing is coming
  EXPECT_EQ(sim.view().attack_name, "Attack");

  sim.Advance(params, 0.5);  // one-shots the only mob
  ASSERT_TRUE(sim.respawning());
  EXPECT_TRUE(sim.view().attack_name.empty());
}

TEST(CombatSimTest, PicksTheAttackThatLandsTheMostOnTheQueue) {
  Mob snail = MakeMob("Snail", 100);
  CombatSim sim;
  // A wide, weak attack against a narrow, strong one, over four mobs: 4 x 5 =
  // 20 beats 1 x 12, so the wide one is chosen.
  CombatParams params = MakeParams(1.0, 100.0, {MakeType(&snail, 12.0, 4)});
  params.attacks[0].name = "Attack";
  AttackOption wide;
  wide.name = "Sweep";
  wide.max_enemies = 4;
  wide.swing_seconds = 1.0;  // same speed as the basic attack
  wide.damage_per_hit = {5.0};
  params.attacks.push_back(wide);

  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.view().attack_name, "Sweep");
  // All four took 5, instead of one taking 12.
  ASSERT_EQ(sim.view().engaged_groups.size(), 1u);
  EXPECT_EQ(sim.view().engaged_groups[0].count, 4);
  EXPECT_NEAR(sim.view().engaged_groups[0].hp_fraction, 0.95, 1e-9);
}

// An attack is worth what it lands per second, not per use: a skill hitting 50%
// harder but taking twice as long is the worse choice.
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
  EXPECT_EQ(sim.view().attack_name, "Attack");
}

// The same skill wins once its animation is quick enough to pay for itself,
// which is why the delay is per skill.
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
  EXPECT_EQ(sim.view().attack_name, "Heavy");
}

// Final Attack follows the attack, so it's part of the attack's value. It
// doesn't depend on which skill triggered it, so a slower attack spreads the
// same extra hit over more seconds, and that alone can decide the choice.
TEST(CombatSimTest, TheChoiceCountsTheFinalAttackThatFollowsIt) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  // Counting it, the basic attack is 30/s against the heavy attack's 25/s.
  // Ignoring it, 10/s against 15/s, and the heavy attack would win instead.
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
  EXPECT_EQ(sim.view().attack_name, "Attack");
}

// An attack three times the basic attack would simply replace it, so a cooldown
// matters only in the gaps when it's unavailable.
TEST(CombatSimTest, ACooldownKeepsTheBestSwingOffTheMenu) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 100.0, {MakeType(&snail, 10.0, 1)});
  params.attacks[0].name = "Attack";
  params.attacks.push_back(MakeBurst());

  for (int i = 0; i < 3; ++i) {
    sim.Advance(params, 1.0);
  }
  // One burst and then two basic attacks: 50, not the 90 three bursts would be.
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.95, 1e-9);
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
  // The cooldown started on the first attack, so three seconds later the fourth
  // is a burst again: 30 + 10 + 10 + 30.
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.92, 1e-9);
}

// Unlike a summon's clock, a player waiting for a respawn really does have
// their cooldown back when mobs appear. The four seconds of empty map are
// exactly the recharge.
TEST(CombatSimTest, ACooldownRunsDownOnAnEmptyMap) {
  Mob snail = MakeMob("Snail", 30);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 4.0, {MakeType(&snail, 10.0, 1)});
  params.attacks[0].name = "Attack";
  params.attacks.push_back(MakeBurst());

  sim.Advance(params, 1.0);
  ASSERT_EQ(sim.view().kills_this_step[0], 1);  // the burst lands and kills
  sim.Advance(params, 1.0);
  sim.Advance(params, 1.0);
  ASSERT_TRUE(sim.respawning());  // three seconds with nothing to hit
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.view().kills_this_step[0], 1)
      << "the respawned mob survived, so the burst was still recharging";
}

// A cooldown belongs to the character, not the encounter, so arriving somewhere
// new doesn't reset it. A boss phase is a new encounter too; without this,
// every phase would start with every skill ready.
TEST(CombatSimTest, ACooldownSurvivesAChangeOfEncounter) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams first =
      MakeParams(1.0, 100.0, {MakeType(&snail, 10.0, 1)}, 1, "phase 1");
  first.attacks[0].name = "Attack";
  first.attacks.push_back(MakeBurst());
  CombatParams second = first;
  second.encounter = "phase 2";

  sim.Advance(first, 1.0);
  ASSERT_NEAR(sim.view().target_hp_fraction, 0.97, 1e-9);  // the burst landed
  sim.Advance(second, 1.0);
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.99, 1e-9)
      << "the burst was ready again in the new encounter";
}

// The pair of skills a wizard alternates between: each on a short cooldown
// after it lands, so the other is used meanwhile.
CombatParams MakeAlternatingParams(const Mob& snail) {
  CombatParams params = MakeParams(1.0, 100.0, {MakeType(&snail, 1.0, 1)});
  params.attacks[0].name = "Attack";
  params.attacks.push_back(MakeSkill("Ice", 10.0, 0.5));
  params.attacks.push_back(MakeSkill("Bolt", 8.0, 0.5));
  return params;
}

// Committing to an attack in progress is what makes a short cooldown alternate
// two skills. Without it, the stronger one comes back mid-animation and takes
// over, and the other is never used.
TEST(CombatSimTest, ASwingUnderwayIsNotDisplacedByABetterOne) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeAlternatingParams(snail);

  for (int i = 0; i < 4; ++i) {
    sim.Advance(params, 0.25);  // Ice, the harder of the two, lands first
  }
  ASSERT_NEAR(sim.view().target_hp_fraction, 0.99, 1e-9);
  ASSERT_EQ(sim.view().attack_name, "Bolt");

  for (int i = 0; i < 3; ++i) {
    sim.Advance(params, 0.25);  // Ice is off cooldown half way through this
  }
  EXPECT_EQ(sim.view().attack_name, "Bolt")
      << "Ice took a swing already underway";
  sim.Advance(params, 0.25);
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.982, 1e-9);  // 10 and then 8
}

TEST(CombatSimTest, TwoRechargingSkillsTakeTurns) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeAlternatingParams(snail);

  for (int i = 0; i < 4; ++i) {
    sim.Advance(params, 1.0);
  }
  // Ice, Bolt, Ice, Bolt: 36, not the 40 four Ices would be.
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.964, 1e-9);
}

// The basic attack is the exception to committing: it's what the character uses
// while everything else recharges, and waiting it out would delay the skill for
// the rest of the animation.
TEST(CombatSimTest, TheFallbackPokeYieldsAsSoonAsTheSkillIsBack) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 100.0, {MakeType(&snail, 1.0, 1)});
  params.attacks[0].name = "Attack";
  params.attacks.push_back(MakeSkill("Ice", 10.0, 0.5));

  for (int i = 0; i < 4; ++i) {
    sim.Advance(params, 0.25);
  }
  ASSERT_EQ(sim.view().attack_name, "Attack") << "nothing else is up to swing";
  for (int i = 0; i < 4; ++i) {
    sim.Advance(params, 0.25);
  }
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.98,
              1e-9);  // 10 twice, not 10 and 1
}

TEST(CombatSimTest, FallsBackToTheStrongSwingOnTheLastMob) {
  Mob snail = MakeMob("Snail", 100);
  CombatSim sim;
  // The same pair of attacks, but only one mob is up: the wide attack's reach
  // is worthless, so 5 loses to 12 and the narrow one takes over.
  CombatParams params = MakeParams(1.0, 100.0, {MakeType(&snail, 12.0, 1)});
  params.attacks[0].name = "Attack";
  AttackOption wide;
  wide.name = "Sweep";
  wide.max_enemies = 4;
  wide.swing_seconds = 1.0;  // same speed as the basic attack
  wide.damage_per_hit = {5.0};
  params.attacks.push_back(wide);

  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.view().attack_name, "Attack");
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.88, 1e-9);  // took the 12
}

TEST(CombatSimTest, TheChoiceChangesAsTheQueueThins) {
  Mob snail = MakeMob("Snail", 10);
  CombatSim sim;
  // Two mobs, both killed in one hit by the wide attack. It clears them on the
  // first attack, and with the queue empty the narrow attack charges next.
  CombatParams params = MakeParams(1.0, 100.0, {MakeType(&snail, 12.0, 2)});
  params.attacks[0].name = "Attack";
  AttackOption wide;
  wide.name = "Sweep";
  wide.max_enemies = 4;
  wide.swing_seconds = 1.0;  // same speed as the basic attack
  wide.damage_per_hit = {10.0};
  params.attacks.push_back(wide);

  sim.Advance(params, 0.5);  // charging, two mobs up: the sweep wins
  EXPECT_EQ(sim.view().attack_name, "Sweep");

  sim.Advance(params, 0.5);  // it lands and clears both
  EXPECT_EQ(sim.view().kills_this_step[0], 2);
  EXPECT_TRUE(sim.respawning());
}

TEST(CombatSimTest, EngagedGroupsAverageASingleTypesWindow) {
  Mob snail = MakeMob("Snail", 20, 3);
  CombatSim sim;
  // Five mobs, reach 3: only the front three are engaged, so the bar merges
  // three of them, not all five.
  CombatParams params =
      MakeParams(1.0, 100.0, {MakeType(&snail, 4.0, 5)}, /*reach=*/3);

  sim.Advance(params, 1.0);  // front three to 16/20 = 0.8
  ASSERT_EQ(sim.view().engaged_groups.size(), 1u);
  EXPECT_EQ(sim.view().engaged_groups[0].name, "Snail");
  EXPECT_EQ(sim.view().engaged_groups[0].level, 3);
  EXPECT_EQ(sim.view().engaged_groups[0].count, 3);
  EXPECT_NEAR(sim.view().engaged_groups[0].hp_fraction, 0.8, 1e-9);
}

TEST(CombatSimTest, EngagedGroupsMergeTheWindowByType) {
  Mob snail = MakeMob("Snail", 100, 1);
  Mob slug = MakeMob("Slug", 100, 2);
  CombatSim sim;
  // Two of each, and reach 4 hits the whole queue. Same-type mobs that arrived
  // together take the same damage, so each type merges into one bar at a shared
  // HP.
  CombatParams params = MakeParams(
      1.0, 100.0, {MakeType(&snail, 50.0, 2), MakeType(&slug, 20.0, 2)},
      /*reach=*/4);

  sim.Advance(params, 1.0);  // Snails -> 0.5, Slugs -> 0.8
  const std::vector<EngagedGroup>& groups = sim.view().engaged_groups;
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
  // 30 HP / 10 damage = 3 hits to kill the lone mob; respawn at 5s.
  CombatParams params = MakeParams(1.0, 5.0, {MakeType(&snail, 10.0, 1)});

  sim.Advance(params, 1.0);  // hp 20
  sim.Advance(params, 1.0);  // hp 10
  sim.Advance(params, 1.0);  // hp 0 -> respawning; respawn_phase = 3
  EXPECT_TRUE(sim.respawning());
  sim.Advance(params, 1.0);  // respawn_phase = 4, still idle
  EXPECT_TRUE(sim.respawning());
  sim.Advance(params, 1.0);  // respawn_phase = 5 -> refill, then one hit
  EXPECT_FALSE(sim.respawning());
  EXPECT_NEAR(sim.view().target_hp_fraction, 20.0 / 30.0, 1e-9);
}

TEST(CombatSimTest, ARespawnBeatAddsOnlyTheMissingMobs) {
  Mob snail = MakeMob("Snail", 10);
  Mob slug = MakeMob("Slug", 100);
  CombatSim sim;
  // One attack hits both: the Snail dies, and the Slug drops to 90%. The
  // respawn at t=1.5 falls between attacks, with only the Snail to replace.
  CombatParams params = MakeParams(
      1.0, 1.5, {MakeType(&snail, 10.0, 1), MakeType(&slug, 10.0, 1)},
      /*reach=*/2);

  sim.Advance(params, 0.5);
  sim.Advance(params, 0.5);  // t=1: the swing lands, leaving the Slug alone
  ASSERT_EQ(sim.view().engaged_groups.size(), 1u);

  sim.Advance(params, 0.5);  // t=1.5: the beat
  const std::vector<EngagedGroup>& groups = sim.view().engaged_groups;
  ASSERT_EQ(groups.size(), 2u);
  const EngagedGroup* snails = FindGroup(groups, "Snail");
  const EngagedGroup* slugs = FindGroup(groups, "Slug");
  ASSERT_NE(snails, nullptr);
  ASSERT_NE(slugs, nullptr);
  // The Snail is a new spawn, so it arrives at full HP.
  EXPECT_NEAR(snails->hp_fraction, 1.0, 1e-9);
  // The Slug never died. A respawn that rebuilt the whole roster would have
  // healed it to full.
  EXPECT_NEAR(slugs->hp_fraction, 0.9, 1e-9);
}

TEST(CombatSimTest, AMobSlowerThanTheBeatStillDies) {
  Mob slug = MakeMob("Slug", 100);
  CombatSim sim;
  // Ten attacks to kill, with a respawn every five. The damage must persist
  // through respawns, or the mob could never die.
  CombatParams params = MakeParams(1.0, 5.0, {MakeType(&slug, 10.0, 1)});

  int64_t kills = 0;
  for (int i = 0; i < 10; ++i) {
    sim.Advance(params, 1.0);
    kills += sim.view().kills_this_step[0];
  }
  EXPECT_EQ(kills, 1);
}

TEST(CombatSimTest, ABeatMidFightKeepsTheSwingCharging) {
  Mob snail = MakeMob("Snail", 100);
  CombatSim sim;
  // 10 damage against 100 HP, so the mob is still up at the respawn. Attack
  // every 2s, respawn every 3s, stepped 0.5s at a time: the respawn at t=3
  // catches an attack half charged.
  CombatParams params = MakeParams(2.0, 3.0, {MakeType(&snail, 10.0, 1)});

  for (int i = 0; i < 5; ++i) {
    sim.Advance(params, 0.5);  // t=2.5: swing landed at 2, phase back to 0.5
  }
  ASSERT_NEAR(sim.view().attack_fraction, 0.25, 1e-9);

  sim.Advance(params, 0.5);  // t=3: the beat, then another half second
  EXPECT_FALSE(sim.respawning());
  // 0.5s of charge survived the respawn and 0.5s more was added. A respawn that
  // restarted the attack would read 0.25 here.
  EXPECT_NEAR(sim.view().attack_fraction, 0.5, 1e-9);
}

TEST(CombatSimTest, ARespawnBeatAfterAClearStartsAFreshSwing) {
  Mob snail = MakeMob("Snail", 10);
  CombatSim sim;
  // One attack clears the map. Stepping 0.75s against a 1s attack leaves 0.25s
  // of overshoot, which the idle time must not keep.
  CombatParams params = MakeParams(1.0, 3.0, {MakeType(&snail, 10.0, 1)});

  sim.Advance(params, 0.75);
  sim.Advance(params, 0.75);  // t=1.5: the swing lands and clears the map
  ASSERT_TRUE(sim.respawning());
  sim.Advance(params, 0.75);  // t=2.25: still idle, nothing charging
  ASSERT_TRUE(sim.respawning());

  sim.Advance(params, 0.75);  // t=3: the beat, then a fresh swing begins
  EXPECT_FALSE(sim.respawning());
  // Exactly 0.75s since the respawn. Keeping the overshoot would have pushed
  // the attack over the line and landed a hit already.
  EXPECT_NEAR(sim.view().attack_fraction, 0.75, 1e-9);
  EXPECT_NEAR(sim.view().target_hp_fraction, 1.0, 1e-9);
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
  EXPECT_EQ(sim.view().target_name, "Snail");
  EXPECT_NEAR(sim.view().target_hp_fraction, 20.0 / 30.0, 1e-9);

  // The move starts fresh on the new map: a new Slug, whose 30 HP takes this
  // step's hit instead of inheriting the Snail's damage, and the kill is
  // credited to the new map's only type, not a stale index from the old roster.
  sim.Advance(there, 1.0);
  EXPECT_EQ(sim.view().target_name, "Slug");
  EXPECT_NEAR(sim.view().target_hp_fraction, 20.0 / 30.0, 1e-9);

  sim.Advance(there, 1.0);
  sim.Advance(there, 1.0);  // hp 0 -> the Slug dies
  EXPECT_EQ(sim.view().kills_this_step[0], 1);
}

TEST(CombatSimTest, ShufflingTheRosterSpreadsKillsAcrossTypes) {
  Mob snail = MakeMob("Snail", 10);
  Mob blue = MakeMob("Blue Snail", 10);
  CombatSim sim;
  // Six mobs, but only a couple die before the 5s respawn refills the roster,
  // so the player never clears it. With a fixed order only the front type would
  // ever be reached; shuffling must let both types die across respawns.
  CombatParams params = MakeParams(
      1.0, 5.0, {MakeType(&snail, 10.0, 3), MakeType(&blue, 10.0, 3)});

  int64_t snail_kills = 0;
  int64_t blue_kills = 0;
  for (int step = 0; step < 200; ++step) {
    sim.Advance(params, 1.0);
    snail_kills += sim.view().kills_this_step[0];
    blue_kills += sim.view().kills_this_step[1];
  }

  EXPECT_GT(snail_kills, 0);
  EXPECT_GT(blue_kills, 0);
}

TEST(CombatSimTest, ClampsLargeGapsToOneSwing) {
  Mob snail = MakeMob("Snail", 100);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 100.0, {MakeType(&snail, 4.0, 1)});
  sim.Advance(params, 1000.0);  // huge gap -> at most one swing of damage
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.96, 1e-9);  // 100 - 4 = 96
}

// Gives `params` a player with HP to lose, hit every `interval` seconds for
// `damage` by the front mob. Every type hits the same unless a test says
// otherwise.
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
  EXPECT_EQ(sim.view().player_hp, 100);
  EXPECT_DOUBLE_EQ(sim.view().player_hp_fraction, 1.0);

  sim.Advance(params, 0.5);  // the interval closes
  EXPECT_EQ(sim.view().player_hp, 90);
  EXPECT_DOUBLE_EQ(sim.view().player_hp_fraction, 0.9);

  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.view().player_hp, 80);
}

// A frozen monster can't move, so its hits due while the freeze lasts don't
// land. Frostprey gives this to a character without an ice attack.
TEST(CombatSimTest, AFrozenMobLandsNoHit) {
  Mob snail = MakeMob("Snail", 100000);
  CombatParams params = MakeParams(0.5, 1000.0, {MakeType(&snail, 1.0, 1)});
  GivePlayerHp(params, 1000, /*interval=*/1.0, /*damage=*/10.0);

  CombatSim thawed;
  for (int i = 0; i < 50; ++i) {
    thawed.Advance(params, 0.1);
  }
  EXPECT_LT(thawed.view().player_hp, 1000);

  params.attacks[0].freeze_seconds = 2.0;  // relaid by every swing
  CombatSim frozen;
  for (int i = 0; i < 50; ++i) {
    frozen.Advance(params, 0.1);
  }
  EXPECT_EQ(frozen.view().player_hp, 1000);

  params.attacks[0].freeze_seconds = 0.0;  // the ice runs out and is not relaid
  for (int i = 0; i < 40; ++i) {
    frozen.Advance(params, 0.1);
  }
  EXPECT_LT(frozen.view().player_hp, 1000);
}

// Holy Fountain: healing on its own clock, costing no attack and needing no
// hit. It runs alongside incoming damage, and arrives in pulses: nothing until
// the interval is up, then the whole amount at once.
TEST(CombatSimTest, AFountainPoursOnItsOwnClock) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(10.0, 1000.0, {MakeType(&snail, 1.0, 1)});
  GivePlayerHp(params, 100, /*interval=*/1.0, /*damage=*/10.0);
  params.regen_pulses = {{0.20, 0, 5.0}};  // 20 HP every 5s against 10 a hit

  // After four hits the fountain has healed nothing, since the pulse isn't due
  // until its interval is up. A continuous rate would have healed 16 HP by now.
  for (int i = 0; i < 4; ++i) {
    sim.Advance(params, 1.0);
  }
  EXPECT_EQ(sim.view().player_hp, 60);
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.view().player_hp, 70);
}

// Storm of Arrows: the rain grows with the number of enemies the character's
// own attack hits, not with its own reach. So a single-target attack calls the
// short rain however many monsters are under it.
TEST(CombatSimTest, APulseGrowsWithTheSwingsCrowdRatherThanItsOwn) {
  Mob snail = MakeMob("Snail", 1000000000);
  double landed[2] = {0.0, 0.0};
  for (int run = 0; run < 2; ++run) {
    // The attack itself does nothing, so all damage below is the rain's.
    CombatParams params =
        MakeParams(1.0, 1e9, {MakeType(&snail, 0.0, 6)}, run == 0 ? 1 : 4);
    AddAutoAttack(params, /*interval=*/1.0, /*damage=*/70.0);
    AttackOption line;
    line.max_enemies = 1;
    line.damage_per_hit = {10.0};
    params.auto_attacks[0].extra_line =
        std::make_shared<const AttackOption>(line);
    params.auto_attacks[0].lines_per_extra_enemy = 2;
    params.auto_attacks[0].max_extra_lines = 8;
    CombatSim sim;
    for (int step = 0; step < 5; ++step) {
      sim.Advance(params, 1.0);
      landed[run] += sim.view().damage_this_step;
    }
  }
  // The first rain falls before any attack has been aimed, so it's short in
  // both runs. The four after it add three extra enemies' six lines.
  EXPECT_NEAR(landed[0], 5 * 70.0, 1e-6);
  EXPECT_NEAR(landed[1], 70.0 + 4 * 130.0, 1e-6);
}

// Darkness Aura: an own-clock pulse that heals as it lands. It heals per strike
// of the tick, not per tick, and never past full HP.
TEST(CombatSimTest, AnOwnClockPulseHealsAsItLands) {
  Mob snail = MakeMob("Snail", 100000);
  CombatSim sim;
  CombatParams params = MakeParams(10.0, 1000.0, {MakeType(&snail, 1.0, 1)});
  GivePlayerHp(params, 100, /*interval=*/1.0, /*damage=*/10.0);
  AddAutoAttack(params, /*interval=*/1.0, /*damage=*/10.0);
  params.auto_attacks[0].hp_recover_pct = 0.03;
  params.auto_attacks[0].strikes_per_pulse = 2;

  // One hit per second against 6% of a hundred-point pool: down four per
  // second.
  for (int i = 0; i < 5; ++i) {
    sim.Advance(params, 1.0);
  }
  EXPECT_EQ(sim.view().player_hp, 80);

  // And full HP takes no more.
  CombatSim topped;
  CombatParams quiet = params;
  GivePlayerHp(quiet, 100, /*interval=*/1.0, /*damage=*/0.0);
  topped.Advance(quiet, 1.0);
  EXPECT_EQ(topped.view().player_hp, 100);
}

// The Evil Eye's aura heals a flat amount instead of a share of HP, and heals
// it in addition to the share when a fountain has both.
TEST(CombatSimTest, AFountainPoursItsFlatHalfToo) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(10.0, 1000.0, {MakeType(&snail, 1.0, 1)});
  GivePlayerHp(params, 1000, /*interval=*/1.0, /*damage=*/100.0);
  params.regen_pulses = {{0.02, 24, 2.0}};  // 24 HP and 20 more every 2s

  sim.Advance(params, 2.0);
  EXPECT_EQ(sim.view().player_hp, 944);
}

// A step longer than the interval pays every pulse it covered, just as a burn
// ticks for each interval it outlasted.
TEST(CombatSimTest, AFountainPoursEveryPulseAWideStepCovered) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(10.0, 1000.0, {MakeType(&snail, 1.0, 1)});
  GivePlayerHp(params, 1000, /*interval=*/1.0, /*damage=*/100.0);
  params.regen_pulses = {{0.02, 0, 2.0}};  // 20 HP every 2s

  sim.Advance(params, 2.0);  // one hit, one pulse
  EXPECT_EQ(sim.view().player_hp, 920);
  sim.Advance(params, 6.0);  // one hit again, but three pulses
  EXPECT_EQ(sim.view().player_hp, 880);
}

// Two fountains on two clocks, as a Bishop has. Neither waits for the other,
// and a step when both are due pays both.
TEST(CombatSimTest, TwoFountainsPourOnSeparateClocks) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(10.0, 1000.0, {MakeType(&snail, 1.0, 1)});
  GivePlayerHp(params, 1000, /*interval=*/1.0, /*damage=*/100.0);
  params.regen_pulses = {{0.02, 0, 2.0}, {0.03, 0, 3.0}};

  for (int i = 0; i < 2; ++i) {
    sim.Advance(params, 1.0);
  }
  EXPECT_EQ(sim.view().player_hp, 820);  // the 2s one, alone
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.view().player_hp, 750);  // the 3s one, alone
  for (int i = 0; i < 3; ++i) {
    sim.Advance(params, 1.0);
  }
  EXPECT_EQ(sim.view().player_hp, 520);  // both, on the same step
}

// It stops at full HP instead of going past it, like every other heal here.
TEST(CombatSimTest, AFountainNeverFillsPastTheHpPool) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(10.0, 1000.0, {MakeType(&snail, 1.0, 1)});
  GivePlayerHp(params, 100, /*interval=*/1000.0, /*damage=*/10.0);
  params.regen_pulses = {{0.50, 0, 1.0}};

  sim.Advance(params, 10.0);
  EXPECT_EQ(sim.view().player_hp, 100);
}

TEST(CombatSimTest, OnlyOneMobHitsBackHoweverManyAreOnTheMap) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  // Five of them standing there, and the player takes one hit, not five. A
  // crowd doesn't mean five attackers.
  CombatParams params = MakeParams(10.0, 1000.0, {MakeType(&snail, 1.0, 5)});
  GivePlayerHp(params, 100, /*interval=*/1.0, /*damage=*/10.0);

  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.view().player_hp, 90);
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

  // The queue shuffles arrivals, so either could be in front, but the hit the
  // player takes must be from the one in front.
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.view().player_hp, sim.view().target_name == "Snail" ? 95 : 50);
}

// Spirit Blade: a share of every hit taken is dealt back to the mob that landed
// it, which is the one in front.
TEST(CombatSimTest, ReflectionHurtsTheMobThatHits) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(100.0, 1000.0, {MakeType(&snail, 1.0, 1)});
  GivePlayerHp(params, 100, /*interval=*/1.0, /*damage=*/10.0);
  params.damage_reflect_pct = 5.0;

  // The attack is 100 seconds away, so every point the snail loses is
  // reflected.
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.view().player_hp, 90);
  EXPECT_DOUBLE_EQ(sim.view().target_hp_fraction, 0.95);
}

TEST(CombatSimTest, ReflectionCanFinishAMob) {
  Mob snail = MakeMob("Snail", 40);
  CombatSim sim;
  CombatParams params = MakeParams(100.0, 1000.0, {MakeType(&snail, 1.0, 2)});
  GivePlayerHp(params, 100, /*interval=*/1.0, /*damage=*/10.0);
  params.damage_reflect_pct = 5.0;
  // A kill counts however it happened: rewards pay for it like any other kill,
  // and it charges a kill-timed skill the same way. The attack is 100 seconds
  // away, so neither is from the attack.
  AddKillClockedAttack(params, /*kills=*/1, /*damage=*/40.0);

  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.view().kills_this_step[0], 2);  // reflection and its release
  EXPECT_TRUE(sim.respawning());
}

TEST(CombatSimTest, NoReflectionWithoutTheSkill) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(100.0, 1000.0, {MakeType(&snail, 1.0, 1)});
  GivePlayerHp(params, 100, /*interval=*/1.0, /*damage=*/10.0);

  sim.Advance(params, 1.0);
  EXPECT_DOUBLE_EQ(sim.view().target_hp_fraction, 1.0);
}

TEST(CombatSimTest, AnEmptyMapHasNothingToHitThePlayerWith) {
  Mob snail = MakeMob("Snail", 10);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 10.0, 1)});
  GivePlayerHp(params, 100, /*interval=*/1.0, /*damage=*/10.0);

  sim.Advance(params, 1.0);  // the snail hits, then the swing clears the map
  ASSERT_TRUE(sim.respawning());
  EXPECT_EQ(sim.view().player_hp, 90);

  for (int i = 0; i < 5; ++i) {  // idling well past several intervals
    sim.Advance(params, 1.0);
  }
  EXPECT_EQ(sim.view().player_hp, 90);
}

TEST(CombatSimTest, ClearingTheMapHealsThePlayer) {
  Mob snail = MakeMob("Snail", 10);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 3.0, {MakeType(&snail, 10.0, 1)});
  GivePlayerHp(params, 100, /*interval=*/1.0, /*damage=*/10.0);

  sim.Advance(params, 0.5);
  sim.Advance(params, 0.5);  // a hit lands, then the swing clears the map
  ASSERT_TRUE(sim.respawning());
  ASSERT_EQ(sim.view().player_hp, 90);

  for (int i = 0; i < 4; ++i) {  // idle until the beat at 3.0s refills the map
    sim.Advance(params, 0.5);
  }
  EXPECT_FALSE(sim.respawning());
  EXPECT_EQ(sim.view().player_hp, 100);
}

// A passive that heals on attack pays per attack landed, costing no attacks, so
// it adds to the respawn heal instead of replacing it. It can't go past full
// HP.
TEST(CombatSimTest, RecoveryOnAttackRidesTheSwing) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 1.0, 1)});
  GivePlayerHp(params, 100, /*interval=*/1.0, /*damage=*/20.0);
  params.hp_recover_pct = 0.05;

  sim.Advance(params, 1.0);  // one hit taken, one swing landed
  EXPECT_EQ(sim.view().player_hp, 85);
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.view().player_hp, 70);
  for (int i = 0; i < 20; ++i) {  // swinging alone cannot overfill the pool
    sim.Advance(params, 1.0);
  }
  EXPECT_LE(sim.view().player_hp, 100);
}

// An attack can heal on its own, on top of what the character recovers on every
// attack: Angel Ray gets both.
TEST(CombatSimTest, ASwingsOwnRecoveryPaysBesideTheCharacters) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 1.0, 1)});
  GivePlayerHp(params, 100, /*interval=*/1.0, /*damage=*/20.0);
  params.hp_recover_pct = 0.05;
  params.attacks[0].hp_recover_pct = 0.05;

  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.view().player_hp, 90);
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.view().player_hp, 80);
}

// Nothing to hit means nothing to heal from. A cleared map already restores HP
// on the respawn, and attacking empty air must not pay for it twice.
TEST(CombatSimTest, RecoveryOnAttackPaysNothingOnAnEmptyMap) {
  Mob snail = MakeMob("Snail", 10);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 10.0, 1)});
  GivePlayerHp(params, 100, /*interval=*/1.0, /*damage=*/10.0);
  params.hp_recover_pct = 0.05;

  sim.Advance(params, 0.5);
  sim.Advance(params, 0.5);  // a hit lands, then the swing clears the map
  ASSERT_TRUE(sim.respawning());
  int cleared = sim.view().player_hp;
  for (int i = 0; i < 6; ++i) {  // idling well short of the 1000s beat
    sim.Advance(params, 0.5);
  }
  EXPECT_EQ(sim.view().player_hp, cleared);
}

TEST(CombatSimTest, TheHitClockWaitsOnAnEmptyMap) {
  Mob snail = MakeMob("Snail", 10);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 3.0, {MakeType(&snail, 10.0, 1)});
  GivePlayerHp(params, 100, /*interval=*/1.0, /*damage=*/10.0);

  sim.Advance(params, 0.5);
  sim.Advance(params, 0.5);  // hit, then the map is cleared
  ASSERT_EQ(sim.view().player_hp, 90);
  for (int i = 0; i < 4; ++i) {  // two idle seconds, then the refill
    sim.Advance(params, 0.5);
  }
  ASSERT_FALSE(sim.respawning());
  ASSERT_EQ(sim.view().player_hp, 100);

  // Nine tenths of a second of fighting since the map refilled. If the two idle
  // seconds had counted toward the mob's attack timer, several hits would have
  // landed by now.
  sim.Advance(params, 0.4);
  EXPECT_EQ(sim.view().player_hp, 100);
}

TEST(CombatSimTest, ARespawnBeatMidFightHealsASlice) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  // The mobs outlast the respawn interval, so the refill brings more monsters,
  // not a break. The HP slice still comes back regardless, which is what lets a
  // map be held instead of only cleared.
  CombatParams params = MakeParams(10.0, 2.0, {MakeType(&snail, 1.0, 2)});
  GivePlayerHp(params, 100, /*interval=*/1.0, /*damage=*/30.0);
  params.beat_heal_fraction = 0.1;

  sim.Advance(params, 1.0);
  ASSERT_EQ(sim.view().player_hp, 70);
  sim.Advance(params, 1.0);  // the beat lands here, with both mobs still up
  ASSERT_FALSE(sim.respawning());
  EXPECT_EQ(sim.view().player_hp,
            50);  // 70, +10 from the beat, -30 from the hit
}

TEST(CombatSimTest, ABeatCannotHealPastFull) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(10.0, 2.0, {MakeType(&snail, 1.0, 2)});
  GivePlayerHp(params, 100, /*interval=*/1.0, /*damage=*/5.0);
  params.beat_heal_fraction = 0.1;

  sim.Advance(params, 1.0);
  ASSERT_EQ(sim.view().player_hp, 95);
  // The respawn's tenth is more than the one hit took, and the surplus isn't
  // kept: the player is left one hit down, not banking healing for later.
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.view().player_hp, 95);
}

TEST(CombatSimTest, ChangingMapHealsThePlayer) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(10.0, 1000.0, {MakeType(&snail, 1.0, 1)});
  GivePlayerHp(params, 100, /*interval=*/1.0, /*damage=*/10.0);

  sim.Advance(params, 1.0);
  sim.Advance(params, 1.0);
  ASSERT_EQ(sim.view().player_hp, 80);

  CombatParams elsewhere = MakeParams(10.0, 1000.0, {MakeType(&snail, 1.0, 1)},
                                      /*reach=*/1, /*map=*/"elsewhere");
  GivePlayerHp(elsewhere, 100, /*interval=*/1.0, /*damage=*/10.0);
  sim.Advance(elsewhere, 0.5);
  EXPECT_EQ(sim.view().player_hp, 100);
}

TEST(CombatSimTest, PlayerHpStopsAtZero) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(10.0, 1000.0, {MakeType(&snail, 1.0, 1)});
  GivePlayerHp(params, 100, /*interval=*/1.0, /*damage=*/60.0);

  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.view().player_hp, 40);

  sim.Advance(params, 1.0);  // more than the 40 that is left
  EXPECT_EQ(sim.view().player_hp, 0);
  EXPECT_DOUBLE_EQ(sim.view().player_hp_fraction, 0.0);
}

TEST(CombatSimTest, ASliverOfHpStillReadsAsOne) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(10.0, 1000.0, {MakeType(&snail, 1.0, 1)});
  GivePlayerHp(params, 100, /*interval=*/1.0, /*damage=*/99.5);

  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.view().player_hp, 1);  // 0.5 left, which is not death
}

TEST(CombatSimTest, LevellingUpFillsTheWiderPool) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(10.0, 1000.0, {MakeType(&snail, 1.0, 1)});
  GivePlayerHp(params, 100, /*interval=*/1.0, /*damage=*/10.0);
  params.player_level = 30;

  sim.Advance(params, 1.0);
  ASSERT_EQ(sim.view().player_hp, 90);

  // The level-up arrives and max HP grows. Otherwise the bar would read 90 of
  // 200, less than half full after losing one hit.
  params.player_level = 31;
  params.max_player_hp = 200;
  sim.Advance(params, 0.5);
  EXPECT_EQ(sim.view().player_hp, 200);
  EXPECT_EQ(sim.view().player_max_hp, 200);
}

// Spending a skill point on a passive with max HP raises max HP at the same
// level. That's not a level-up and must not heal, or the player would get a
// free full heal for every point they had left to spend.
TEST(CombatSimTest, AWiderPoolAtTheSameLevelDoesNotHeal) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(10.0, 1000.0, {MakeType(&snail, 1.0, 1)});
  GivePlayerHp(params, 100, /*interval=*/1.0, /*damage=*/10.0);
  params.player_level = 30;

  sim.Advance(params, 1.0);
  ASSERT_EQ(sim.view().player_hp, 90);

  params.max_player_hp = 200;
  sim.Advance(params, 0.5);
  EXPECT_EQ(sim.view().player_hp, 90);
  EXPECT_EQ(sim.view().player_max_hp, 200);
}

// When max HP shrinks (say, removing a hat), current HP drops with it instead
// of staying above what their stats allow. This holds for a character with no
// fountain too: the clamp belongs to the HP pool, not to healing.
TEST(CombatSimTest, ANarrowerPoolTakesTheOverflowWithIt) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(10.0, 1000.0, {MakeType(&snail, 1.0, 1)});
  GivePlayerHp(params, 100, /*interval=*/1.0, /*damage=*/10.0);
  params.player_level = 30;

  sim.Advance(params, 1.0);
  ASSERT_EQ(sim.view().player_hp, 90);

  params.max_player_hp = 50;
  sim.Advance(params, 0.5);
  EXPECT_EQ(sim.view().player_hp, 50);
}

TEST(CombatSimTest, InactiveParamsShowNoPlayerHp) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(10.0, 1000.0, {MakeType(&snail, 1.0, 1)});
  GivePlayerHp(params, 100, /*interval=*/1.0, /*damage=*/10.0);
  sim.Advance(params, 1.0);
  ASSERT_EQ(sim.view().player_hp, 90);

  sim.Advance(CombatParams{}, 1.0);
  EXPECT_EQ(sim.view().player_hp, 0);
  EXPECT_DOUBLE_EQ(sim.view().player_hp_fraction, 0.0);
}

// --- skills that fire on their own clock ---

TEST(CombatSimTest, AnAutoAttackFiresOnItsOwnClock) {
  Mob snail = MakeMob("Snail", 100);
  CombatSim sim;
  // An attack far too slow to interfere, so only the cast lands.
  CombatParams params = MakeParams(1000.0, 1000.0, {MakeType(&snail, 0.0, 1)});
  AddAutoAttack(params, /*interval=*/2.0, /*damage=*/25.0);

  sim.Advance(params, 1.0);  // mid-interval
  EXPECT_NEAR(sim.view().target_hp_fraction, 1.0, 1e-9);
  sim.Advance(params, 1.0);  // the interval closes
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.75, 1e-9);
  sim.Advance(params, 2.0);
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.50, 1e-9);
}

TEST(CombatSimTest, AnAutoAttackKillsAreRewardedLikeAnySwing) {
  Mob snail = MakeMob("Snail", 10);
  CombatSim sim;
  CombatParams params = MakeParams(1000.0, 1000.0, {MakeType(&snail, 0.0, 1)});
  AddAutoAttack(params, /*interval=*/1.0, /*damage=*/50.0);

  sim.Advance(params, 1.0);
  ASSERT_EQ(sim.view().kills_this_step.size(), 1u);
  EXPECT_EQ(sim.view().kills_this_step[0], 1);
}

TEST(CombatSimTest, AnAutoAttackReachesWhatItsSkillSays) {
  Mob snail = MakeMob("Snail", 100);
  CombatSim sim;
  CombatParams params = MakeParams(1000.0, 1000.0, {MakeType(&snail, 0.0, 5)});
  AddAutoAttack(params, /*interval=*/1.0, /*damage=*/100.0, /*reach=*/3);

  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.view().kills_this_step[0], 3);  // three of the five, not one
}

TEST(CombatSimTest, ATriggeredAttackFiresOnTheFourthSwing) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  // An attack that does nothing, so only the volley damages the mob.
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 0.0, 1)});
  AddTriggeredAttack(params, /*attacks=*/4, /*damage=*/100.0);

  for (int i = 0; i < 3; ++i) {
    sim.Advance(params, 1.0);
    EXPECT_NEAR(sim.view().target_hp_fraction, 1.0, 1e-9) << "swing " << i + 1;
  }
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.90, 1e-9);
  // And again four attacks later, not every attack from here on. Stepped one
  // attack at a time, since a single long Advance is clamped to one.
  for (int i = 0; i < 3; ++i) {
    sim.Advance(params, 1.0);
    EXPECT_NEAR(sim.view().target_hp_fraction, 0.90, 1e-9) << "swing " << i + 5;
  }
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.80, 1e-9);
}

// This is why the weight exists: an attack landing seven times as often counts
// as a seventh, so the volley comes at the same rate either way.
TEST(CombatSimTest, ARapidSwingTakesSevenTimesAsManyToFireIt) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 0.0, 1)});
  params.attacks[0].count_weight = 1.0 / 7.0;
  AddTriggeredAttack(params, /*attacks=*/4, /*damage=*/100.0);

  // 27 attacks is just short of the 4 it takes.
  for (int i = 0; i < 27; ++i) {
    sim.Advance(params, 1.0);
  }
  EXPECT_NEAR(sim.view().target_hp_fraction, 1.0, 1e-9);
  // The 28th completes it. A seventh isn't exact in floating point, so this
  // also checks the counter uses a tolerance instead of a bare comparison.
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.90, 1e-9);
}

// The remainder carries over instead of resetting, or an attack worth a
// fraction would lose the rest and never fire at all.
TEST(CombatSimTest, TheSwingCountCarriesItsRemainder) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 0.0, 1)});
  params.attacks[0].count_weight = 3.0;
  AddTriggeredAttack(params, /*attacks=*/2, /*damage=*/100.0);

  // Worth three where two are needed: it fires, and the spare one counts toward
  // the next.
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.90, 1e-9);
  sim.Advance(params, 1.0);  // 1 carried + 3 = 4, so two more casts
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.70, 1e-9);
}

// Inhuman Speed's afterimage: one firing is five shots, which land separately
// like a pulse's strikes instead of merging into one bigger hit.
TEST(CombatSimTest, ATriggeredAttackLandsEveryStrikeOfOneFiring) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 0.0, 1)});
  AddTriggeredAttack(params, /*attacks=*/2, /*damage=*/100.0);
  params.triggered_attacks[0].strikes_per_pulse = 5;

  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.view().target_hp_fraction, 1.0, 1e-9);
  sim.Advance(params, 1.0);  // 5 x 100 of the snail's 1000
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.50, 1e-9);
}

TEST(CombatSimTest, ATriggeredAttackReachesWhatItsSkillSays) {
  Mob snail = MakeMob("Snail", 10);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 0.0, 6)});
  AddTriggeredAttack(params, /*attacks=*/1, /*damage=*/100.0, /*reach=*/6);

  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.view().kills_this_step[0], 6);
}

// Erda Fountain's timing: the twelfth enemy defeated releases what the previous
// eleven built up. Defeats are counted one step late, so the release lands on
// the step after the one that completed the count.
TEST(CombatSimTest, AKillClockedAttackFiresOnTheTwelfthDefeat) {
  Mob snail = MakeMob("Snail", 10);
  CombatSim sim;
  // One attack, one kill; the release kills one more when it comes.
  CombatParams params = MakeParams(1.0, 1.0, {MakeType(&snail, 10.0, 13)});
  AddKillClockedAttack(params, /*kills=*/12, /*damage=*/10.0);

  for (int i = 0; i < 12; ++i) {
    sim.Advance(params, 1.0);
    EXPECT_EQ(sim.view().kills_this_step[0], 1) << "swing " << i + 1;
  }
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.view().kills_this_step[0], 2);  // the release, and the swing
}

// A wide attack can kill more than the whole count at once, and owes a release
// for each full count. The remainder carries over, as the attack count does.
TEST(CombatSimTest, ACrowdFallingAtOnceOwesEveryReleaseItCharged) {
  Mob snail = MakeMob("Snail", 10);
  CombatSim sim;
  CombatParams params =
      MakeParams(1.0, 1.0, {MakeType(&snail, 10.0, 24)}, /*reach=*/12);
  AddKillClockedAttack(params, /*kills=*/5, /*damage=*/10.0);

  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.view().kills_this_step[0], 12);  // the swing alone
  // Twelve defeats owe two releases with two left over, so the next twelve owe
  // three: the remainder still counts.
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.view().kills_this_step[0], 14);
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.view().kills_this_step[0], 15);
}

// A defeat counts however it happened: the character never attacks here, and a
// summon's kills charge the fountain alone. The release's own kill counts too,
// which keeps it going once started.
TEST(CombatSimTest, ADefeatChargesItHoweverItWasDealt) {
  Mob snail = MakeMob("Snail", 10);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 1.0, {MakeType(&snail, 0.0, 8)});
  AddAutoAttack(params, /*interval=*/1.0, /*damage=*/10.0);
  AddKillClockedAttack(params, /*kills=*/2, /*damage=*/10.0);

  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.view().kills_this_step[0], 1);  // the summon, charging it
  // The second defeat triggers a release, and its kill is half of the next
  // count, so from here the summon alone keeps a release coming every step.
  for (int i = 0; i < 3; ++i) {
    sim.Advance(params, 1.0);
    EXPECT_EQ(sim.view().kills_this_step[0], 2) << "step " << i + 2;
  }
}

// A healing cast isn't an attack, so it counts toward nothing. A character
// spending turns staying alive isn't building up a volley too.
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

  // Beaten below a quarter of HP, at which point every turn goes to the cast,
  // and the mob's HP never changes however many are spent.
  for (int i = 0; i < 40; ++i) {
    sim.Advance(params, 1.0);
  }
  ASSERT_LT(sim.view().player_hp_fraction, 0.25);
  double before = sim.view().target_hp_fraction;
  for (int i = 0; i < 20; ++i) {
    sim.Advance(params, 1.0);
  }
  EXPECT_NEAR(sim.view().target_hp_fraction, before, 1e-9);
}

// The attack is chosen after the casts land, so a skill that thins out the map
// changes what the character uses next.
TEST(CombatSimTest, TheSwingIsPickedAfterTheCasts) {
  Mob snail = MakeMob("Snail", 10);
  CombatSim sim;
  CombatParams params = MakeParams(1000.0, 1000.0, {MakeType(&snail, 0.0, 2)});
  AddAutoAttack(params, /*interval=*/1.0, /*damage=*/100.0, /*reach=*/2);

  sim.Advance(params, 1.0);
  EXPECT_TRUE(sim.respawning());  // the cast emptied the map
  EXPECT_TRUE(sim.view().attack_name.empty());
}

// Bolt Barrage's shape: a wall hit once per bolt, not all at once. Dead mobs
// are cleared between bolts, so a wall reaching two enemies kills eight in one
// attack. Merging the eight bolts into one hit would waste that as overkill.
TEST(CombatSimTest, ASequencedSwingClearsTheDeadBetweenItsStrikes) {
  Mob snail = MakeMob("Snail", 10);
  CombatSim sim;
  CombatParams params =
      MakeParams(1.0, 1e9, {MakeType(&snail, 10.0, 8)}, /*reach=*/2);
  params.attacks[0].strikes_in_sequence = 4;
  params.attacks[0].cast_interval_seconds = 0.25;

  // The attack lands its first strike and the rest follow on their interval,
  // two enemies at a time and never the same two, since the dead are cleared
  // between.
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.view().kills_this_step[0], 2);
  for (int strike = 0; strike < 3; ++strike) {
    sim.Advance(params, 0.25);
    EXPECT_EQ(sim.view().kills_this_step[0], 2) << "strike " << strike + 2;
  }

  // The same attack merged into one strike of four times the damage hits its
  // two targets and no more, however hard it lands.
  CombatSim folded;
  CombatParams lump =
      MakeParams(1.0, 1e9, {MakeType(&snail, 40.0, 8)}, /*reach=*/2);
  folded.Advance(lump, 1.0);
  EXPECT_EQ(folded.view().kills_this_step[0], 2);
  folded.Advance(lump, 0.25);
  EXPECT_EQ(folded.view().kills_this_step[0], 0);
}

// A sequenced attack is valued at what one press really lands: the strikes that
// fit before the same skill can be cast again and restart the wall.
TEST(CombatSimTest, ASequencedSwingIsPricedAtTheStrikesThePressBuys) {
  Mob boss = MakeMob("Boss", 1000000000);
  CombatParams params = MakeParams(1.0, 1e9, {MakeType(&boss, 100.0, 1)});
  params.attacks[0].name = "Bolt Barrage";
  params.attacks[0].strikes_in_sequence = 4;
  params.attacks[0].cast_interval_seconds = 0.25;
  // Stronger in one hit than a bolt, and weaker than all four together.
  AttackOption flat;
  flat.name = "Chain Lightning";
  flat.max_enemies = 1;
  flat.swing_seconds = 1.0;
  flat.damage_per_hit.assign(1, 300.0);
  params.attacks.push_back(std::move(flat));

  // Three intervals of 0.25s fit in the 1s press, so the wall is worth 400.
  CombatSim inside;
  inside.Advance(params, 0.1);
  EXPECT_EQ(inside.view().attack_name, "Bolt Barrage");

  // Make the interval as long as the press, and the next cast cuts the wall off
  // after two bolts: 200 against the flatter attack's 300.
  params.attacks[0].cast_interval_seconds = 1.0;
  CombatSim overhanging;
  overhanging.Advance(params, 0.1);
  EXPECT_EQ(overhanging.view().attack_name, "Chain Lightning");

  // A cooldown is what really limits the wall, and a three-second one lets
  // every bolt land. Jupiter Thunder's shape: a barrage nothing can cut short
  // is worth all four.
  params.attacks[0].cooldown_seconds = 3.0;
  CombatSim waiting;
  waiting.Advance(params, 0.1);
  EXPECT_EQ(waiting.view().attack_name, "Bolt Barrage");
}

TEST(CombatSimTest, AnAutoAttackClockWaitsWhileTheMapIsEmpty) {
  Mob snail = MakeMob("Snail", 10);
  CombatSim sim;
  // An attack slow enough to never land, so only the casts kill and they
  // control the timing.
  CombatParams params = MakeParams(1000.0, 5.0, {MakeType(&snail, 0.0, 1)});
  AddAutoAttack(params, /*interval=*/3.0, /*damage=*/50.0);

  for (int i = 0; i < 3; ++i) {
    sim.Advance(params, 1.0);
  }
  ASSERT_EQ(sim.view().kills_this_step[0], 1);  // t=3: the cast lands
  ASSERT_TRUE(sim.respawning());

  // t=4 is idle and gives the summon nothing. The respawn at t=5 refills the
  // map and the clock resumes, so the next cast is due at t=7. If the idle
  // second had counted, it would have come at t=6.
  for (int i = 0; i < 3; ++i) {
    sim.Advance(params, 1.0);
  }
  EXPECT_EQ(sim.view().kills_this_step[0], 0);  // t=6
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.view().kills_this_step[0], 1);  // t=7
}

TEST(CombatSimTest, AnAutoAttackWithNoIntervalNeverFires) {
  Mob snail = MakeMob("Snail", 100);
  CombatSim sim;
  CombatParams params = MakeParams(1000.0, 1000.0, {MakeType(&snail, 0.0, 1)});
  AddAutoAttack(params, /*interval=*/0.0, /*damage=*/100.0);

  sim.Advance(params, 100.0);
  EXPECT_NEAR(sim.view().target_hp_fraction, 1.0, 1e-9);
}

TEST(CombatSimTest, AnAutoAttackIsNeverChosenAsTheSwing) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 1.0, 1)});
  // A cast that hits much harder than the attack still doesn't replace it: the
  // charge bar shows what the character is attacking with, not what a summon is
  // about to do.
  AddAutoAttack(params, /*interval=*/1.0, /*damage=*/500.0);

  sim.Advance(params, 0.5);
  EXPECT_EQ(sim.view().attack_name, "Attack");
}

// --- Final Attack ---

// Gives the attack a Final Attack worth `damage` against each enemy it reaches.
void AddFinalAttack(CombatParams& params, double damage) {
  params.attacks[0].final_attack_damage.assign(params.types.size(), damage);
}

TEST(CombatSimTest, FinalAttackAddsToTheSwing) {
  Mob snail = MakeMob("Snail", 100);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 10.0, 1)});
  AddFinalAttack(params, /*damage=*/15.0);

  sim.Advance(params, 1.0);
  // 10 from the attack and 15 from the Final Attack, on the one mob in front.
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.75, 1e-9);
}

// A Final Attack rolls separately against every enemy the attack reached, so a
// wide attack triggers it once per target.
TEST(CombatSimTest, FinalAttackFollowsTheSwingOntoEveryEnemy) {
  Mob snail = MakeMob("Snail", 100);
  CombatSim sim;
  CombatParams params =
      MakeParams(1.0, 1000.0, {MakeType(&snail, 10.0, 3)}, /*reach=*/3);
  AddFinalAttack(params, /*damage=*/40.0);

  sim.Advance(params, 1.0);
  // All three took the attack's 10 and the Final Attack's 40.
  const std::vector<EngagedGroup>& groups = sim.view().engaged_groups;
  ASSERT_EQ(groups.size(), 1u);
  EXPECT_EQ(groups[0].count, 3);
  EXPECT_NEAR(groups[0].hp_fraction, 0.5, 1e-9);
}

// An attack wider than the queue triggers it once per mob actually present, not
// once per target it could have reached.
TEST(CombatSimTest, FinalAttackStopsWithTheSwing) {
  Mob snail = MakeMob("Snail", 100);
  CombatSim sim;
  CombatParams params =
      MakeParams(1.0, 1000.0, {MakeType(&snail, 10.0, 2)}, /*reach=*/6);
  AddFinalAttack(params, /*damage=*/40.0);

  sim.Advance(params, 1.0);
  const std::vector<EngagedGroup>& groups = sim.view().engaged_groups;
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
  EXPECT_EQ(sim.view().kills_this_step[0], 1);
}

// A summon isn't the character attacking, so nothing follows it.
TEST(CombatSimTest, ACastOnItsOwnClockSetsOffNoFinalAttack) {
  Mob snail = MakeMob("Snail", 100);
  CombatSim sim;
  CombatParams params = MakeParams(1000.0, 1000.0, {MakeType(&snail, 0.0, 1)});
  AddFinalAttack(params, /*damage=*/50.0);
  AddAutoAttack(params, /*interval=*/1.0, /*damage=*/10.0);

  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.90,
              1e-9);  // the cast's 10, alone
}

TEST(CombatSimTest, NoFinalAttackLeavesTheSwingAsItIs) {
  Mob snail = MakeMob("Snail", 100);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 10.0, 1)});

  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.90, 1e-9);
}

// Gives the attack an opening hit worth `damage` on one enemy.
void AddLead(CombatParams& params, double damage) {
  params.attacks[0].lead_damage.assign(params.types.size(), damage);
}

// The opening hit lands once, on the healthiest mob the attack reached, not on
// all of them and not on the front one.
TEST(CombatSimTest, TheOpeningHitPicksTheHealthiestMobItReached) {
  Mob snail = MakeMob("Snail", 100);
  Mob boar = MakeMob("Boar", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(
      1.0, 1000.0, {MakeType(&snail, 10.0, 2), MakeType(&boar, 10.0, 1)},
      /*reach=*/3);
  AddLead(params, /*damage=*/100.0);

  sim.Advance(params, 1.0);
  // All three took the spread's 10. Only the boar, at 1000 HP against the
  // snails' 100, also took the opening 100.
  const EngagedGroup* snails = FindGroup(sim.view().engaged_groups, "Snail");
  const EngagedGroup* boars = FindGroup(sim.view().engaged_groups, "Boar");
  ASSERT_NE(snails, nullptr);
  ASSERT_NE(boars, nullptr);
  EXPECT_NEAR(snails->hp_fraction, 0.90, 1e-9);
  EXPECT_NEAR(boars->hp_fraction, 0.89, 1e-9);
}

// A second part that reaches fewer enemies than the first lands on that many,
// healthiest first. Piercing Arrow II's fragment works this way.
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
  // The ogre and the boar are the two healthiest, so the fragment hits them and
  // the snail takes only the spread.
  const EngagedGroup* snails = FindGroup(sim.view().engaged_groups, "Snail");
  const EngagedGroup* boars = FindGroup(sim.view().engaged_groups, "Boar");
  const EngagedGroup* ogres = FindGroup(sim.view().engaged_groups, "Ogre");
  ASSERT_NE(snails, nullptr);
  ASSERT_NE(boars, nullptr);
  ASSERT_NE(ogres, nullptr);
  EXPECT_NEAR(snails->hp_fraction, 0.90, 1e-9);
  EXPECT_NEAR(boars->hp_fraction, 0.89, 1e-9);
  EXPECT_NEAR(ogres->hp_fraction, 0.989, 1e-9);
}

// An attack worth more than its spread alone must be ranked on its full value,
// or the fight picks the wrong one.
TEST(CombatSimTest, TheOpeningHitCountsTowardChoosingTheSwing) {
  Mob snail = MakeMob("Snail", 100000);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 5.0, 1)});
  AddLead(params, /*damage=*/50.0);
  AttackOption plain = MakeSkill("Plain", /*damage=*/20.0, /*cooldown=*/0.0);
  params.attacks.push_back(std::move(plain));

  // The basic attack spreads for 5 where Plain lands 20, but its opening hit
  // adds 50: 55 against 20, so it's the one used.
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.view().attack_name, "Attack");
  EXPECT_NEAR(sim.view().target_hp_fraction, 1.0 - 55.0 / 100000.0, 1e-9);
}

// Splits the attack into `hits` scattered strikes, with a repeat hit keeping
// `kept` of a strike's damage.
void AddScatter(CombatParams& params, int hits, double kept) {
  params.attacks[0].scatter_hits = hits;
  params.attacks[0].scatter_repeat_kept = kept;
}

// Five strikes over three enemies: each takes one strike before any takes a
// second, and the two spare strikes go to the healthiest. This is GMS's "the
// flames go for the boss first", as far as this game can model it.
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
  const EngagedGroup* snails = FindGroup(sim.view().engaged_groups, "Snail");
  const EngagedGroup* boars = FindGroup(sim.view().engaged_groups, "Boar");
  const EngagedGroup* ogres = FindGroup(sim.view().engaged_groups, "Ogre");
  ASSERT_NE(snails, nullptr);
  ASSERT_NE(boars, nullptr);
  ASSERT_NE(ogres, nullptr);
  // The ogre and the boar take two strikes each, the second worth 45% of the
  // first; the snail takes one.
  EXPECT_NEAR(ogres->hp_fraction, 1.0 - 14.5 / 10000.0, 1e-9);
  EXPECT_NEAR(boars->hp_fraction, 1.0 - 14.5 / 1000.0, 1e-9);
  EXPECT_NEAR(snails->hp_fraction, 1.0 - 10.0 / 100.0, 1e-9);
}

// With nobody else to spread to, every strike lands on the one enemy. That's
// the point of the skill, and treating it as a plain N-enemy attack would lose
// it. The rate must reflect this too, or the fight would never pick it.
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
  EXPECT_EQ(sim.view().attack_name, "Attack");
  EXPECT_NEAR(sim.view().target_hp_fraction, 1.0 - 28.0 / 100000.0, 1e-9);
}

// Poison Nova's cap: fifteen clouds burst on a lone boss, but only the three
// GMS allows land and the rest hit nothing. Strikes past the cap are lost, not
// moved elsewhere, which is how this differs from a repeat reduction.
TEST(CombatSimTest, AScatteredSwingPilesNoDeeperThanItsCap) {
  Mob snail = MakeMob("Snail", 100000);
  Mob boar = MakeMob("Boar", 100000);
  CombatSim sim;
  CombatParams params = MakeParams(
      1.0, 1000.0, {MakeType(&snail, 10.0, 1), MakeType(&boar, 10.0, 1)},
      /*reach=*/5);
  AddScatter(params, /*hits=*/5, /*kept=*/1.0);
  params.attacks[0].scatter_max_hits_per_enemy = 2;

  // Five strikes over two enemies would be three and two. The cap limits the
  // healthier one to two, and the third strike is lost.
  sim.Advance(params, 1.0);
  const EngagedGroup* snails = FindGroup(sim.view().engaged_groups, "Snail");
  const EngagedGroup* boars = FindGroup(sim.view().engaged_groups, "Boar");
  ASSERT_NE(snails, nullptr);
  ASSERT_NE(boars, nullptr);
  EXPECT_NEAR(snails->hp_fraction, 1.0 - 20.0 / 100000.0, 1e-9);
  EXPECT_NEAR(boars->hp_fraction, 1.0 - 20.0 / 100000.0, 1e-9);
}

// An attack with more reach than strikes hits only as many enemies as it has
// strikes. Burns and freezes follow suit, since all three read the same count.
TEST(CombatSimTest, AScatteredSwingReachesNoFurtherThanItsStrikes) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 10.0, 4)},
                                   /*reach=*/4);
  AddScatter(params, /*hits=*/2, /*kept=*/0.45);

  sim.Advance(params, 1.0);
  // Two of the four snails took a strike each and the other two nothing, so the
  // group has lost 20 of its 4000, not 40.
  const EngagedGroup* snails = FindGroup(sim.view().engaged_groups, "Snail");
  ASSERT_NE(snails, nullptr);
  EXPECT_NEAR(snails->hp_fraction, 1.0 - 20.0 / 4000.0, 1e-9);
}

// DoT Punisher: as many orbs as burn stacks already active, up to its cap. An
// attack's own burn lands after its damage, so it never widens itself.
TEST(CombatSimTest, AScatteredSwingWidensWithTheBurnsAlreadyAlight) {
  Mob snail = MakeMob("Snail", 1000000);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 1e9, {MakeType(&snail, 10.0, 1)},
                                   /*reach=*/5);
  AddScatter(params, /*hits=*/2, /*kept=*/0.5);
  params.attacks[0].scatter_hits_per_dot = 1.0;
  params.attacks[0].scatter_max_hits = 5;
  // A burn dealing nothing per tick, so it only changes the count.
  DotApplication burn = MakeBurn(0.0, 1.0, 30.0);
  // More stacks than the count can use, so the cap stops it, not the stack
  // limit.
  burn.max_stacks = 6;
  params.attacks[0].dots.push_back(burn);
  params.dot_count = 1;

  double taken[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
  double left = 1.0;
  for (int cast = 0; cast < 6; ++cast) {
    sim.Advance(params, 1.0);
    taken[cast] = (left - sim.view().target_hp_fraction) * 1000000.0;
    left = sim.view().target_hp_fraction;
  }
  // 2 strikes, then 3, 4 and 5 as the stacks grow, then 5 twice more while the
  // stacks keep growing: the count stops at its cap.
  EXPECT_NEAR(taken[0], 15.0, 1e-9);
  EXPECT_NEAR(taken[1], 20.0, 1e-9);
  EXPECT_NEAR(taken[2], 25.0, 1e-9);
  EXPECT_NEAR(taken[3], 30.0, 1e-9);
  EXPECT_NEAR(taken[4], 30.0, 1e-9);
  EXPECT_NEAR(taken[5], 30.0, 1e-9);
}

// Adds a healing cast beside the attack, worth `fraction` of HP. It carries
// damage the fight must never land: the cast is harmless because the fight
// doesn't attack with it, not because the encounter zeroed it.
void AddHeal(CombatParams& params, double fraction, double swing = 1.0) {
  AttackOption heal;
  heal.name = "Heal";
  heal.max_enemies = 1;
  heal.damage_per_hit.assign(params.types.size(), 1000.0);
  heal.swing_seconds = swing;
  heal.heal_fraction = fraction;
  params.attacks.push_back(std::move(heal));
}

// The whole healing cast rule in one setup: ignored while the player is
// healthy, used as soon as they drop below a quarter, and healing its share of
// HP instead of dealing damage.
TEST(CombatSimTest, AHealingCastIsSpentOnlyOnceThePlayerIsLow) {
  Mob snail = MakeMob("Snail", 100000);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 10.0, 1)});
  GivePlayerHp(params, /*max_hp=*/100, /*interval=*/1.0, /*damage=*/10.0);
  AddHeal(params, /*fraction=*/0.5);

  // Seven seconds of hits leave them at 30, still above a quarter, so every
  // turn so far has been the attack: seven of them, 70 damage.
  for (int i = 0; i < 7; ++i) {
    sim.Advance(params, 1.0);
  }
  ASSERT_EQ(sim.view().player_hp, 30);
  EXPECT_NEAR(sim.view().target_hp_fraction, 1.0 - 70.0 / 100000.0, 1e-9);
  EXPECT_EQ(sim.view().attack_name, "Attack");

  // The eighth hit takes them to 20, and that turn goes to the cast: half their
  // HP back, and the mob still at the seven hits it has taken.
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.view().player_hp, 70);
  EXPECT_NEAR(sim.view().target_hp_fraction, 1.0 - 70.0 / 100000.0, 1e-9);
}

// The cast replaces the next attack, not the one already winding up. An attack
// in progress is committed to, even when the player is in trouble.
TEST(CombatSimTest, AHealingCastWaitsForTheSwingAlreadyWindingUp) {
  Mob snail = MakeMob("Snail", 100000);
  CombatSim sim;
  // The basic attack does nothing, so the four-second skill is chosen, and
  // unlike the basic attack, a skill is committed to once it's winding up.
  CombatParams params =
      MakeParams(4.0, 1000.0, {MakeType(&snail, 0.0, 3)}, /*reach=*/3);
  AttackOption skill = MakeSkill("Skill", /*damage=*/10.0, /*cooldown=*/0.0);
  skill.swing_seconds = 4.0;
  skill.max_enemies = 3;
  params.attacks.push_back(std::move(skill));
  AddHeal(params, /*fraction=*/0.5, /*swing=*/4.0);
  GivePlayerHp(params, /*max_hp=*/1000, /*interval=*/3.0, /*damage=*/800.0);

  // A hit on the third second takes them to a fifth of their HP, with the skill
  // three seconds into its four.
  for (int i = 0; i < 3; ++i) {
    sim.Advance(params, 1.0);
  }
  ASSERT_EQ(sim.view().player_hp, 200);
  EXPECT_EQ(sim.view().attack_name, "Skill");
  EXPECT_DOUBLE_EQ(sim.view().target_hp_fraction, 1.0);

  // The fourth second finishes it: the skill lands instead of being cancelled,
  // and only then is the cast queued.
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.view().target_hp_fraction, 1.0 - 10.0 / 100000.0, 1e-9);
  EXPECT_EQ(sim.view().attack_name, "Heal");
  // The cast targets nobody, but the window it charges in still shows the last
  // attack's targets, so the mob bars must not collapse behind it.
  ASSERT_EQ(sim.view().engaged_groups.size(), 1u);
  EXPECT_EQ(sim.view().engaged_groups.front().count, 3);
}

// Max HP is the ceiling: overhealing is wasted, not saved.
TEST(CombatSimTest, AHealingCastStopsAtAFullPool) {
  Mob snail = MakeMob("Snail", 100000);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 10.0, 1)});
  GivePlayerHp(params, /*max_hp=*/100, /*interval=*/1.0, /*damage=*/45.0);
  AddHeal(params, /*fraction=*/5.0);

  sim.Advance(params, 1.0);
  ASSERT_EQ(sim.view().player_hp, 55);
  sim.Advance(params, 1.0);  // down to 10, then five pools' worth of healing
  EXPECT_EQ(sim.view().player_hp, 100);
}

// A cleared map restores HP for free on the respawn, so a turn spent healing
// there would gain nothing. This can happen because a skill on its own clock
// can kill the last mob before the attack is aimed.
TEST(CombatSimTest, AHealingCastIsNotSpentOnAnEmptyMap) {
  Mob snail = MakeMob("Snail", 10);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 0.0, 1)});
  AddAutoAttack(params, /*interval=*/1.0, /*damage=*/10.0);
  GivePlayerHp(params, /*max_hp=*/100, /*interval=*/1.0, /*damage=*/80.0);
  AddHeal(params, /*fraction=*/0.5);

  // The hit takes them below a quarter, and the cast clears the map before the
  // attack is chosen.
  sim.Advance(params, 1.0);
  ASSERT_TRUE(sim.respawning());
  EXPECT_EQ(sim.view().player_hp, 20);
  EXPECT_EQ(sim.view().attack_name, "");
}

// Empowered Arrows: the Sniper's Piercing Arrow is upgraded every fourth shot.
// Three normal attacks, then the bigger one, not the bigger one first.
TEST(CombatSimTest, LandsAnEmpoweredSwingOnceEveryNth) {
  Mob snail = MakeMob("Snail", 20);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 1.0, 1)});
  SetEmpoweredForm(params.attacks[0], /*every=*/4, /*damage=*/10.0);

  for (int i = 0; i < 3; ++i) {
    sim.Advance(params, 1.0);
  }
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.85, 1e-9);
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.35, 1e-9)
      << "the fourth swing lands the empowered form";
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.30, 1e-9)
      << "and the count starts again from the fifth";
}

// Creeping Toxin: the poison pool ticks, and every fourth tick detonates
// instead. The same swap on the other clock, counted in pulses instead of
// attacks.
TEST(CombatSimTest, LandsAnEmpoweredPulseOnceEveryNth) {
  Mob snail = MakeMob("Snail", 20);
  CombatSim sim;
  // An attack worth nothing, so only the summon moves the bar.
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 0.0, 1)});
  AddAutoAttack(params, /*interval=*/1.0, /*damage=*/1.0);
  SetEmpoweredForm(params.auto_attacks[0], /*every=*/4, /*damage=*/8.0);

  for (int i = 0; i < 3; ++i) {
    sim.Advance(params, 1.0);
  }
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.85, 1e-9);
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.45, 1e-9)
      << "the fourth pulse detonates instead of ticking";
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.40, 1e-9)
      << "and the count starts again from the fifth";
}

// The count toward the bigger form belongs to the character, like cooldowns:
// three attacks used count wherever they landed.
TEST(CombatSimTest, TheEmpoweredCountSurvivesAChangeOfEncounter) {
  Mob snail = MakeMob("Snail", 20);
  CombatSim sim;
  CombatParams field = MakeParams(1.0, 1000.0, {MakeType(&snail, 1.0, 1)});
  SetEmpoweredForm(field.attacks[0], /*every=*/4, /*damage=*/10.0);
  for (int i = 0; i < 3; ++i) {
    sim.Advance(field, 1.0);
  }

  CombatParams forest = field;
  forest.encounter = "forest";
  // The fourth attack is the fourth wherever it lands, so it detonates on the
  // new mob this encounter put in front.
  sim.Advance(forest, 1.0);
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.5, 1e-9);
}

// The same holds for the cooldown on a strike an attack triggers, which runs
// alongside the attack's own: a Night Lord leaving keeps Showdown's cooldown
// just as they keep the shuriken's.
TEST(CombatSimTest, TheSideStrikesWaitSurvivesAChangeOfEncounter) {
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
  // The mob is untouched: the attack itself deals nothing, and the strike that
  // would is still a hundred seconds away.
  EXPECT_NEAR(sim.view().target_hp_fraction, 1.0, 1e-9);
}

// The two clocks count separately. An attack landing its empowered form must
// not advance the summon's count, or the Sniper's Piercing Arrow would trigger
// a pool it has nothing to do with.
TEST(CombatSimTest, EachClockCountsItsOwnEmpoweredRound) {
  Mob snail = MakeMob("Snail", 20);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 1.0, 1)});
  SetEmpoweredForm(params.attacks[0], /*every=*/2, /*damage=*/3.0);
  AddAutoAttack(params, /*interval=*/1.0, /*damage=*/1.0);
  SetEmpoweredForm(params.auto_attacks[0], /*every=*/4, /*damage=*/40.0);

  // Three seconds in: the attack has landed its form once, on the second of
  // three, for 1 + 3 + 1. The summon has pulsed three times and must still be
  // waiting for its fourth, so all three are worth 1.
  for (int i = 0; i < 3; ++i) {
    sim.Advance(params, 1.0);
  }
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.60, 1e-9);
}

// It replaces the attack instead of adding to it: four attacks are three normal
// ones and one empowered, never four plus a fifth.
TEST(CombatSimTest, AnEmpoweredSwingReplacesTheOneItLandsFor) {
  Mob snail = MakeMob("Snail", 20);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 1.0, 1)});
  SetEmpoweredForm(params.attacks[0], /*every=*/4, /*damage=*/5.0);

  for (int i = 0; i < 4; ++i) {
    sim.Advance(params, 1.0);
  }
  // 1 + 1 + 1 + 5 of 20. Adding instead of replacing would leave 0.45.
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.60, 1e-9);
}

// The empowered form has its own, wider reach than the attack it replaces, so
// on its turn it hits mobs the normal attack never touches.
TEST(CombatSimTest, AnEmpoweredSwingBringsItsOwnReach) {
  Mob snail = MakeMob("Snail", 10);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 1.0, 3)});
  SetEmpoweredForm(params.attacks[0], /*every=*/2, /*damage=*/10.0,
                   /*reach=*/3);

  sim.Advance(params, 1.0);  // ordinary: one mob, no kill
  EXPECT_EQ(sim.view().kills_this_step[0], 0);
  sim.Advance(params, 1.0);  // empowered: all three at once
  EXPECT_EQ(sim.view().kills_this_step[0], 3);
}

// Divine Judgment: Blast brands what it hits, and the brand belongs to the
// enemy. A monster joining a fight already in progress starts its own count
// from zero instead of inheriting the attack's count.
TEST(CombatSimTest, ABrandRidesTheEnemyRatherThanTheSwing) {
  Mob soft = MakeMob("Soft", 20);
  Mob tough = MakeMob("Tough", 20);
  CombatSim sim;
  CombatParams params = MakeParams(
      1.0, 1000.0, {MakeType(&soft, 20.0, 1), MakeType(&tough, 1.0, 1)});
  SetEmpoweredForm(params.attacks[0], /*every=*/2, /*damage=*/6.0, /*reach=*/1,
                   /*marks=*/true);

  sim.Advance(params, 1.0);
  ASSERT_EQ(sim.view().target_name, "Tough")
      << "the soft one dies to one strike";
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.95, 1e-9)
      << "the newcomer's first strike is an ordinary one, not the swing's 2nd";
  sim.Advance(params, 1.0);
  // 19 - (1 + 6) of 20. Replacing the strike instead of adding to it would
  // leave 13, and inheriting the attack's count would have triggered one attack
  // sooner.
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.60, 1e-9)
      << "the mark goes off on the second strike IT has taken, on top of it";
}

// With marks, the form never replaces the whole attack: it triggers on the mobs
// whose marks are due, and everything else the attack reached takes its normal
// strike.
TEST(CombatSimTest, AMarkGoesOffOnlyOnTheEnemyThatEarnedIt) {
  Mob soft = MakeMob("Soft", 20);
  Mob tough = MakeMob("Tough", 20);
  CombatSim sim;
  CombatParams params = MakeParams(
      1.0, 1000.0, {MakeType(&soft, 20.0, 1), MakeType(&tough, 1.0, 2)},
      /*reach=*/2);
  SetEmpoweredForm(params.attacks[0], /*every=*/2, /*damage=*/6.0, /*reach=*/2,
                   /*marks=*/true);

  // The first attack kills the weak mob and marks the tough one beside it. The
  // second reaches both tough ones: one is due, the other has only just
  // arrived.
  sim.Advance(params, 1.0);
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.60, 1e-9)
      << "12 of 20: one ordinary strike, then a second with the mark on top";
  const EngagedGroup* group = FindGroup(sim.view().engaged_groups, "Tough");
  ASSERT_NE(group, nullptr);
  EXPECT_EQ(group->count, 2);
  EXPECT_NEAR(group->hp_fraction, 0.775, 1e-9)
      << "12 and 19 of 20 apiece: only one of them was due";
}

// Attacks are chosen by damage per second, so an attack that lands a much
// bigger form every few uses must be weighed on the average of the two. On its
// normal form alone, this one loses to the basic attack.
TEST(CombatSimTest, WeighsAnEmpoweredSwingIntoTheChoice) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 3.0, 1)});
  params.attacks.push_back(MakeSkill("Piercing Arrow", 2.0, /*cooldown=*/0.0));
  SetEmpoweredForm(params.attacks[1], /*every=*/4, /*damage=*/10.0);

  // 2 + (10 - 2) / 4 = 4 per attack, against the basic attack's 3.
  sim.Advance(params, 0.1);
  EXPECT_EQ(sim.view().attack_name, "Piercing Arrow");
}

// Final Pact: a passive that catches the hit that would have killed the player.
// They survive with full HP and the fight continues. See AdvanceCombat for what
// dying costs when it isn't caught.
TEST(CombatSimTest, APactCatchesTheHitThatWouldHaveKilled) {
  Mob snail = MakeMob("Snail", 1000);  // too tough to kill, so hits keep coming
  CombatSim sim;
  CombatParams params = MakeParams(10.0, 1000.0, {MakeType(&snail, 1.0, 1)});
  GivePlayerHp(params, 100, /*interval=*/1.0, /*damage=*/60.0);
  params.revive_cooldown_seconds = 10.0;

  sim.Advance(params, 1.0);
  ASSERT_EQ(sim.view().player_hp, 40);

  sim.Advance(params, 1.0);
  EXPECT_FALSE(sim.view().died_this_step);
  EXPECT_EQ(sim.view().player_hp, 100);

  // The next one within the cooldown is a real death.
  sim.Advance(params, 1.0);
  ASSERT_EQ(sim.view().player_hp, 40);
  sim.Advance(params, 1.0);
  EXPECT_TRUE(sim.view().died_this_step);
}

TEST(CombatSimTest, APactCatchesAgainOnceItsWaitIsOut) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(10.0, 1000.0, {MakeType(&snail, 1.0, 1)});
  GivePlayerHp(params, 100, /*interval=*/1.0, /*damage=*/60.0);
  params.revive_cooldown_seconds = 2.0;

  sim.Advance(params, 1.0);
  sim.Advance(params, 1.0);
  ASSERT_EQ(sim.view().player_hp, 100);  // caught, and the wait starts

  sim.Advance(params, 1.0);
  ASSERT_EQ(sim.view().player_hp, 40);
  sim.Advance(params, 1.0);  // two seconds on, so it catches this one too
  EXPECT_FALSE(sim.view().died_this_step);
  EXPECT_EQ(sim.view().player_hp, 100);
}

// Invincible Belief's shape: below 15% HP it heals 20% per second for three
// seconds, then goes on cooldown. It triggers on nearly dying, while a pact
// triggers on dying, so both can be carried at once.
CombatParams EmergencyHealParams(Mob& snail, double damage) {
  CombatParams params = MakeParams(10.0, 1000.0, {MakeType(&snail, 1.0, 1)});
  GivePlayerHp(params, 100, /*interval=*/1.0, damage);
  params.emergency_heal = {0.20, 3.0, 0.15, 20.0};
  return params;
}

TEST(CombatSimTest, TheEmergencyHealPoursOnceThePoolIsNearlyEmpty) {
  Mob snail = MakeMob("Snail", 1000);  // too tough to kill, so the hits keep
  CombatSim sim;
  CombatParams params = EmergencyHealParams(snail, /*damage=*/10.0);

  // Down to 20 with nothing healed: the threshold is 15 and they're above it.
  for (int step = 0; step < 8; ++step) {
    sim.Advance(params, 1.0);
  }
  EXPECT_EQ(sim.view().player_hp, 20);

  // The ninth hit puts them below it, so the next second heals 20.
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.view().player_hp, 30);
  // Two more seconds of healing against two more hits, and then it stops.
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.view().player_hp, 40);
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.view().player_hp, 50);
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.view().player_hp, 40);
}

TEST(CombatSimTest, TheEmergencyHealWaitsOutItsCooldownAndFillsNoFurther) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = EmergencyHealParams(snail, /*damage=*/10.0);

  // Once it has fired, a second dip within the cooldown isn't caught: HP runs
  // down to zero and the player dies.
  for (int step = 0; step < 20 && !sim.view().died_this_step; ++step) {
    sim.Advance(params, 1.0);
  }
  EXPECT_TRUE(sim.view().died_this_step);
}

TEST(CombatSimTest, TheEmergencyHealNeverPoursPastThePool) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  // One hit takes 90 of the 100, so three seconds of 20 would overfill.
  CombatParams params = EmergencyHealParams(snail, /*damage=*/90.0);
  params.hit_seconds = 100.0;  // and no second hit to spend it on

  for (int step = 0; step < 5; ++step) {
    sim.Advance(params, 1.0);
  }
  EXPECT_EQ(sim.view().player_hp, 100);
}

// Trickblade's shape: ten enemies for 10, or one for 300 while a wound is three
// stacks deep. The wound is left by a different attack, so a test must use that
// one first.
AttackOption MakeWoundedSwing() {
  AttackOption blade = MakeSkill("Trickblade", 10.0, /*cooldown=*/14.0);
  blade.max_enemies = 10;
  std::shared_ptr<AttackOption> form = std::make_shared<AttackOption>(
      MakeSkill("Trickblade: Finish", 300.0, /*cooldown=*/20.0));
  blade.wound_max_stacks = 3;
  blade.wound_form = form;
  return blade;
}

// The attack that leaves the wound: `stacks` deep, for ten seconds.
AttackOption MakeWoundingSwing(int stacks, int reach = 1) {
  AttackOption blow = MakeSkill("Sonic Blow", 1.0, /*cooldown=*/0.0);
  blow.max_enemies = reach;
  blow.wound_stacks = stacks;
  blow.wound_max_stacks = 3;
  blow.wound_seconds = 10.0;
  return blow;
}

// One Sonic Blow fills the wound, so the next Trickblade is the stronger form:
// 300 on one enemy instead of 10 on each of ten, with the form's longer
// cooldown instead of the skill's own.
TEST(CombatSimTest, AFullWoundPutsTheHeavierFormInThePressesPlace) {
  Mob boss = MakeMob("Zakum", 100000);
  CombatParams params = MakeParams(1.0, 0.0, {MakeType(&boss, 0.0, 1)});
  params.attacks.push_back(MakeWoundingSwing(3));
  params.attacks.push_back(MakeWoundedSwing());

  CombatSim sim;
  // Nothing is wounded yet, so the key press gives the spread.
  sim.Advance(params, 1.0);
  double dealt = 100000 * (1.0 - sim.view().roster.front().hp_fraction);
  EXPECT_LT(dealt, 300.0);

  // Sonic Blow lands, and from here Trickblade is the five slashes: three of
  // them in the next minute on the form's twenty-second cooldown, not four on
  // the skill's own fourteen.
  for (int step = 0; step < 60; ++step) {
    sim.Advance(params, 1.0);
  }
  double total = 100000 * (1.0 - sim.view().roster.front().hp_fraction);
  EXPECT_GT(total, 3 * 300.0);
  EXPECT_LT(total, 4 * 300.0);
}

// The wound is on one monster: a new wound moves it from whoever had it, and a
// wounded monster that dies takes it with it. Either way, the key press falls
// back to the spread.
TEST(CombatSimTest, AWoundDiesWithItsMonster) {
  Mob snail = MakeMob("Snail", 5);
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 0.0, 2)});
  // Wide enough to kill what it wounds, which is the case being tested.
  AttackOption blow = MakeWoundingSwing(3, /*reach=*/2);
  blow.damage_per_hit = {100.0};
  params.attacks.push_back(std::move(blow));
  params.attacks.push_back(MakeWoundedSwing());

  CombatSim sim;
  sim.Advance(params, 1.0);
  sim.Advance(params, 1.0);
  // Nothing is left with a wound, so there's nothing to slash.
  EXPECT_TRUE(sim.view().roster.empty());
}

// GMS picks the wound's target by max HP, not current HP, so a snail beside a
// boss never gets it, and killing the snail leaves the wound where it was.
TEST(CombatSimTest, AWoundGoesToTheBiggestEnemyTheSwingReached) {
  Mob snail = MakeMob("Snail", 10);
  Mob boss = MakeMob("Zakum", 100000);
  CombatParams params = MakeParams(
      1.0, 0.0, {MakeType(&snail, 0.0, 1), MakeType(&boss, 0.0, 1)}, 2);
  AttackOption blow = MakeWoundingSwing(3, /*reach=*/2);
  blow.damage_per_hit = {20.0, 1.0};  // clears the snail, tickles the boss
  params.attacks.push_back(std::move(blow));
  AttackOption blade = MakeWoundedSwing();
  blade.damage_per_hit = {50.0, 50.0};
  std::shared_ptr<AttackOption> form = std::make_shared<AttackOption>(
      MakeSkill("Trickblade: Finish", 300.0, /*cooldown=*/20.0));
  form->damage_per_hit = {50.0, 300.0};
  blade.wound_form = form;
  params.attacks.push_back(std::move(blade));

  CombatSim sim;
  for (int step = 0; step < 60; ++step) {
    sim.Advance(params, 1.0);
  }
  // The snail is long dead and the boss is still being slashed, which wouldn't
  // happen if the wound had followed the snail.
  ASSERT_EQ(sim.view().roster.size(), 1u);
  EXPECT_GT(100000 * (1.0 - sim.view().roster.front().hp_fraction), 3 * 300.0);
}

// Trickblade's invulnerability comes only with the stronger form: the spread it
// uses with nothing wounded triggers nothing. Checked through damage, since the
// buffed table here is the same attacks doubled.
TEST(CombatSimTest, AFormOnlyBuffIsNotRaisedByTheOrdinaryPress) {
  Mob boss = MakeMob("Zakum", 1000000);
  CombatParams params = MakeParams(1.0, 0.0, {MakeType(&boss, 0.0, 1)});
  params.attacks.push_back(MakeWoundingSwing(3));
  params.attacks.push_back(MakeWoundedSwing());
  BuffOption shelter;
  shelter.name = "Trickblade";
  shelter.duration_seconds = 1000.0;
  shelter.laid_by_attack = 2;
  shelter.needs_wound_form = true;
  params.buffs.push_back(std::move(shelter));
  AttackSet set;
  set.attacks = params.attacks;
  for (AttackOption& attack : set.attacks) {
    for (double& damage : attack.damage_per_hit) {
      damage *= 2.0;
    }
  }
  params.buffed[1] = std::move(set);

  CombatSim sim;
  // Trickblade goes first, hitting hardest. Nothing is wounded, so it uses the
  // spread for 10 and triggers nothing. Sonic Blow fills the wound after it and
  // lands 1 per second while Trickblade recharges.
  for (int step = 0; step < 15; ++step) {
    sim.Advance(params, 1.0);
  }
  EXPECT_NEAR(sim.view().target_hp_fraction, 1.0 - 24.0 / 1000000.0, 1e-9);
}

// Gives `params` a timed buff: while it's up, every attack hits `factor` times
// as hard. Its table is the same attacks with bigger numbers, which is what
// ComputeCombatParams really builds.
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
  params.buffed[1] = std::move(set);
}

// Gives `params` a buff rolled on every attack landed, in `stacks` stacks with
// their own durations. Each stack's table hits `factor` times the one below, so
// the damage shows which one priced an attack.
void GiveRolledBuff(CombatParams& params, int stacks, double duration,
                    double chance, bool needs_afflicted = false) {
  for (int stack = 0; stack < stacks; ++stack) {
    BuffOption buff;
    buff.name = "Empirical Knowledge";
    buff.duration_seconds = duration;
    buff.raise_chance = chance;
    buff.needs_afflicted_target = needs_afflicted;
    params.buffs.push_back(std::move(buff));
  }
  // One table per stack count, which covers every mask a prefix can make.
  for (int held = 1; held <= stacks; ++held) {
    AttackSet set;
    set.attacks = params.attacks;
    for (AttackOption& attack : set.attacks) {
      for (double& damage : attack.damage_per_hit) {
        damage *= 1.0 + held;
      }
    }
    params.buffed[(1 << held) - 1] = std::move(set);
  }
}

// One stack per attack, up to three at once, and the mask is always a prefix of
// the group. That's why the windows are kept in order.
TEST(CombatSimTest, ARolledBuffGathersOneHelpingPerSwing) {
  Mob snail = MakeMob("Snail", 1000000);
  CombatParams params = MakeParams(1.0, 1e9, {MakeType(&snail, 10.0, 1)});
  GiveRolledBuff(params, /*stacks=*/3, /*duration=*/100.0, /*chance=*/1.0);

  CombatSim sim;
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.buff_mask(), 1);
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.buff_mask(), 3);
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.buff_mask(), 7);
  // Three stacks still show as one buff in the charge bar's dots.
  EXPECT_EQ(sim.view().buff_count, 1);
  // A full stack gains nothing: each stack lasts its own duration.
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.buff_mask(), 7);
}

// The oldest stack expires first and the rest shift down, so the mask becomes 3
// instead of leaving a gap at the front.
TEST(CombatSimTest, ALapsedHelpingLeavesNoHoleInThePile) {
  Mob snail = MakeMob("Snail", 1000000);
  CombatParams params = MakeParams(1.0, 1e9, {MakeType(&snail, 10.0, 1)});
  GiveRolledBuff(params, /*stacks=*/3, /*duration=*/2.5, /*chance=*/1.0);

  CombatSim sim;
  for (int step = 0; step < 3; ++step) {
    sim.Advance(params, 1.0);
  }
  ASSERT_EQ(sim.buff_mask(), 7);
  // The first stack expires half a second into the fourth attack, and the
  // fourth roll fills the freed slot.
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.buff_mask(), 7);
  // Nothing attacks, so nothing is gathered and they expire oldest first. This
  // applies to every table, since the attack is chosen from whichever table the
  // stacks put the fight in.
  params.attacks[0].swing_seconds = 1000.0;
  for (std::pair<const int, AttackSet>& window : params.buffed) {
    window.second.attacks[0].swing_seconds = 1000.0;
  }
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.buff_mask(), 3);
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.buff_mask(), 1);
}

// Thief's Cunning: the buff triggers only on an attack that hits an enemy
// already afflicted, so a character who inflicts nothing never triggers it.
TEST(CombatSimTest, ABuffNeedingAnAfflictedEnemyWaitsForOne) {
  Mob snail = MakeMob("Snail", 1000000);
  CombatParams params = MakeParams(1.0, 1e9, {MakeType(&snail, 10.0, 1)});
  GiveRolledBuff(params, /*stacks=*/1, /*duration=*/100.0, /*chance=*/1.0,
                 /*needs_afflicted=*/true);

  CombatSim unarmed;
  for (int step = 0; step < 5; ++step) {
    unarmed.Advance(params, 1.0);
  }
  EXPECT_EQ(unarmed.buff_mask(), 0);

  // With ice on the attack, the first one lands on a clean enemy and the second
  // finds the one it just froze.
  params.attacks[0].freeze_seconds = 10.0;
  CombatSim frozen;
  frozen.Advance(params, 1.0);
  EXPECT_EQ(frozen.buff_mask(), 0);
  frozen.Advance(params, 1.0);
  EXPECT_EQ(frozen.buff_mask(), 1);
}

// Gives `params` a buff that loads an attack: while it's up, the character can
// fire the loaded attack, only `charges` times per cast. Repeating Crossbow
// Cartridge works this way.
void GiveMagazine(CombatParams& params, double duration, double cooldown,
                  int charges, double damage) {
  AttackOption loaded;
  loaded.name = "Full Burst Shot";
  loaded.max_enemies = 1;
  loaded.swing_seconds = 1.0;
  loaded.charges = charges;
  loaded.damage_per_hit.assign(params.types.size(), damage);
  BuffOption buff;
  buff.name = "Repeating Crossbow Cartridge";
  buff.duration_seconds = duration;
  buff.cooldown_seconds = cooldown;
  buff.magazine_attack = static_cast<int>(params.attacks.size());
  params.attacks.push_back(std::move(loaded));
  params.buffs.push_back(std::move(buff));
  AttackSet set;
  set.attacks = params.attacks;
  set.auto_attacks = params.auto_attacks;
  set.triggered_attacks = params.triggered_attacks;
  params.buffed[1] = std::move(set);
}

// Runs `seconds` of fight in quarter-second steps and totals the damage.
double DamageOver(CombatSim& sim, const CombatParams& params, double seconds) {
  double total = 0.0;
  for (double t = 0.0; t < seconds; t += 0.25) {
    sim.Advance(params, 0.25);
    total += sim.view().damage_this_step;
  }
  return total;
}

// Poison Nova's shape: the clouds have no key of their own and go off when the
// named skill is pressed. One charge over a window with several presses means
// the first press uses it all.
TEST(CombatSimTest, ALoadGoesOffOnThePressThatSpendsIt) {
  Mob boss = MakeMob("Zakum", 1000000);
  CombatSim sim;
  CombatParams params =
      MakeParams(1.0, 0.0, {MakeType(&boss, 0.0, 1)}, 1, "zakum");
  // The normal attack lands 10 per second and spends the load. The clouds are
  // laid by a separate attack on a twenty-second cooldown.
  params.attacks[0].damage_per_hit.assign(params.types.size(), 10.0);
  params.attacks.push_back(MakeSkill("Nova", /*damage=*/1.0,
                                     /*cooldown=*/20.0));
  GiveMagazine(params, /*duration=*/10.0, /*cooldown=*/0.0, /*charges=*/1,
               /*damage=*/500.0);
  int loaded = static_cast<int>(params.attacks.size()) - 1;
  params.attacks[loaded].spent_by_attack = 0;
  params.attacks[0].loaded =
      std::make_shared<AttackOption>(params.attacks[loaded]);
  params.attacks[0].loaded_attack = loaded;
  // Laid by the attack that carries it, as in Poison Nova: the clouds go up on
  // the cast instead of on the buff's own clock, and that attack's cooldown
  // decides how often they can be laid again.
  params.buffs.back().laid_by_attack = 1;
  params.buffs.back().raised_on_cast = true;
  params.buffed[1].attacks = params.attacks;

  // One second laying the clouds for 1, nine attacks of 10, and the charge
  // spent on the first of them. The laying attack's cooldown stops the buff
  // refreshing on every press.
  EXPECT_DOUBLE_EQ(DamageOver(sim, params, 10.0), 1.0 + 90.0 + 500.0);
  // The clouds have expired and Nova is still recharging: plain presses only.
  EXPECT_DOUBLE_EQ(DamageOver(sim, params, 10.0), 100.0);
  // Nova is ready again and lays a new load.
  EXPECT_DOUBLE_EQ(DamageOver(sim, params, 10.0), 1.0 + 90.0 + 500.0);
}

// Eight cartridges and a minute to fire them: it stops at eight, the count goes
// with the buff, and the next cast brings a new load.
TEST(CombatSimTest, AMagazineFiresItsChargesAndNoMore) {
  Mob boss = MakeMob("Zakum", 1000000);
  CombatSim sim;
  CombatParams params =
      MakeParams(1.0, 0.0, {MakeType(&boss, 0.0, 1)}, 1, "zakum");
  GiveMagazine(params, /*duration=*/30.0, /*cooldown=*/120.0, /*charges=*/8,
               /*damage=*/100.0);

  // Well past the eighth shot but still within the buff: the empty magazine
  // stops it, not the clock.
  EXPECT_DOUBLE_EQ(DamageOver(sim, params, 25.0), 800.0);
  // The buff expires and the cooldown runs out at 120s, so nothing lands until
  // then.
  EXPECT_DOUBLE_EQ(DamageOver(sim, params, 90.0), 0.0);
  // Fully reloaded, and spent again.
  EXPECT_DOUBLE_EQ(DamageOver(sim, params, 25.0), 800.0);
}

// Throw Blasting's shape: a press takes several charges from one bank and lands
// the strike once for each. Eight charges at three per press is three presses:
// three, three, and the last two.
TEST(CombatSimTest, APressSpendsSeveralChargesAndTheLastTakesWhatIsLeft) {
  Mob boss = MakeMob("Zakum", 1000000);
  CombatSim sim;
  CombatParams params =
      MakeParams(1.0, 0.0, {MakeType(&boss, 0.0, 1)}, 1, "zakum");
  // The attack itself is worth nothing, so only the load does damage.
  params.attacks[0].damage_per_hit.assign(params.types.size(), 0.0);
  GiveMagazine(params, /*duration=*/30.0, /*cooldown=*/120.0, /*charges=*/8,
               /*damage=*/100.0);
  int loaded = static_cast<int>(params.attacks.size()) - 1;
  params.attacks[loaded].charges_per_swing = 3;
  params.attacks[loaded].spent_by_attack = 0;
  params.attacks[0].loaded =
      std::make_shared<AttackOption>(params.attacks[loaded]);
  params.attacks[0].loaded_attack = loaded;
  params.buffed[1].attacks = params.attacks;

  // Four seconds is four presses, and only three find charges. One at a time,
  // the eight would still be going.
  EXPECT_DOUBLE_EQ(DamageOver(sim, params, 4.0), 800.0);
}

// Throw Blasting's passive half: the bank refills on its own clock, not while a
// cast of the buff is active, and resumes as soon as those charges are gone.
TEST(CombatSimTest, ABankFillsItselfOnlyOnceTheLoadIsSpent) {
  Mob boss = MakeMob("Zakum", 1000000);
  CombatSim sim;
  CombatParams params =
      MakeParams(1.0, 0.0, {MakeType(&boss, 0.0, 1)}, 1, "zakum");
  params.attacks[0].damage_per_hit.assign(params.types.size(), 0.0);
  GiveMagazine(params, /*duration=*/30.0, /*cooldown=*/120.0, /*charges=*/4,
               /*damage=*/100.0);
  int loaded = static_cast<int>(params.attacks.size()) - 1;
  params.attacks[loaded].recharge_seconds = 4.0;
  params.attacks[loaded].recharge_max = 1;
  params.attacks[loaded].spent_by_attack = 0;
  params.attacks[0].loaded =
      std::make_shared<AttackOption>(params.attacks[loaded]);
  params.attacks[0].loaded_attack = loaded;
  params.buffed[1].attacks = params.attacks;

  // The buff goes up on the first step and gives four charges, which the first
  // four presses spend. If the clock had been running during them, there would
  // be more than four in those seconds.
  EXPECT_DOUBLE_EQ(DamageOver(sim, params, 4.0), 400.0);
  // The load is gone and the buff has twenty-six seconds left: the bank's own
  // charge lands every four of them.
  double later = DamageOver(sim, params, 24.0);
  EXPECT_GT(later, 0.0) << "the bank stayed silent for the rest of the buff";
  EXPECT_DOUBLE_EQ(later, 600.0);
}

// A buff too short to use its whole load loses the rest: cartridges don't carry
// past the duration.
TEST(CombatSimTest, AnUnspentMagazineEmptiesWithItsBuff) {
  Mob boss = MakeMob("Zakum", 1000000);
  CombatSim sim;
  CombatParams params =
      MakeParams(1.0, 0.0, {MakeType(&boss, 0.0, 1)}, 1, "zakum");
  GiveMagazine(params, /*duration=*/4.0, /*cooldown=*/120.0, /*charges=*/8,
               /*damage=*/100.0);

  double fired = DamageOver(sim, params, 20.0) / 100.0;
  EXPECT_GT(fired, 0.0);
  EXPECT_LT(fired, 8.0) << "the buff lapsed with charges still loaded";
}

// Angel of Balance dismisses Bahamut: GMS doesn't allow both summons at once,
// so the dragon stops while the angel is out.
TEST(CombatSimTest, ABuffPutsOutTheSummonItNames) {
  Mob boss = MakeMob("Zakum", 1000000);
  CombatSim sim;
  CombatParams params =
      MakeParams(1e9, 0.0, {MakeType(&boss, 0.0, 1)}, 1, "zakum");
  params.attacks[0].damage_per_hit.assign(params.types.size(), 0.0);
  AttackOption dragon;
  dragon.name = "Bahamut";
  dragon.interval_seconds = 1.0;
  dragon.damage_per_hit.assign(params.types.size(), 10.0);
  dragon.silenced_by_buff = 0;
  params.auto_attacks.push_back(std::move(dragon));
  GiveBuff(params, /*duration=*/5.0, /*cooldown=*/1000.0, /*factor=*/1.0);
  params.buffed[1].auto_attacks = params.auto_attacks;

  // The angel goes up on the first step and lasts five seconds, so the dragon
  // strikes during the remaining five.
  EXPECT_DOUBLE_EQ(DamageOver(sim, params, 10.0), 50.0);
}

// Angel of Balance's mark: the angel brands what it touches and the next holy
// attack spends the brand on one line. Built twice, differing only in the
// mark's value.
CombatParams MarkingParams(std::vector<TypeSpec> specs, double lift) {
  CombatParams params = MakeParams(1.0, 0.0, std::move(specs), 1, "zakum");
  params.attacks[0].lines = 4;
  params.attacks[0].damage_per_hit.assign(params.types.size(), 100.0);
  params.attacks[0].collects_mark_lift = true;
  AttackOption angel;
  angel.name = "Avenging Angel";
  angel.interval_seconds = 4.0;
  angel.max_pulses = 1;
  angel.damage_per_hit.assign(params.types.size(), 5.0);
  angel.mark_seconds = 60.0;
  angel.mark_lift_pct = lift;
  params.auto_attacks.push_back(std::move(angel));
  return params;
}

TEST(CombatSimTest, AMarkIsWorthOneLineAndIsThenSpent) {
  Mob boss = MakeMob("Zakum", 1000000);
  CombatSim marked;
  CombatSim plain;
  CombatParams with = MarkingParams({MakeType(&boss, 0.0, 1)}, 0.10);
  CombatParams without = MarkingParams({MakeType(&boss, 0.0, 1)}, 0.0);

  // One attack of 100 over four lines found the mark: a quarter of the bonus.
  EXPECT_DOUBLE_EQ(
      DamageOver(marked, with, 10.0) - DamageOver(plain, without, 10.0), 2.5);
  // The angel has fired its one strike and the mark is spent, so the following
  // attacks land the same however long the mark had left.
  EXPECT_DOUBLE_EQ(
      DamageOver(marked, with, 10.0) - DamageOver(plain, without, 10.0), 0.0);
}

// A buff lasting ten seconds but active only four in every five, with a summon
// striking throughout. The buff doubles the hit, so the damage shows which
// seconds it was active.
TEST(CombatSimTest, ADutyCycledBuffGrantsInBursts) {
  Mob boss = MakeMob("Zakum", 1000000);
  CombatSim sim;
  CombatParams params =
      MakeParams(1.0, 0.0, {MakeType(&boss, 0.0, 1)}, 1, "zakum");
  params.attacks[0].damage_per_hit.assign(params.types.size(), 10.0);
  GiveBuff(params, /*duration=*/10.0, /*cooldown=*/1000.0, /*factor=*/2.0);
  params.buffs.back().duty_seconds = 4.0;
  params.buffs.back().duty_interval_seconds = 5.0;

  // Eight of the ten seconds at 20 and two at 10. Active the whole time, it
  // would be 200.
  EXPECT_DOUBLE_EQ(DamageOver(sim, params, 10.0), 180.0);
}

// The buff's effect switches on and off, but the buff itself stays. A summon it
// cast keeps striking through the gaps, which is why the two masks are
// separate.
TEST(CombatSimTest, ADutyCycledBuffsSummonStrikesThroughTheGap) {
  Mob boss = MakeMob("Zakum", 1000000);
  CombatSim sim;
  CombatParams params =
      MakeParams(1e9, 0.0, {MakeType(&boss, 0.0, 1)}, 1, "zakum");
  params.attacks[0].damage_per_hit.assign(params.types.size(), 0.0);
  AttackOption pulse;
  pulse.name = "Avenging Angel";
  pulse.interval_seconds = 1.0;
  pulse.damage_per_hit.assign(params.types.size(), 100.0);
  pulse.needs_buff = 0;
  params.auto_attacks.push_back(std::move(pulse));
  GiveBuff(params, /*duration=*/10.0, /*cooldown=*/1000.0, /*factor=*/1.0);
  params.buffs.back().duty_seconds = 4.0;
  params.buffs.back().duty_interval_seconds = 5.0;
  params.buffed[1].auto_attacks = params.auto_attacks;

  // Ten strikes over the ten seconds it lasts, not the eight it's active.
  EXPECT_DOUBLE_EQ(DamageOver(sim, params, 10.0), 1000.0);
}

// A buff with two forms to choose between, as in Burning Soul Blade: a short
// dense one and a long thin one, each dealing damage through its own pulse.
// Their pulses are added as auto attacks tagged with the form that fires them.
void GiveStancedBuff(CombatParams& params, double cooldown, double dense_length,
                     double dense_damage, double thin_length,
                     double thin_damage) {
  BuffOption buff;
  buff.name = "Burning Soul Blade";
  buff.cooldown_seconds = cooldown;
  buff.duration_seconds = std::max(dense_length, thin_length);
  double lengths[] = {dense_length, thin_length};
  double damages[] = {dense_damage, thin_damage};
  for (int i = 0; i < 2; ++i) {
    AttackOption pulse;
    pulse.name = buff.name;
    pulse.interval_seconds = 1.0;
    pulse.damage_per_hit.assign(params.types.size(), damages[i]);
    pulse.needs_buff = 0;
    pulse.needs_buff_stance = i;
    StanceOption form;
    form.duration_seconds = lengths[i];
    form.pulse_interval_seconds = pulse.interval_seconds;
    form.pulse_attack = static_cast<int>(params.auto_attacks.size());
    buff.stances.push_back(form);
    params.auto_attacks.push_back(std::move(pulse));
  }
  params.buffs.push_back(std::move(buff));
  AttackSet set;
  set.attacks = params.attacks;
  set.auto_attacks = params.auto_attacks;
  set.triggered_attacks = params.triggered_attacks;
  params.buffed[1] = std::move(set);
}

// The dense form deals 2000 over 20 seconds and the thin one 20 a second, so
// the thin one needs 100 seconds. A shorter boss picks the dense one with no
// buff deciding it.
TEST(CombatSimTest, AShortFightRaisesTheDenseStance) {
  Mob boss = MakeMob("Zakum", 3000);
  CombatSim sim;
  CombatParams params =
      MakeParams(1e9, 0.0, {MakeType(&boss, 0.0, 1)}, 1, "zakum");
  params.reference_dps = 100.0;
  GiveStancedBuff(params, /*cooldown=*/120.0, /*dense_length=*/20.0,
                  /*dense_damage=*/100.0, /*thin_length=*/120.0,
                  /*thin_damage=*/20.0);

  // 3000 HP at 100 per second is 30 seconds left, well under the crossover.
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.view().damage_this_step, 100.0);
}

// A map refills on respawn, so it never ends however few mobs are left. The
// thin form's rate is what counts there, whatever the queue holds.
TEST(CombatSimTest, AMapRaisesTheThinStanceHoweverLowItRuns) {
  Mob snail = MakeMob("Snail", 1);
  CombatSim sim;
  CombatParams params = MakeParams(1e9, 600.0, {MakeType(&snail, 0.0, 1)});
  params.reference_dps = 100.0;
  GiveStancedBuff(params, /*cooldown=*/120.0, /*dense_length=*/20.0,
                  /*dense_damage=*/100.0, /*thin_length=*/120.0,
                  /*thin_damage=*/20.0);

  // One snail with 1 HP is a second of fight by the math a boss would use, and
  // the planted sword still goes up.
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.view().damage_this_step, 20.0);
}

// A boss that will outlast the dense form's payout gets the thin form, and
// keeps it when the fight gets short: GMS removed the key that swapped forms,
// so there's no way to change.
TEST(CombatSimTest, ALongFightRaisesTheThinStanceAndKeepsIt) {
  Mob boss = MakeMob("Zakum", 20000);
  CombatSim sim;
  CombatParams params =
      MakeParams(1e9, 0.0, {MakeType(&boss, 0.0, 1)}, 1, "zakum");
  params.reference_dps = 100.0;
  GiveStancedBuff(params, /*cooldown=*/120.0, /*dense_length=*/20.0,
                  /*dense_damage=*/100.0, /*thin_length=*/120.0,
                  /*thin_damage=*/20.0);

  // 20000 HP at 100 per second is 200 seconds left, over the crossover.
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.view().damage_this_step, 20.0);
  // Ten seconds later the boss is nearly dead, and the sword planted for two
  // minutes is still the one dealing damage.
  sim.Advance(params, 10.0);
  ASSERT_LT(sim.view().target_hp_fraction, 0.99);
  EXPECT_EQ(sim.view().damage_this_step, 200.0);
}

// Casting a buff takes time away from attacking: its animation is taken from
// the attack being charged, so the step it goes up on lands one attack fewer.
TEST(CombatSimTest, RaisingABuffCostsTheSwingItsAnimation) {
  Mob snail = MakeMob("Snail", 1e9);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 1e9, {MakeType(&snail, 10.0, 1)});
  params.attacks[0].name = "Hurricane";
  GiveBuff(params, /*duration=*/10.0, /*cooldown=*/60.0, /*factor=*/1.0);
  params.buffs[0].cast_seconds = 0.6;

  // A second of a one-second attack, minus the six-tenths the cast took: the
  // attack this step would have landed is still four-tenths short.
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.view().damage_this_step, 0.0);
  // A step this much longer than the cast ends with the cast long finished, so
  // the bar is back on the attack and shows what's left of its animation.
  EXPECT_EQ(sim.view().attack_name, "Hurricane");
  EXPECT_DOUBLE_EQ(sim.view().attack_fraction, 0.4);
  // The remainder carries over, so the attack the cast delayed lands next step.
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.view().damage_this_step, 10.0);
}

// Several buffs cast at once put the attack clock in debt, and the bar shows
// each cast in turn, filling over its own animation, instead of an empty bar
// under the name of an attack that isn't coming.
TEST(CombatSimTest, TheChargeBarNamesEachBuffBeingCast) {
  Mob snail = MakeMob("Snail", 1e9);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 1e9, {MakeType(&snail, 10.0, 1)});
  params.attacks[0].name = "Hurricane";
  GiveBuff(params, /*duration=*/10.0, /*cooldown=*/60.0, /*factor=*/1.0);
  GiveBuff(params, /*duration=*/10.0, /*cooldown=*/60.0, /*factor=*/1.0);
  params.buffs[0].name = "Epic Adventure";
  params.buffs[0].cast_seconds = 0.6;
  params.buffs[1].name = "Sharp Eyes";
  params.buffs[1].cast_seconds = 0.3;

  // Both go up on the first step: nine-tenths of a second of animation against
  // a one-second attack. The last cast is the one shown.
  sim.Advance(params, 0.1);
  EXPECT_EQ(sim.view().attack_name, "Sharp Eyes");
  EXPECT_NEAR(sim.view().attack_fraction, 1.0 / 3.0, 1e-9);
  sim.Advance(params, 0.1);
  EXPECT_EQ(sim.view().attack_name, "Sharp Eyes");
  EXPECT_NEAR(sim.view().attack_fraction, 2.0 / 3.0, 1e-9);

  // Finished, and the one below it takes the bar from the start.
  sim.Advance(params, 0.1);
  EXPECT_EQ(sim.view().attack_name, "Epic Adventure");
  EXPECT_NEAR(sim.view().attack_fraction, 0.0, 1e-9);
  sim.Advance(params, 0.3);
  EXPECT_EQ(sim.view().attack_name, "Epic Adventure");
  EXPECT_NEAR(sim.view().attack_fraction, 0.5, 1e-9);

  // Out of debt: the attack has the bar back, and nothing has landed yet.
  sim.Advance(params, 0.3);
  EXPECT_EQ(sim.view().attack_name, "Hurricane");
  EXPECT_NEAR(sim.view().attack_fraction, 0.0, 1e-9);
  EXPECT_EQ(sim.view().damage_this_step, 0.0);
}

// A cast over a partly charged attack also holds the bar for its whole
// animation. Whether the attack had enough charge banked to cover it doesn't
// matter; the character is casting either way.
TEST(CombatSimTest, ACastTakesTheBarOverAPartChargedSwing) {
  Mob snail = MakeMob("Snail", 1e9);
  CombatSim sim;
  CombatParams params = MakeParams(8.0, 1e9, {MakeType(&snail, 10.0, 1)});
  params.attacks[0].name = "Hurricane";
  GiveBuff(params, /*duration=*/1.0, /*cooldown=*/1.0, /*factor=*/1.0);
  params.buffs[0].name = "Sharp Eyes";
  params.buffs[0].cast_seconds = 0.5;

  // Five steps: the first cast, three steps of charging the attack, and the
  // buff coming back half a second into the attack's own clock.
  for (int step = 0; step < 5; ++step) {
    sim.Advance(params, 0.25);
  }
  ASSERT_EQ(sim.view().attack_name, "Sharp Eyes");
  EXPECT_DOUBLE_EQ(sim.view().attack_fraction, 0.5);

  // Done casting, and the attack has all the charge it had built.
  sim.Advance(params, 0.25);
  EXPECT_EQ(sim.view().attack_name, "Hurricane");
  EXPECT_DOUBLE_EQ(sim.view().attack_fraction, 0.0625);
}

// Smokescreen's shape: a buff that reduces the mob's hits instead of helping
// the player directly. The reduction ends with the buff, so the next hit is
// full again.
TEST(CombatSimTest, ABuffCanSoftenTheHitsWhileItStands) {
  Mob snail = MakeMob("Snail", 100000);
  CombatSim sim;
  CombatParams params = MakeParams(10.0, 1000.0, {MakeType(&snail, 1.0, 1)});
  GivePlayerHp(params, 100, /*interval=*/1.0, /*damage=*/10.0);
  GiveBuff(params, /*duration=*/2.0, /*cooldown=*/10.0, /*factor=*/1.0,
           /*heal=*/0.0, /*reduction=*/0.0, /*soften=*/0.5);

  // The buff heals after this hit instead of reducing it: it goes up after the
  // hit lands, which is the order every step runs in.
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.view().player_hp, 90);
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.view().player_hp, 85);
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.view().player_hp, 80);
  sim.Advance(params, 1.0);  // lapsed, and the wait is still running
  EXPECT_EQ(sim.view().player_hp, 70);
}

// Smokescreen from a party member: on the Shadower's clock, not the reader's,
// and costing the reader no attack to cast. It reduces hits like their own buff
// would and ends the same way, so the next hit is full again.
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
  params.buffs.push_back(std::move(ally));

  // Up after this step's hit lands, like the character's own buffs, and the
  // attack still lands, because nobody here cast anything.
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.view().player_hp, 90);
  EXPECT_EQ(sim.view().damage_this_step, 1.0);
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.view().player_hp, 85);
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.view().player_hp, 80);
  sim.Advance(params, 1.0);  // lapsed, and the caster's wait still running
  EXPECT_EQ(sim.view().player_hp, 70);
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.view().player_hp, 60);  // the wait closes, and it goes up again
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.view().player_hp, 55);
}

// Two damage reductions don't double up: the party's and the character's own
// multiply, like every damage reduction in the game.
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
  params.buffs.push_back(std::move(ally));

  sim.Advance(params, 1.0);
  ASSERT_EQ(sim.view().player_hp, 80);  // both go up behind this blow
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.view().player_hp, 75);  // a quarter of the hit, not none of it
}

// Holy Magic Shell's shape: a buff that blocks whole hits instead of reducing
// each one, and heals the HP of whoever it's cast on.
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
  params.buffed[1] = std::move(set);
}

// The shell waits until HP is low enough to be worth healing, blocks its count
// of hits entirely, and breaks as soon as the last is used, even with plenty of
// duration left.
TEST(CombatSimTest, AShellBlocksWholeHitsUntilItsCountRunsOut) {
  Mob snail = MakeMob("Snail", 1e9);
  CombatSim sim;
  CombatParams params = MakeParams(10.0, 1000.0, {MakeType(&snail, 1.0, 1)});
  GivePlayerHp(params, 100, /*interval=*/1.0, /*damage=*/40.0);
  GiveShield(params, /*hits=*/2, /*boss_soften=*/0.5);

  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.view().player_hp,
            60);  // full pool: nothing worth raising it for
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.view().player_hp,
            20);  // low enough, so it goes up behind the blow
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.view().player_hp, 20);
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.view().player_hp, 20);  // the second block, and the shell falls
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.view().player_hp, 0);
  EXPECT_TRUE(sim.view().died_this_step);
}

// A shell can't block a boss hit: the hit is reduced by its share instead, and
// uses no block, so the shell stays whole however many land.
TEST(CombatSimTest, AShellBluntsABossHitInsteadOfBlockingIt) {
  Mob zakum = MakeMob("Zakum", 1e9);
  zakum.set_boss(true);
  CombatSim sim;
  CombatParams params = MakeParams(10.0, 1000.0, {MakeType(&zakum, 1.0, 1)});
  GivePlayerHp(params, 1000, /*interval=*/1.0, /*damage=*/40.0);
  GiveShield(params, /*hits=*/2, /*boss_soften=*/0.5);

  // Cast on the first step instead of waiting for low HP: against a boss, one
  // hit could end the fight, so it goes up as soon as it's ready.
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.view().player_hp, 960);
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.view().player_hp, 940);
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.view().player_hp, 920);
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.view().player_hp,
            900);  // still blunting: no block was ever spent
}

// One shell over the whole party: an ally's cast heals this character and
// blocks their hits, on the caster's clock and costing them no attack.
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
  params.buffs.push_back(std::move(ally));

  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.view().player_hp, 60);
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.view().player_hp,
            70);  // the blow lands, then the heal answers it
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.view().player_hp, 70);
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.view().player_hp, 70);  // the second block, and the shell falls
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.view().player_hp, 30);
}

// Two shells don't double up: a hit uses one block from one of them, so the
// party's and the character's own together last twice as long, instead of being
// used two at a time.
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
  params.buffs.push_back(std::move(ally));

  sim.Advance(params, 1.0);
  ASSERT_EQ(sim.view().player_hp, 60);
  sim.Advance(params, 1.0);
  ASSERT_EQ(sim.view().player_hp, 20);  // both go up behind this blow
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.view().player_hp, 20);  // the character's own block pays for it
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.view().player_hp, 20);  // and the party's for the next
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.view().player_hp, 0);
}

// Puncture's shape: a weaker attack applies the buff instead of a cooldown
// casting it, and while it's up every attack hits `factor` times as hard.
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
  params.buffed[1] = std::move(set);
}

// The fight spends an attack applying the wound, returns to the strongest
// attack, and comes back when it expires. Checked through damage instead of
// attack_name(), which shows the attack being charged next.
TEST(CombatSimTest, TheFightSpendsASwingToLayALapsedBuff) {
  Mob snail = MakeMob("Snail", 10000);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 10.0, 1)});
  GiveWound(params, /*duration=*/3.0, /*factor=*/2.0);

  // 5: Puncture, the weakest attack available, landing unbuffed, since its
  // wound isn't up while it's being applied.
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.9995, 1e-9);
  // 40 twice: the strongest attack, doubled by the wound now active.
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.9955, 1e-9);
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.9915, 1e-9);
  // Three seconds in, the wound has expired, but the attack started while it
  // was active is committed to and finishes, landing 20 instead of 40.
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.9895, 1e-9);
  // Only then is it applied again, for another 5.
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.9890, 1e-9);
}

// Sword Illusion's shape, compared to Puncture's: a buff GMS grants "upon use"
// goes up before its own attack lands, so the attack that applies it is already
// buffed.
TEST(CombatSimTest, ABuffRaisedAtTheCastLiftsItsOwnSwing) {
  Mob snail = MakeMob("Snail", 10000);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 10.0, 1)});
  GiveWound(params, /*duration=*/3.0, /*factor=*/2.0);
  params.buffs[0].raised_on_cast = true;

  // 10 instead of Puncture's unbuffed 5: the buff started at the cast, so the
  // applying attack is priced under it.
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.9990, 1e-9);
  // And every attack after it, as before.
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.9950, 1e-9);
}

// The wound itself: a pulse that waits for the buff its skill applies, so it
// ticks only where a wound was left, not from the moment the skill is learned.
TEST(CombatSimTest, APulseGatedOnABuffWaitsForItToBeLaid) {
  Mob snail = MakeMob("Snail", 10000);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 10.0, 1)});
  AddAutoAttack(params, /*interval=*/1.0, /*damage=*/100.0);
  GiveWound(params, /*duration=*/3.0, /*factor=*/1.0);
  params.auto_attacks[0].name = "Puncture";
  params.auto_attacks[0].needs_buff = 0;
  params.buffed[1].auto_attacks = params.auto_attacks;

  // 5 for the attack that applies it and nothing from the pulse, since no wound
  // existed when the step began.
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.9995, 1e-9);
  // Now it ticks, alongside the strongest attack's 20.
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.9875, 1e-9);
}

// Elemental Fury's shape: a buff lengthened by the poisons already active when
// it's cast. The first cast finds nothing burning and lasts its base second;
// the second finds both burns and lasts three.
TEST(CombatSimTest, ABuffStandsLongerForTheBurnsAlreadyAlight) {
  Mob snail = MakeMob("Snail", 1000000);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 1e9, {MakeType(&snail, 100.0, 1)});
  // Two burns dealing nothing per tick, so they only change the count the buff
  // duration reads.
  params.attacks[0].dots.push_back(MakeBurn(0.0, 1.0, 30.0));
  params.attacks[0].dots.push_back(MakeBurn(0.0, 1.0, 30.0));
  params.attacks[0].dots[1].slot = 1;
  params.dot_count = 2;
  GiveBuff(params, /*duration=*/1.0, /*cooldown=*/4.0, /*factor=*/2.0);
  params.buffs[0].duration_seconds_per_dot = 1.0;
  params.buffs[0].dot_count_cap = 2;

  double taken[8] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
  double left = 1.0;
  for (int step = 0; step < 8; ++step) {
    sim.Advance(params, 1.0);
    taken[step] = (left - sim.view().target_hp_fraction) * 1000000.0;
    left = sim.view().target_hp_fraction;
  }
  // Nothing was burning when the first went up, so it lasted one second. By the
  // next cast both burns were active, and it lasted three.
  EXPECT_NEAR(taken[0], 200.0, 1e-9);
  EXPECT_NEAR(taken[1], 100.0, 1e-9);
  EXPECT_NEAR(taken[2], 100.0, 1e-9);
  EXPECT_NEAR(taken[3], 100.0, 1e-9);
  EXPECT_NEAR(taken[4], 200.0, 1e-9);
  EXPECT_NEAR(taken[5], 200.0, 1e-9);
  EXPECT_NEAR(taken[6], 200.0, 1e-9);
  EXPECT_NEAR(taken[7], 100.0, 1e-9);
}

// Inhuman Speed's passive half: it counts the character's attacks only while
// its own buff is down, and resumes the count where it left off when the buff
// expires.
TEST(CombatSimTest, ASilencedHalfCountsNothingWhileItsBuffStands) {
  Mob snail = MakeMob("Snail", 10000);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 10000.0, {MakeType(&snail, 0.0, 1)});
  AddTriggeredAttack(params, /*attacks=*/2, /*damage=*/100.0);
  params.triggered_attacks[0].silent_while_buff = true;
  params.triggered_attacks[0].needs_buff = 0;
  GiveBuff(params, /*duration=*/4.0, /*cooldown=*/1000.0, /*factor=*/1.0);
  params.buffed[1].triggered_attacks = params.triggered_attacks;

  // The buff goes up on the first step and lasts four seconds. Nothing fires
  // during them, however many attacks land.
  for (int step = 0; step < 4; ++step) {
    sim.Advance(params, 1.0);
    EXPECT_NEAR(sim.view().target_hp_fraction, 1.0, 1e-9) << "step " << step;
  }
  // It expires, and counting resumes: two attacks, one firing.
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.view().target_hp_fraction, 1.0, 1e-9);
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.99, 1e-9);
}

// Cry Valhalla's shape: a pulse landing several strikes at once that runs out
// before its buff does. Twelve strikes over four ticks here, three per tick,
// and the fifth tick, still within the buff, lands nothing.
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
  params.buffed[1].auto_attacks = params.auto_attacks;

  // Three strikes of 100 per tick, four ticks: 1200 of the snail's 100000.
  for (int step = 0; step < 4; ++step) {
    sim.Advance(params, 1.0);
  }
  ASSERT_NEAR(sim.view().target_hp_fraction, 0.988, 1e-9);
  // Four more seconds with the buff still up, and nothing more lands.
  for (int step = 0; step < 4; ++step) {
    sim.Advance(params, 1.0);
  }
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.988, 1e-9);
}

// Poison Chain's shape: a pulse that gains a step each time it fires and stays
// at the top of its ramp. Nine ticks here of 100, 200, 300, 300...: the ramp is
// two steps, so the third tick and every one after it lands 300.
TEST(CombatSimTest, ARampedPulseClimbsAStepAFiringAndPinsAtTheTop) {
  Mob snail = MakeMob("Snail", 100000);
  CombatSim sim;
  CombatParams params = MakeParams(1000.0, 1000.0, {MakeType(&snail, 10.0, 1)});
  AddAutoAttack(params, /*interval=*/1.0, /*damage=*/100.0);
  params.auto_attacks[0].name = "Poison Explosion";
  params.auto_attacks[0].max_pulses = 5;
  for (double damage : {200.0, 300.0}) {
    AttackOption form = params.auto_attacks[0];
    form.damage_per_hit.assign(params.types.size(), damage);
    params.auto_attacks[0].repeats.push_back(
        std::make_shared<const AttackOption>(std::move(form)));
  }
  GiveBuff(params, /*duration=*/100.0, /*cooldown=*/1000.0, /*factor=*/1.0);
  params.auto_attacks[0].needs_buff = 0;
  params.buffed[1].auto_attacks = params.auto_attacks;

  // 100 + 200 + 300 + 300 + 300 = 1200 of the snail's 100000, and the count
  // stops it there however long the buff lasts.
  for (int step = 0; step < 5; ++step) {
    sim.Advance(params, 1.0);
  }
  ASSERT_NEAR(sim.view().target_hp_fraction, 0.988, 1e-9);
  sim.Advance(params, 5.0);
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.988, 1e-9);
}

// The extra explosion GMS ends on: one more strike at the top of the ramp,
// landing with the last tick instead of an interval after it.
TEST(CombatSimTest, ARampedPulseGoesOutOnOneMoreStrikeAtTheTop) {
  Mob snail = MakeMob("Snail", 100000);
  CombatSim sim;
  CombatParams params = MakeParams(1000.0, 1000.0, {MakeType(&snail, 10.0, 1)});
  AddAutoAttack(params, /*interval=*/1.0, /*damage=*/100.0);
  params.auto_attacks[0].name = "Poison Explosion";
  params.auto_attacks[0].max_pulses = 3;
  params.auto_attacks[0].final_repeat_strike = true;
  AttackOption top = params.auto_attacks[0];
  top.damage_per_hit.assign(params.types.size(), 500.0);
  params.auto_attacks[0].repeats.push_back(
      std::make_shared<const AttackOption>(std::move(top)));
  GiveBuff(params, /*duration=*/100.0, /*cooldown=*/1000.0, /*factor=*/1.0);
  params.auto_attacks[0].needs_buff = 0;
  params.buffed[1].auto_attacks = params.auto_attacks;

  // Two seconds in: 100 and then 500, with the count not yet used up.
  sim.Advance(params, 1.0);
  ASSERT_NEAR(sim.view().target_hp_fraction, 0.999, 1e-9);
  sim.Advance(params, 1.0);
  ASSERT_NEAR(sim.view().target_hp_fraction, 0.994, 1e-9);
  // The third tick uses it up, so 500 lands and another 500 goes with it.
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.984, 1e-9);
  sim.Advance(params, 5.0);
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.984, 1e-9);
}

// The scroll bursting as it leaves: a strike of its own shape, landing with the
// last tick, as many times as it states.
TEST(CombatSimTest, APulseGoesOutOnAStrikeOfItsOwnShape) {
  Mob snail = MakeMob("Snail", 100000);
  CombatSim sim;
  CombatParams params = MakeParams(1000.0, 1000.0, {MakeType(&snail, 10.0, 1)});
  AddAutoAttack(params, /*interval=*/1.0, /*damage=*/100.0);
  params.auto_attacks[0].name = "Throwing Stars";
  params.auto_attacks[0].max_pulses = 3;
  AttackOption burst = params.auto_attacks[0];
  burst.damage_per_hit.assign(params.types.size(), 400.0);
  burst.strikes_per_pulse = 2;
  params.auto_attacks[0].final_strike =
      std::make_shared<const AttackOption>(std::move(burst));
  GiveBuff(params, /*duration=*/100.0, /*cooldown=*/1000.0, /*factor=*/1.0);
  params.auto_attacks[0].needs_buff = 0;
  params.buffed[1].auto_attacks = params.auto_attacks;

  // Two ordinary ticks of 100, the count not yet used up.
  sim.Advance(params, 2.0);
  ASSERT_NEAR(sim.view().target_hp_fraction, 0.998, 1e-9);
  // The third uses it up: 100, then two strikes of 400 with it.
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.989, 1e-9);
  sim.Advance(params, 5.0);
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.989, 1e-9);
}

// The count resets per cast, not per fight: the next cast gives all twelve
// again.
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
  params.buffed[1].auto_attacks = params.auto_attacks;

  // Two ticks of 300 while the first cast lasts, then nothing until it's ready
  // on the seventh second and pays another two.
  for (int step = 0; step < 6; ++step) {
    sim.Advance(params, 1.0);
  }
  ASSERT_NEAR(sim.view().target_hp_fraction, 0.994, 1e-9);
  for (int step = 0; step < 2; ++step) {
    sim.Advance(params, 1.0);
  }
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.988, 1e-9);
}

// The other half of the rule: an active wound is left alone. Nothing reapplies
// it early, so the strong attack takes every turn after the first.
TEST(CombatSimTest, ABuffStillStandingIsNotLaidAgain) {
  Mob snail = MakeMob("Snail", 10000);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 10.0, 1)});
  GiveWound(params, /*duration=*/1000.0, /*factor=*/2.0);

  for (int step = 0; step < 6; ++step) {
    sim.Advance(params, 1.0);
  }
  // 5 to apply it, then 40 five times.
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.9795, 1e-9);
}

// Dark Resonance's shape, and what makes a timed buff more than a passive: it
// expires before it's ready again, so the fight attacks buffed part of the time
// and unbuffed the rest.
TEST(CombatSimTest, ABuffLandsHarderUntilItLapses) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 10.0, 1)});
  GiveBuff(params, /*duration=*/2.0, /*cooldown=*/5.0, /*factor=*/2.0);

  sim.Advance(params, 1.0);  // up from the first step, so 20 rather than 10
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.98, 1e-9);
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.96, 1e-9);
  // After two seconds it expires, and the same attack is worth half as much.
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.95, 1e-9);
}

TEST(CombatSimTest, ABuffComesBackWhenItsWaitIsOut) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 10.0, 1)});
  GiveBuff(params, /*duration=*/2.0, /*cooldown=*/5.0, /*factor=*/2.0);

  for (int step = 0; step < 5; ++step) {
    sim.Advance(params, 1.0);
  }
  // 20, 20, then 10 three times: the buff is over and the cooldown isn't.
  ASSERT_NEAR(sim.view().target_hp_fraction, 0.93, 1e-9);
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.91, 1e-9);
}

// A buff belongs to the character, not the map: after a move it lasts the rest
// of its duration, then returns on the cooldown it was already on.
TEST(CombatSimTest, WalkingToAnotherMapKeepsTheBuffsClocks) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams field = MakeParams(1.0, 1000.0, {MakeType(&snail, 10.0, 1)});
  GiveBuff(field, /*duration=*/2.0, /*cooldown=*/5.0, /*factor=*/2.0);
  sim.Advance(field, 1.0);  // up, with a second of it left and four of wait

  CombatParams forest = field;
  forest.encounter = "forest";
  // The mob here is new, so the fractions measure this map.
  sim.Advance(forest, 1.0);
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.98, 1e-9)
      << "the buff walked over still standing";
  for (int step = 0; step < 3; ++step) {
    sim.Advance(forest, 1.0);
  }
  // 10 three times: it expires on the second step here, not two steps later,
  // and a new map doesn't reset the cooldown.
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.95, 1e-9);
  sim.Advance(forest, 1.0);
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.93, 1e-9)
      << "and it comes back on the wait it left the other map serving";
}

// Attacking fast has another benefit beyond damage: the buff comes back sooner.
// A five-second cooldown, less one second per attack landed in the meantime, is
// back in four steps instead of six.
TEST(CombatSimTest, AttackingShortensTheWaitForTheNextBuff) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 10.0, 1)});
  GiveBuff(params, /*duration=*/2.0, /*cooldown=*/5.0, /*factor=*/2.0,
           /*heal=*/0.0, /*reduction=*/1.0);

  for (int step = 0; step < 3; ++step) {
    sim.Advance(params, 1.0);
  }
  ASSERT_NEAR(sim.view().target_hp_fraction, 0.95, 1e-9);  // 20, 20, 10
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.93, 1e-9);  // up again already
}

TEST(CombatSimTest, ABuffHealsTheShareItPromisesWhenItGoesUp) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(10.0, 1000.0, {MakeType(&snail, 1.0, 1)});
  GivePlayerHp(params, 100, /*interval=*/1.0, /*damage=*/60.0);
  GiveBuff(params, /*duration=*/1.0, /*cooldown=*/1000.0, /*factor=*/1.0,
           /*heal=*/0.5);

  // The hit lands first, then the buff goes up and restores half the HP.
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.view().player_hp, 90);
  // Only when it goes up: the next hit isn't healed.
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.view().player_hp, 30);
}

// Mortal Blow's shape: a chance rolled once per attack that lands a share of it
// again on one enemy, and restores some HP when it does. Certain and doubled
// here, so random noise can't hide the roll.
TEST(CombatSimTest, AChanceCanLandOneEnemyHarderAndPayTheHitBack) {
  Mob snail = MakeMob("Snail", 1000);
  CombatParams params =
      MakeParams(1.0, 1000.0, {MakeType(&snail, 10.0, 4)}, /*reach=*/2);
  GivePlayerHp(params, 100, /*interval=*/1000.0, /*damage=*/0.0);
  CombatSim plain;
  plain.Advance(params, 1.0);
  // Two enemies, ten each, averaged into the one bar they share.
  ASSERT_EQ(plain.view().engaged_groups.size(), 1u);
  ASSERT_NEAR(plain.view().engaged_groups[0].hp_fraction, 0.99, 1e-9);

  params.attacks[0].procs.push_back(
      {/*chance=*/1.0, /*damage_pct=*/1.0, /*hp_recover_pct=*/0.25});
  CombatSim rolled;
  rolled.Advance(params, 1.0);
  // The front one takes its ten twice; the one behind still takes ten.
  ASSERT_EQ(rolled.view().engaged_groups.size(), 1u);
  EXPECT_NEAR(rolled.view().engaged_groups[0].hp_fraction, 0.985, 1e-9);
  EXPECT_EQ(rolled.view().player_hp,
            100);  // already full, so the quarter is capped

  GivePlayerHp(params, 100, /*interval=*/0.5, /*damage=*/50.0);
  CombatSim healed;
  healed.Advance(params, 1.0);
  // Fifty taken, then twenty-five restored by the attack that landed.
  EXPECT_EQ(healed.view().player_hp, 75);
}

// A barrage charges a hit-counting buff as its bolts land, not all at the cast:
// a shock that finds an empty map landed nothing and counts for nothing.
TEST(CombatSimTest, ABarrageChargesAHitBuffOnlyForBoltsThatLand) {
  Mob snail = MakeMob("Snail", 10);
  CombatSim sim;
  // One monster and a respawn every second, so the barrage kills its target and
  // its last three bolts fall on an empty map.
  CombatParams params = MakeParams(1.0, 1.0, {MakeType(&snail, 0.0, 1)});
  BuffOption buff;
  buff.name = "Mortal Blow";
  buff.duration_seconds = 100.0;
  buff.charge_lines = 2;
  params.buffs.push_back(std::move(buff));
  AttackOption orb = MakeSkill("Jupiter Thunder", 100.0, /*cooldown=*/0.0);
  orb.lines = 1;
  orb.max_enemies = 1;
  orb.strikes_in_sequence = 4;
  orb.cast_interval_seconds = 0.25;
  params.attacks.push_back(orb);
  AttackSet set;
  set.attacks = params.attacks;
  set.attacks[1].damage_per_hit[0] = 1000.0;
  params.buffed[1] = std::move(set);

  // The cast lands one line and its three bolts none, so one of the two lines
  // the buff needs is counted.
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.view().damage_this_step, 100.0, 1e-9);
  for (int bolt = 0; bolt < 3; ++bolt) {
    sim.Advance(params, 0.25);
    EXPECT_NEAR(sim.view().damage_this_step, 0.0, 1e-9);
  }

  // The respawn refills the map, but the attack clock was paused while it was
  // empty, so the next cast is a step later.
  sim.Advance(params, 0.25);
  EXPECT_NEAR(sim.view().damage_this_step, 0.0, 1e-9);
  // That cast counts the second line and the buff goes up after it, so it still
  // lands unbuffed. Counting at the cast would have raised the buff three steps
  // ago, and this would already be the buffed thousand.
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.view().damage_this_step, 100.0, 1e-9);
  // And the next one lands under the buff.
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.view().damage_this_step, 1000.0, 1e-9);
}

// A buff earned by landing hits instead of by a cooldown: it goes up on the hit
// that completes the count, and nothing counts while it's active. Its uptime
// depends on how fast the character attacks.
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
  params.buffed[1] = std::move(set);

  // Three attacks to charge it, each landing a plain ten.
  for (int step = 0; step < 3; ++step) {
    sim.Advance(params, 1.0);
  }
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.97, 1e-9);
  // It's up now, and the next two attacks land under it.
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.87, 1e-9);
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.77, 1e-9);
  // Those two landed while it was up, so neither counted toward the next one:
  // three more plain attacks are needed before it returns.
  for (int step = 0; step < 3; ++step) {
    sim.Advance(params, 1.0);
  }
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.74, 1e-9);
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.64, 1e-9);
}

// Freezing Crush: ice leaves a stack per line and lightning spends them for
// more damage. Lightning is stronger alone, so stacks get built only because
// ice is credited with what it leaves.
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

  // Ice first: 10 of its own, plus the two stacks it leaves, each worth half
  // the lightning attack.
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.view().attack_name, "Thunder Bolt");  // aimed next, pile full
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.99, 1e-9);
  // Lightning next, at 11, doubled by the two stacks it spends.
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.968, 1e-9);
  // And back again, because the stacks are gone.
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.958, 1e-9);
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.936, 1e-9);
}

// Priced against every attack known, stacks would look worth building for a
// storm two minutes from ready, and the fight would lay ice it never spends.
TEST(CombatSimTest, FreezeIsNotLaidForASwingStillRecharging) {
  Mob snail = MakeMob("Snail", 1e9);
  CombatParams params = MakeParams(1.0, 1e9, {MakeType(&snail, 20.0, 1)});
  params.attacks[0].name = "Magic Claw";
  params.freeze_cap = 2;
  AttackOption ice = MakeSkill("Cold Beam", 10.0, /*cooldown=*/0.0);
  ice.freeze_build = 2;
  ice.freeze_seconds = 4.0;
  params.attacks.push_back(std::move(ice));
  AttackOption storm = MakeSkill("Jupiter Thunder", 100.0, /*cooldown=*/100.0);
  storm.freeze_spends = true;
  storm.freeze_fd_per_stack = 0.5;
  params.attacks.push_back(std::move(storm));

  // The ice is laid once, for the storm that's ready, and the storm spends the
  // stacks. Its cooldown then leaves nothing available that can spend a stack,
  // so the plain attack takes every press after that.
  CombatSim spent;
  for (int step = 0; step < 6; ++step) {
    spent.Advance(params, 1.0);
  }
  EXPECT_NEAR(spent.by_attack()[1].damage, 10.0, 1e-9);
  EXPECT_NEAR(spent.by_attack()[2].damage, 200.0, 1e-9);
  EXPECT_NEAR(spent.by_attack()[0].damage, 80.0, 1e-9);

  // The same storm without a cooldown spends every stack laid, so ice stays
  // worth using and the plain attack never wins.
  CombatParams ready = params;
  ready.attacks[2].cooldown_seconds = 0.0;
  CombatSim standing;
  for (int step = 0; step < 6; ++step) {
    standing.Advance(ready, 1.0);
  }
  EXPECT_GT(standing.by_attack()[1].damage, 10.0);
  EXPECT_NEAR(standing.by_attack()[0].damage, 0.0, 1e-9);
}

// Spirit of Snow's shape: a blizzard worth three stacks on a lone enemy and one
// on each in a crowd. The stacks depend on what the strike actually hit, so the
// same attack gives different results on the two maps.
TEST(CombatSimTest, AStrikeAloneLeavesItsOwnCountOfFreezeStacks) {
  Mob snail = MakeMob("Snail", 1e9);
  CombatSim alone;
  CombatParams one = MakeParams(1.0, 1e9, {MakeType(&snail, 1.0, 1)},
                                /*reach=*/10);
  one.freeze_cap = 9;
  AttackOption blizzard = MakeSkill("Spirit of Snow", 100.0, /*cooldown=*/0.0);
  blizzard.lines = 12;
  blizzard.max_enemies = 10;
  blizzard.freeze_build = 1;
  blizzard.freeze_build_alone = 3;
  one.attacks.push_back(blizzard);
  alone.Advance(one, 1.0);
  EXPECT_EQ(alone.freeze_stacks(), 3);

  // The same blizzard over a crowd gives the one GMS states.
  CombatSim crowd;
  CombatParams many = MakeParams(1.0, 1e9, {MakeType(&snail, 1.0, 8)},
                                 /*reach=*/10);
  many.freeze_cap = 9;
  many.attacks.push_back(blizzard);
  crowd.Advance(many, 1.0);
  EXPECT_EQ(crowd.freeze_stacks(), 1);
}

// Jupiter Thunder's rate: a shock spends one stack every five lines, so five
// stacks outlast the first shock instead of being spent by it.
TEST(CombatSimTest, ASwingCanSpendTheFreezePileByTheLine) {
  Mob snail = MakeMob("Snail", 1e9);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 1e9, {MakeType(&snail, 1.0, 1)});
  params.freeze_cap = 5;
  AttackOption shock = MakeSkill("Jupiter Thunder", 100.0, /*cooldown=*/0.0);
  shock.lines = 8;
  shock.freeze_spends = true;
  shock.freeze_lines_per_spend = 5;
  params.attacks.push_back(shock);

  // Set by hand: the stacks' value isn't the point here, only how many a shock
  // removes.
  CombatParams built = params;
  built.attacks[1].freeze_spends = false;
  built.attacks[1].freeze_build = 5;
  sim.Advance(built, 1.0);
  ASSERT_EQ(sim.freeze_stacks(), 5);

  // Eight lines at one stack per five is one stack, not eight.
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.freeze_stacks(), 4);
  sim.Advance(params, 1.0);
  EXPECT_EQ(sim.freeze_stacks(), 3);
}

// Jupiter Thunder's refund: a barrage that outlasts the crowd refunds cooldown
// for every shock it didn't land, and refunds nothing on a boss.
TEST(CombatSimTest, UnspentStrikesHandBackTheirOwnWait) {
  Mob snail = MakeMob("Snail", 10);
  CombatSim sim;
  // Two mobs and ten shocks, so eight find an empty map.
  CombatParams params = MakeParams(1.0, 1e9, {MakeType(&snail, 1.0, 2)});
  AttackOption orb = MakeSkill("Jupiter Thunder", 200.0, /*cooldown=*/100.0);
  orb.max_enemies = 1;
  orb.strikes_in_sequence = 10;
  orb.cast_interval_seconds = 0.25;
  orb.cooldown_refund_seconds = 3.4;
  params.attacks.push_back(orb);

  // Measured against the same barrage without a refund, not against a cooldown
  // that has also been ticking down.
  CombatSim plain;
  CombatParams unpaid = params;
  unpaid.attacks[1].cooldown_refund_seconds = 0.0;
  sim.Advance(params, 1.0);
  plain.Advance(unpaid, 1.0);
  for (int step = 0; step < 10; ++step) {
    sim.Advance(params, 0.25);
    plain.Advance(unpaid, 0.25);
  }
  EXPECT_EQ(sim.view().kills_this_step[0], 0);
  // Two mobs took the first shocks and eight found an empty map.
  EXPECT_NEAR(plain.cooldown_left(1) - sim.cooldown_left(1), 3.4 * 8, 1e-9);

  // A crowd big enough to take every shock gets no refund.
  CombatSim full;
  CombatSim full_unpaid;
  CombatParams many = MakeParams(1.0, 1e9, {MakeType(&snail, 1.0, 20)});
  many.attacks.push_back(orb);
  CombatParams many_unpaid = many;
  many_unpaid.attacks[1].cooldown_refund_seconds = 0.0;
  full.Advance(many, 1.0);
  full_unpaid.Advance(many_unpaid, 1.0);
  for (int step = 0; step < 10; ++step) {
    full.Advance(many, 0.25);
    full_unpaid.Advance(many_unpaid, 0.25);
  }
  EXPECT_NEAR(full.cooldown_left(1), full_unpaid.cooldown_left(1), 1e-9);
}

// Two barrages at once, as with an I/L Arch Mage who has both Jupiter Thunder
// and Bolt Barrage. Each keeps its own timing: casting the second doesn't
// cancel what the first has left to land.
TEST(CombatSimTest, ASecondBarrageDoesNotCutTheFirstShort) {
  Mob boss = MakeMob("Zakum", 1000000);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 1e9, {MakeType(&boss, 1.0, 1)});
  AttackOption thunder =
      MakeSkill("Jupiter Thunder", 100.0, /*cooldown=*/100.0);
  thunder.strikes_in_sequence = 4;
  thunder.cast_interval_seconds = 0.5;
  params.attacks.push_back(thunder);
  AttackOption bolts = MakeSkill("Bolt Barrage", 50.0, /*cooldown=*/100.0);
  bolts.strikes_in_sequence = 2;
  bolts.cast_interval_seconds = 0.5;
  params.attacks.push_back(bolts);

  // The thunder is cast first as the stronger of the two, and its shocks fall
  // at 1.5, 2.0 and 2.5 seconds. The bolts are cast at 2.0, in the middle of
  // them, and their second lands at 2.5 alongside the thunder's last.
  for (int step = 0; step < 6; ++step) {
    sim.Advance(params, 0.5);
  }
  EXPECT_NEAR(sim.by_attack()[1].damage, 400.0, 1e-9);
  EXPECT_NEAR(sim.by_attack()[2].damage, 100.0, 1e-9);
}

// The current reaches two enemies while the orb hits one, so the wide part
// lands on an enemy the attack itself never touched.
TEST(CombatSimTest, AWideHitReachesPastTheSwingCarryingIt) {
  Mob snail = MakeMob("Snail", 100);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 1e9, {MakeType(&snail, 0.0, 4)});
  AttackOption orb = MakeSkill("Jupiter Thunder", 0.0, /*cooldown=*/0.0);
  orb.max_enemies = 1;
  orb.damage_per_hit = {100.0};
  orb.wide_hit_damage = {100.0};
  orb.wide_hit_enemies = 2;
  params.attacks.push_back(orb);

  sim.Advance(params, 1.0);
  // Two dead: the one the orb hit, taking both parts, and the one only the
  // current reached.
  EXPECT_EQ(sim.view().kills_this_step[0], 2);
}

// Jupiter Thunder's shock: the enemy with it takes more from every other
// lightning attack, but not from Jupiter Thunder itself.
TEST(CombatSimTest, AStunLiftsTheSwingsThatCollectItAndNotItsOwn) {
  Mob snail = MakeMob("Snail", 1e9);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 1e9, {MakeType(&snail, 0.0, 1)});
  AttackOption bolt = MakeSkill("Chain Lightning", 100.0, /*cooldown=*/0.0);
  bolt.collects_stun_lift = true;
  params.attacks.push_back(bolt);

  // Nothing has stunned it yet, so the bolt lands for its stated damage.
  sim.Advance(params, 1.0);
  double plain = sim.view().damage_this_step;
  EXPECT_NEAR(plain, 100.0, 1e-9);

  // The orb alone first, to leave the stun, then the bolt: with a 12% mark, the
  // same bolt lands for 112.
  CombatSim stunned;
  CombatParams shocking = MakeParams(1.0, 1e9, {MakeType(&snail, 0.0, 1)});
  AttackOption orb = MakeSkill("Jupiter Thunder", 1.0, /*cooldown=*/0.0);
  orb.stun_seconds = 4.0;
  orb.stun_lift_pct = 0.12;
  shocking.attacks.push_back(orb);
  stunned.Advance(shocking, 1.0);
  stunned.Advance(params, 1.0);
  EXPECT_NEAR(stunned.view().damage_this_step, 112.0, 1e-9);

  // The skill that left the stun never benefits from it: GMS excludes the shock
  // from the attacks its own stun boosts.
  CombatSim itself;
  itself.Advance(shocking, 1.0);
  itself.Advance(shocking, 1.0);
  EXPECT_NEAR(itself.view().damage_this_step, 1.0, 1e-9);
}

// Scarring Sword's shape: each of two lines scars at even odds, and a line on a
// scarred monster does 50% more. The first attack benefits only on its second
// line; the second finds the scar already there three times in four.
TEST(CombatSimTest, AScarPaysTheLinesLandingAfterIt) {
  Mob boss = MakeMob("Zakum", 1000000);
  CombatParams params = MakeParams(1.0, 0.0, {MakeType(&boss, 0.0, 1)});
  AttackOption sword = MakeSkill("Scarring Sword", 100.0, /*cooldown=*/0.0);
  sword.lines = 2;
  sword.scar_chance = 0.5;
  sword.scar_seconds = 10.0;
  sword.scar_fd = 0.5;
  params.attacks.push_back(sword);
  CombatSim sim;
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.view().damage_this_step, 112.5, 1e-9);
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.view().damage_this_step, 140.625, 1e-9);
}

// A stun afflicts monsters but never bosses, though a boss still gets the
// damage bonus. A stun that fails its roll gives neither.
TEST(CombatSimTest, AStunAfflictsNoBossAndOnlyWhereItTakes) {
  auto bolt_after_orb = [](bool boss, double chance) {
    Mob mob = MakeMob("Target", 1e9);
    mob.set_boss(boss);
    CombatParams shocking = MakeParams(1.0, 1e9, {MakeType(&mob, 0.0, 1)});
    AttackOption orb = MakeSkill("Jupiter Thunder", 1.0, /*cooldown=*/0.0);
    orb.stun_seconds = 4.0;
    orb.stun_lift_pct = 0.12;
    orb.stun_chance = chance;
    shocking.attacks.push_back(orb);
    CombatParams params = MakeParams(1.0, 1e9, {MakeType(&mob, 0.0, 1)});
    AttackOption bolt = MakeSkill("Chain Lightning", 100.0, /*cooldown=*/0.0);
    bolt.collects_stun_lift = true;
    bolt.fd_when_afflicted = 0.5;
    params.attacks.push_back(bolt);
    CombatSim sim;
    sim.Advance(shocking, 1.0);
    sim.Advance(params, 1.0);
    return sim.view().damage_this_step;
  };
  EXPECT_NEAR(bolt_after_orb(/*boss=*/false, 1.0), 168.0, 1e-9);
  EXPECT_NEAR(bolt_after_orb(/*boss=*/true, 1.0), 112.0, 1e-9);
  EXPECT_NEAR(bolt_after_orb(/*boss=*/false, 0.0), 100.0, 1e-9);
}

TEST(CombatSimTest, WithNoPileToBuildTheHarderSwingSimplyWins) {
  Mob snail = MakeMob("Snail", 1000);
  CombatSim sim;
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 0.0, 1)});
  GiveFreezeStacks(params, /*cap=*/0);

  for (int step = 0; step < 4; ++step) {
    sim.Advance(params, 1.0);
  }
  EXPECT_EQ(sim.view().attack_name, "Thunder Bolt");
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.956, 1e-9);  // 11 four times
}

// The critical damage from a held stack applies to every attack, ice and
// lightning alike: a frozen enemy is frozen whatever element hits it.
TEST(CombatSimTest, AHeldStackLiftsTheIceSwingItCameFrom) {
  Mob snail = MakeMob("Snail", 1000);
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 0.0, 1)});
  GiveFreezeStacks(params, /*cap=*/4);
  // No lightning attack at all, so nothing spends what the ice leaves.
  params.attacks.pop_back();
  params.attacks[1].freeze_crit_gain = 0.25;

  CombatSim sim;
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.99,
              1e-9);  // 10, nothing held yet
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.975,
              1e-9);  // 10 x 1.5, two held
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.955,
              1e-9);  // 10 x 2.0, four held
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.935,
              1e-9);  // capped, so no more
}

// Lightning Orb's shape: a held attack pulsing 10 damage every 0.15s for up to
// 12 pulses, ending on a 50-damage burst that takes 0.2s. The minimum is 0.96s,
// which fits five pulses and the finish.
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

// Steps the fight in slices and totals the character's damage. Advance limits
// one call to a single basic attack, so a longer hold must be stepped through.
double RunFor(CombatSim& sim, const CombatParams& params, double seconds) {
  double damage = 0.0;
  for (double step = 0.0; step + 1e-9 < seconds; step += 0.01) {
    sim.Advance(params, 0.01);
    damage += sim.view().damage_this_step;
  }
  return damage;
}

// A boss is never close to dying from the finish, so the orb is held to the
// end: twelve pulses and the burst, over the full two seconds.
TEST(CombatSimTest, AHoldRunsToTheEndAgainstSomethingThatSurvivesIt) {
  Mob boss = MakeMob("Zakum", 1000000);
  CombatParams params = MakeParams(1.0, 0.0, {MakeType(&boss, 0.0, 1)});
  params.attacks.push_back(MakeHeldSwing());

  CombatSim sim;
  EXPECT_DOUBLE_EQ(RunFor(sim, params, 1.95), 0.0);  // still being held
  EXPECT_EQ(sim.view().attack_name, "Lightning Orb");
  EXPECT_DOUBLE_EQ(RunFor(sim, params, 0.1), 170.0);  // 12 x 10, then 50
}

// Against something the burst alone nearly kills, the hold is released at its
// minimum: five pulses and the finish, in 0.96s instead of 2s.
TEST(CombatSimTest, AHoldIsLetGoOnceMorePulsesWouldBuyNothing) {
  Mob snail = MakeMob("Snail", 60);
  CombatParams params = MakeParams(1.0, 0.0, {MakeType(&snail, 0.0, 1)});
  params.attacks.push_back(MakeHeldSwing());

  CombatSim sim;
  EXPECT_DOUBLE_EQ(RunFor(sim, params, 0.95), 0.0);
  EXPECT_DOUBLE_EQ(RunFor(sim, params, 0.02), 100.0);  // 5 x 10, then 50
  EXPECT_TRUE(sim.view().roster.empty());
}

// A hold that grows: two pulses at 10, then 30 each, with no final strike. A
// boss takes all six; a monster with 50 HP is released at the third, the first
// grown pulse finishing it off.
AttackOption MakeGrowingHold() {
  AttackOption hold = MakeSkill("Hurricane", 0.0, /*cooldown=*/0.0);
  hold.channel.pulses = 6;
  hold.channel.min_pulses = 1;
  hold.channel.pulse_seconds = 0.1;
  hold.channel.min_seconds = 0.1;
  hold.channel.small_pulses = 2;
  hold.channel.grown = {{30.0}, SwingRolls{}};
  hold.groups.push_back({{10.0}, SwingRolls{}});
  hold.damage_per_hit = {2 * 10.0 + 4 * 30.0};
  hold.swing_seconds = HoldSeconds(hold.channel, hold.channel.pulses);
  return hold;
}

TEST(CombatSimTest, AGrowingHoldBeatsAtBothStrengths) {
  Mob boss = MakeMob("Zakum", 1000000);
  CombatParams params = MakeParams(1.0, 0.0, {MakeType(&boss, 0.0, 1)});
  params.attacks.push_back(MakeGrowingHold());
  CombatSim sim;
  EXPECT_DOUBLE_EQ(RunFor(sim, params, 0.55), 0.0);
  EXPECT_DOUBLE_EQ(RunFor(sim, params, 0.1), 140.0);

  Mob snail = MakeMob("Snail", 50);
  CombatParams small = MakeParams(1.0, 0.0, {MakeType(&snail, 0.0, 1)});
  small.attacks.push_back(MakeGrowingHold());
  CombatSim quick;
  EXPECT_DOUBLE_EQ(RunFor(quick, small, 0.25), 0.0);
  EXPECT_DOUBLE_EQ(RunFor(quick, small, 0.1), 50.0);
  EXPECT_TRUE(quick.view().roster.empty());
}

// Sonic Blow's shape: a hold with no final strike. It's still released early,
// since pulses after the crowd is dead hit nothing and still cost time.
TEST(CombatSimTest, AHoldThatEndsOnNoStrikeIsStillLetGoEarly) {
  Mob snail = MakeMob("Snail", 30);
  CombatParams params = MakeParams(1.0, 0.0, {MakeType(&snail, 0.0, 1)});
  AttackOption blow = MakeHeldSwing();
  blow.groups.pop_back();
  blow.damage_per_hit = {12 * 10.0};
  params.attacks.push_back(std::move(blow));

  CombatSim sim;
  EXPECT_DOUBLE_EQ(RunFor(sim, params, 0.95), 0.0);
  EXPECT_DOUBLE_EQ(RunFor(sim, params, 0.02), 50.0);  // 5 x 10, at the floor
  EXPECT_TRUE(sim.view().roster.empty());
}

// Holding protects the player: damage taken while the key is down is halved,
// and back to full as soon as the attack lands.
TEST(CombatSimTest, AHoldShelttersThePlayerWhileItRuns) {
  Mob biter = MakeMob("Biter", 1000000);
  CombatParams params = MakeParams(1.0, 0.0, {MakeType(&biter, 0.0, 1)});
  GivePlayerHp(params, 1000, /*interval=*/0.5, /*damage=*/100.0);
  AttackOption orb = MakeHeldSwing();
  orb.channel.damage_taken_pct = 0.5;
  params.attacks.push_back(std::move(orb));

  CombatSim sim;
  // Three hits land during the two-second hold, each halved.
  RunFor(sim, params, 1.6);
  EXPECT_EQ(sim.view().player_hp, 1000 - 150);
}

// A healing hold heals for the pulses it landed, not the pulses it could have:
// the same attack released at its minimum restores less than one held to the
// end.
TEST(CombatSimTest, AHoldRecoversForThePulsesItActuallyLanded) {
  auto play = [](int mob_hp) {
    Mob mob = MakeMob("Mob", mob_hp);
    CombatParams params = MakeParams(1.0, 0.0, {MakeType(&mob, 0.0, 1)});
    GivePlayerHp(params, 1000, /*interval=*/0.5, /*damage=*/200.0);
    AttackOption orb = MakeHeldSwing();
    orb.channel.hp_recover_pct = 0.01;
    orb.hp_recover_pct = 0.05;
    params.attacks.push_back(std::move(orb));
    CombatSim sim;
    RunFor(sim, params, 2.05);
    return sim.view().player_hp;
  };
  // Held to the end: four hits taken over two seconds, then twelve pulses and
  // the finish restore 17% of HP.
  EXPECT_EQ(play(1000000), 1000 - 800 + 170);
  // Released at its 0.96s minimum: one hit taken before it lands and five
  // pulses healed, then the fight idles on a cleared map.
  EXPECT_EQ(play(60), 1000 - 200 + 100);
}

// Divine Punishment's shape: a hold paid for from a charge bank instead of a
// cooldown. One charge every 2s, one stored at a time, and each buys four of
// the twelve pulses, so a press lasts only as long as the bank pays for.
AttackOption MakeBankedHold() {
  AttackOption punish = MakeSkill("Divine Punishment", 0.0, /*cooldown=*/0.0);
  punish.channel.pulses = 12;
  punish.channel.min_pulses = 2;
  punish.channel.pulse_seconds = 0.15;
  punish.channel.min_seconds = 0.3;
  punish.channel.charge_seconds = 2.0;
  punish.channel.max_charges = 1;
  punish.channel.pulses_per_charge = 4;
  punish.groups.push_back({{10.0}, SwingRolls{}});
  punish.damage_per_hit = {12 * 10.0};
  punish.swing_seconds = HoldSeconds(punish.channel, punish.channel.pulses);
  return punish;
}

// The boss survives anything the hold does, so only the bank can shorten it:
// one charge buys four pulses, and the hold is released at 0.6s after a third
// of a full hold's damage.
TEST(CombatSimTest, ABankedHoldRunsOnlyAsLongAsItsChargesPayFor) {
  Mob boss = MakeMob("Zakum", 1000000);
  CombatParams params = MakeParams(1.0, 0.0, {MakeType(&boss, 0.0, 1)});
  params.attacks.push_back(MakeBankedHold());

  CombatSim sim;
  EXPECT_DOUBLE_EQ(RunFor(sim, params, 0.55), 0.0);
  EXPECT_DOUBLE_EQ(RunFor(sim, params, 0.1), 40.0);
}

// The bank refills on its own clock, so the hold comes back as charges arrive.
// A press uses one whole charge and keeps progress toward the next, putting the
// second hold at 2s, not 2.6s.
TEST(CombatSimTest, ABankRefillsOnItsOwnClockAndBringsTheHoldBack) {
  Mob boss = MakeMob("Zakum", 1000000);
  CombatParams params = MakeParams(1.0, 0.0, {MakeType(&boss, 0.0, 1)});
  params.attacks.push_back(MakeBankedHold());

  CombatSim sim;
  EXPECT_DOUBLE_EQ(RunFor(sim, params, 1.95), 40.0);  // the first hold alone
  EXPECT_DOUBLE_EQ(RunFor(sim, params, 0.7), 40.0);   // the bank paid again
}

// Glacial Fury's part of the stacks: magic attack per held stack, and only an
// ice attack gets it.
TEST(CombatSimTest, GlacialFurysMagicAttackRidesTheIceSwingAlone) {
  Mob snail = MakeMob("Snail", 1000);
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 0.0, 1)});
  GiveFreezeStacks(params, /*cap=*/4);
  // No lightning attack, so the stacks only grow, and the test reads what the
  // ice attack gains from holding them.
  params.attacks.pop_back();
  params.attacks[1].freeze_matt_gain = 0.25;

  CombatSim sim;
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.99,
              1e-9);  // 10, nothing held yet
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.975,
              1e-9);  // 10 x 1.5, two held
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.955,
              1e-9);  // 10 x 2.0, four held
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.935,
              1e-9);  // capped, so no more
}

// Storm Magic's part: final damage while any stacks are held, the same however
// many there are.
TEST(CombatSimTest, StormMagicStandsOnAnyStackHoweverDeep) {
  Mob snail = MakeMob("Snail", 1000);
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 0.0, 1)});
  GiveFreezeStacks(params, /*cap=*/4);
  // No lightning attack, so nothing spends what the ice leaves.
  params.attacks.pop_back();
  params.attacks[1].fd_when_afflicted = 0.5;

  CombatSim sim;
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.99,
              1e-9);  // 10, nothing held yet
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.975,
              1e-9);  // 10 x 1.5, two held
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.96,
              1e-9);  // the same half at four
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.945,
              1e-9);  // and no more at the cap
}

// The fraction of HP left on the named mob. The queue is shuffled as it fills,
// so the roster's order doesn't say which monster is which.
double LeftOn(const CombatSim& sim, const std::string& name) {
  for (const MobStatus& mob : sim.view().roster) {
    if (mob.name == name) {
      return mob.hp_fraction;
    }
  }
  return -1.0;
}

// Shatter's part: the defence ignored per held stack, priced per mob type.
// Against a monster with no defence it gives nothing.
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

// The freeze enables the bonus and the stacks set its size: a character with
// full stacks gets nothing against a monster no attack has frozen.
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
  // Four attacks of a flat 10, however many stacks were held.
  EXPECT_NEAR(sim.view().target_hp_fraction, 0.96, 1e-9);
}

// The freeze outlasts the attack that applied it, which is what makes
// alternating work. Storm Magic applies to the basic attack because it belongs
// to the character.
TEST(CombatSimTest, TheIceOutlastsTheSwingThatLaidIt) {
  Mob snail = MakeMob("Snail", 1000000);
  CombatParams params = MakeParams(1.0, 0.0, {MakeType(&snail, 10.0, 1)});
  GiveFreezeStacks(params, /*cap=*/4);
  params.attacks.pop_back();  // no lightning swing to spend the pile
  params.attacks[1].freeze_seconds = 2.5;
  params.attacks[1].cooldown_seconds = 100.0;  // cast once only
  params.attacks[0].fd_when_afflicted = 1.0;
  params.attacks[1].fd_when_afflicted = 1.0;

  CombatSim sim;
  sim.Advance(params, 1.0);
  EXPECT_DOUBLE_EQ(sim.view().damage_this_step,
                   10.0);  // ice, on a thawed monster
  sim.Advance(params, 1.0);
  EXPECT_DOUBLE_EQ(sim.view().damage_this_step,
                   20.0);  // the basic attack, on 1.5s of ice
  sim.Advance(params, 1.0);
  EXPECT_DOUBLE_EQ(sim.view().damage_this_step, 20.0);  // and on the last 0.5s
  sim.Advance(params, 1.0);
  EXPECT_DOUBLE_EQ(sim.view().damage_this_step,
                   10.0);  // thawed, so a flat basic attack
}

// The stack cap is higher while the buff raising it is up, and the fight reads
// the cap for the active buffs instead of one number for the whole encounter.
TEST(CombatSimTest, ABuffDeepensThePile) {
  Mob snail = MakeMob("Snail", 1000);
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&snail, 0.0, 1)});
  GiveFreezeStacks(params, /*cap=*/2);
  EXPECT_EQ(params.FreezeCap(0), 2);

  AttackSet deeper;
  deeper.attacks = params.attacks;
  deeper.freeze_cap = 10;
  params.buffed[1] = std::move(deeper);
  EXPECT_EQ(params.FreezeCap(1), 10);
  // An out-of-range index means no buffs, as the attack tables read it.
  EXPECT_EQ(params.FreezeCap(2), 2);
}

// A boss fight keeps the roster it started with: nothing refills, and an empty
// queue stays empty however long the fight lasts.
TEST(CombatSimTest, NoRespawnSecondsMeansNothingComesBack) {
  Mob mob = MakeMob("Arm", 10);
  CombatParams params = MakeParams(1.0, 0.0, {MakeType(&mob, 100.0, 2)});
  CombatSim sim;
  for (int i = 0; i < 100; ++i) {
    sim.Advance(params, 1.0);
  }
  EXPECT_TRUE(sim.respawning());
  EXPECT_TRUE(sim.view().roster.empty());
}

// The roster has one entry per mob instead of the merged bars, and each entry
// keeps its id as those beside it die. That's what keeps each of Zakum's arms
// in its own panel.
TEST(CombatSimTest, TheRosterHoldsEveryMobAndKeepsItsIds) {
  Mob mob = MakeMob("Arm", 100, 110);
  CombatParams params = MakeParams(1.0, 0.0, {MakeType(&mob, 60.0, 3)});
  CombatSim sim;
  sim.Advance(params, 0.0);
  ASSERT_EQ(sim.view().roster.size(), 3u);
  EXPECT_EQ(sim.view().roster[0].name, "Arm");
  EXPECT_DOUBLE_EQ(sim.view().roster[1].hp_fraction, 1.0);
  std::vector<int> ids;
  for (const MobStatus& status : sim.view().roster) {
    ids.push_back(status.id);
  }
  EXPECT_EQ(std::set<int>(ids.begin(), ids.end()).size(), 3u);

  // Two attacks kill the front mob and a third damages the next; the survivors
  // keep their ids.
  for (int i = 0; i < 3; ++i) {
    sim.Advance(params, 1.0);
  }
  ASSERT_EQ(sim.view().roster.size(), 2u);
  EXPECT_EQ(sim.view().roster[0].id, ids[1]);
  EXPECT_EQ(sim.view().roster[1].id, ids[2]);
  EXPECT_LT(sim.view().roster[0].hp_fraction, 1.0);
  EXPECT_DOUBLE_EQ(sim.view().roster[1].hp_fraction, 1.0);
}

// The fight watches the encounter name to know it has moved, so a boss phase
// change rebuilds the roster just like a map change does.
TEST(CombatSimTest, ANewEncounterNameRefillsTheQueue) {
  Mob arm = MakeMob("Arm", 100);
  Mob body = MakeMob("Body", 500);
  CombatParams first = MakeParams(1.0, 0.0, {MakeType(&arm, 1000.0, 1)});
  CombatSim sim;
  sim.Advance(first, 1.0);
  EXPECT_TRUE(sim.view().roster.empty());

  CombatParams second =
      MakeParams(1.0, 0.0, {MakeType(&body, 10.0, 1)}, 1, "phase2");
  sim.Advance(second, 0.0);
  ASSERT_EQ(sim.view().roster.size(), 1u);
  EXPECT_EQ(sim.view().roster[0].name, "Body");
}

// Recording is off unless requested: the sims step the fight millions of times
// and draw none of it.
TEST(CombatSimTest, NothingIsRecordedUnlessItIsAskedFor) {
  Mob mob = MakeMob("Snail", 1000000);
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&mob, 25.0, 1)});
  CombatSim sim;
  sim.Advance(params, 1.0);
  EXPECT_TRUE(sim.damage_lines_this_step().empty());
}

// The full recording contract: one line per hit, all lines from one attack on
// one monster under one event, recorded against the monster that took them, and
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
  double before = sim.view().target_hp_fraction;
  sim.Advance(params, 1.0);

  const std::vector<DamageLine>& lines = sim.damage_lines_this_step();
  ASSERT_EQ(lines.size(), 4u);
  ASSERT_EQ(sim.view().roster.size(), 1u);
  double total = 0.0;
  for (const DamageLine& line : lines) {
    EXPECT_EQ(line.mob_id, sim.view().roster[0].id);
    EXPECT_EQ(line.event, lines[0].event);
    EXPECT_GT(line.damage, 0.0);
    total += line.damage;
  }
  EXPECT_NEAR(total, (before - sim.view().target_hp_fraction) * mob.max_hp(),
              1e-6);
}

// Two monsters, one attack: each gets its own stack, so the numbers can be
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

// A crit is marked separately from a plain line; the number's colour depends
// only on this.
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
      // Nothing varies, so a plain line is the attack's 25 over its eight
      // lines, less the 50% the crit rate already averaged in, and a crit is
      // exactly twice that.
      double plain = 25.0 / (8 * 1.5);
      EXPECT_NEAR(line.damage, line.crit ? 2 * plain : plain, 1e-9);
    }
  }
  EXPECT_TRUE(crit_seen);
  EXPECT_TRUE(plain_seen);
}

// Every line says what dealt it, so a caller drawing them can tell an attack
// from what fires beside it.
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

// Every line names the skill it's credited to. A Final Attack and a burn get
// their own names, on the cast of the attack that triggered them. A burn tick
// counts as its own cast.
TEST(CombatSimTest, EveryLineNamesItsSkill) {
  Mob mob = MakeMob("Snail", 1000000);
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&mob, 25.0, 1)});
  params.record_damage_lines = true;
  params.dot_count = 1;
  AttackOption& swing = params.attacks[0];
  swing.credit = "Brandish";
  FinalAttackRoll follow;
  follow.chance = 1.0;
  follow.damage = {5.0};
  follow.credit = "Advanced Final Attack";
  swing.final_attack_rolls.push_back(follow);
  swing.final_attack_damage = {5.0};
  DotApplication burn;
  burn.damage = {40.0};
  burn.interval_seconds = 0.6;
  burn.duration_seconds = 10.0;
  burn.slot = 0;
  burn.credit = "Venom";
  swing.dots.push_back(burn);

  CombatSim sim;
  std::map<std::string, std::set<int>> casts;
  for (int step = 0; step < 3; ++step) {
    sim.Advance(params, 1.0);
    for (const DamageLine& line : sim.damage_lines_this_step()) {
      casts[sim.damage_credit_name(line.credit)].insert(line.cast);
    }
  }
  ASSERT_EQ(casts.size(), 3u);
  EXPECT_EQ(casts["Brandish"].size(), 3u);
  EXPECT_EQ(casts["Advanced Final Attack"], casts["Brandish"]);
  EXPECT_FALSE(casts["Venom"].empty());
  for (int cast : casts["Venom"]) {
    EXPECT_EQ(casts["Brandish"].count(cast), 0u);
  }
}

// Nothing is named unless lines are being recorded, so a sim pays nothing for
// the breakdown.
TEST(CombatSimTest, ACreditIsOnlyNumberedWhileRecording) {
  DamageLedger ledger;
  ledger.BeginStep(false);
  EXPECT_EQ(ledger.Credit("Brandish"), -1);
  ledger.BeginStep(true);
  int brandish = ledger.Credit("Brandish");
  EXPECT_EQ(ledger.Credit("Rush"), brandish + 1);
  EXPECT_EQ(ledger.Credit("Brandish"), brandish);
  EXPECT_EQ(ledger.credit_name(brandish), "Brandish");
  EXPECT_EQ(ledger.credit_name(-1), "");
}

// A hold counts one cast per pulse, numbered after the landing's own.
TEST(CombatSimTest, AHoldTakesACastPerPulse) {
  DamageLedger ledger;
  ledger.BeginStep(true);
  ledger.OpenLandings(1, 1, {}, "Hurricane", 5);
  Landing held = ledger.LandingAt(7, 0, 1.0);
  ledger.OpenLandings(1, 1, {}, "Hurricane");
  EXPECT_EQ(ledger.LandingAt(7, 0, 1.0).cast, held.cast + 5);
}

// A burn ticks between attacks, not with one, so it's a separate event with its
// own stack of numbers.
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

// Damage is counted whether or not lines are recorded, and it counts what the
// attack rolled, not what the mob had left.
TEST(CombatSimTest, DamageThisStepCountsOverkill) {
  Mob mob = MakeMob("Snail", 10);
  CombatParams params = MakeParams(1.0, 1000.0, {MakeType(&mob, 250.0, 1)});

  CombatSim sim;
  sim.Advance(params, 1.0);
  EXPECT_DOUBLE_EQ(sim.view().damage_this_step, 250.0);
  EXPECT_EQ(sim.view().kills_this_step[0], 1);

  // A step that hits nothing does no damage, and the count doesn't carry over
  // from the previous step.
  sim.Advance(params, 0.1);
  EXPECT_DOUBLE_EQ(sim.view().damage_this_step, 0.0);
}

// A burn ticking between attacks counts as damage the character dealt.
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
  EXPECT_DOUBLE_EQ(sim.view().damage_this_step, 80.0);
}

// The respawn is flagged on the step it happens, and no other.
TEST(CombatSimTest, TheRespawnBeatIsFlaggedOnItsStep) {
  Mob mob = MakeMob("Snail", 100);
  CombatParams params = MakeParams(1.0, 2.0, {MakeType(&mob, 10.0, 1)});

  CombatSim sim;
  sim.Advance(params, 0.5);
  EXPECT_FALSE(sim.view().respawned_this_step);
  sim.Advance(params, 1.0);
  EXPECT_FALSE(sim.view().respawned_this_step);
  sim.Advance(params, 1.0);
  EXPECT_TRUE(sim.view().respawned_this_step);
  sim.Advance(params, 0.5);
  EXPECT_FALSE(sim.view().respawned_this_step);
}

// A totem halves the respawn interval mid-wait, but doesn't shorten the wait
// already in progress. Removing it follows the same rule in reverse.
TEST(CombatSimTest, ChangingTheBeatWaitsOutTheCycleItFound) {
  Mob mob = MakeMob("Snail", 1'000'000);  // never falls: only the beat moves
  CombatParams slow = MakeParams(1.0, 8.0, {MakeType(&mob, 1.0, 1)});
  CombatParams fast = slow;
  fast.respawn_seconds = 4.0;

  CombatSim sim;
  // One second per step, since Advance limits a step to one attack.
  auto seconds = [&sim](const CombatParams& params, int n) {
    bool beat = false;
    for (int i = 0; i < n; ++i) {
      sim.Advance(params, 1.0);
      beat = beat || sim.view().respawned_this_step;
    }
    return beat;
  };

  EXPECT_FALSE(seconds(slow, 4));
  EXPECT_FALSE(seconds(fast, 1)) << "the totem went up five seconds into eight";
  EXPECT_NEAR(sim.view().respawn_fraction, 5.0 / 8.0, 1e-9);
  EXPECT_FALSE(seconds(fast, 2));
  EXPECT_TRUE(seconds(fast, 1)) << "eight, the wait this cycle began under";

  EXPECT_FALSE(seconds(fast, 3));
  EXPECT_TRUE(seconds(fast, 1)) << "and every cycle after it runs at four";

  EXPECT_FALSE(seconds(slow, 3));
  EXPECT_TRUE(seconds(slow, 1)) << "put away mid-cycle: this one stays short";
}

// A measured fight keeps its roster: the monsters take damage but none die, so
// the result is the damage rate, not how fast the map emptied.
TEST(CombatSimTest, AMeasuredRosterNeverFalls) {
  Mob mob = MakeMob("Snail", 10);
  CombatParams params = MakeParams(1.0, 1e9, {MakeType(&mob, 1000.0, 4)}, 4);
  params.measuring = true;

  CombatSim sim;
  double damage = 0.0;
  for (int step = 0; step < 100; ++step) {
    sim.Advance(params, 1.0);
    damage += sim.view().damage_this_step;
    EXPECT_EQ(sim.view().kills_this_step[0], 0);
  }
  EXPECT_NEAR(damage, 100 * 4 * 1000.0, 1e-6);
}

// Every roll lands its mean while measuring, so two runs of one build agree
// exactly, and any difference between two builds comes from the build.
TEST(CombatSimTest, AMeasuredSwingLandsItsMean) {
  Mob mob = MakeMob("Snail", 1000000);
  double dealt[3] = {0.0, 0.0, 0.0};
  for (int run = 0; run < 3; ++run) {
    CombatParams params = MakeParams(1.0, 1e9, {MakeType(&mob, 25.0, 1)});
    params.measuring = true;
    HitGroup group;
    group.damage = {25.0};
    group.rolls.lines = 4;
    group.rolls.mastery = run == 1 ? 0.4 : 0.99;
    group.rolls.crit_rate = run == 1 ? 0.5 : 0.0;
    group.rolls.crit_dmg = 1.0;
    params.attacks[0].groups.push_back(group);
    CombatSim sim;
    for (int step = 0; step < 50; ++step) {
      sim.Advance(params, 1.0);
      dealt[run] += sim.view().damage_this_step;
    }
  }
  EXPECT_NEAR(dealt[0], 50 * 25.0, 1e-6);
  EXPECT_NEAR(dealt[1], dealt[0], 1e-6);
  EXPECT_NEAR(dealt[2], dealt[0], 1e-6);
}

// A Final Attack landing a fifth of the time counts as a fifth of a hit in a
// measurement, instead of a coin toss that needs a long run to average out.
TEST(CombatSimTest, AMeasuredChanceIsPaidAsItsShare) {
  Mob mob = MakeMob("Snail", 1000000);
  CombatParams params = MakeParams(1.0, 1e9, {MakeType(&mob, 0.0, 1)});
  params.measuring = true;
  FinalAttackRoll source;
  source.damage = {100.0};
  source.chance = 0.2;
  source.count = 1;
  params.attacks[0].final_attack_damage = {20.0};
  params.attacks[0].final_attack_rolls.push_back(source);

  CombatSim sim;
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.view().damage_this_step, 20.0, 1e-6);
}

// Damage per attack, separated. A burn counts for the attack that applied it,
// and a summon counts for no attack, since it runs on its own clock.
TEST(CombatSimTest, AMeasurementTellsTheSwingsApart) {
  Mob mob = MakeMob("Snail", 1000000);
  CombatParams params = MakeParams(1.0, 1e9, {MakeType(&mob, 10.0, 1)});
  params.measuring = true;
  params.dot_count = 1;
  DotApplication burn;
  burn.slot = 0;
  burn.damage = {5.0};
  burn.interval_seconds = 1.0;
  burn.duration_seconds = 100.0;
  burn.chance = 1.0;
  burn.max_stacks = 1;
  params.attacks[0].dots.push_back(burn);
  AttackOption summon;
  summon.max_enemies = 1;
  summon.interval_seconds = 1.0;
  summon.damage_per_hit = {7.0};
  params.auto_attacks.push_back(summon);

  CombatSim sim;
  for (int step = 0; step < 10; ++step) {
    sim.Advance(params, 1.0);
  }
  ASSERT_EQ(sim.by_attack().size(), 1u);
  EXPECT_EQ(sim.by_attack()[0].swings, 10);
  // Ten attacks at 10, plus the burn ticking every second from the first
  // attack.
  EXPECT_NEAR(sim.by_attack()[0].damage, 10 * 10.0 + 9 * 5.0, 1e-6);
  EXPECT_NEAR(sim.own_clock_damage(), 10 * 7.0, 1e-6);
}

// How far a measurement can step: to the next attack landing or the next buff
// change, whichever comes first.
TEST(CombatSimTest, TheNextEventIsTheSwingOrABuff) {
  Mob mob = MakeMob("Snail", 1000000);
  CombatParams params = MakeParams(4.0, 1e9, {MakeType(&mob, 10.0, 1)});
  params.measuring = true;

  CombatSim sim;
  sim.Advance(params, 1.0);
  EXPECT_NEAR(sim.SecondsToNextEvent(params), 3.0, 1e-9);

  BuffOption buff;
  buff.duration_seconds = 2.0;
  buff.cooldown_seconds = 10.0;
  params.buffs.push_back(buff);
  AttackSet set;
  set.attacks = params.attacks;
  params.buffed[1] = std::move(set);
  CombatSim buffed;
  buffed.Advance(params, 1.0);
  // The buff went up on that step with its full duration, which ends sooner
  // than the three seconds the attack still needs.
  EXPECT_NEAR(buffed.SecondsToNextEvent(params), 2.0, 1e-9);

  // A rolled buff with a stack not yet gathered waits on the attack; it must
  // not set the step to zero.
  CombatParams rolled = MakeParams(4.0, 1e9, {MakeType(&mob, 10.0, 1)});
  rolled.measuring = true;
  GiveRolledBuff(rolled, /*stacks=*/3, /*duration=*/10.0, /*chance=*/0.2);
  CombatSim rolling;
  rolling.Advance(rolled, 1.0);
  EXPECT_NEAR(rolling.SecondsToNextEvent(rolled), 3.0, 1e-9);
}

// A fight with a normal attack and a big move on a cooldown, under a buff that
// doubles all damage. The buff is up for `up` seconds of every `every`, so the
// window the fight can see coming opens at `every`.
CombatParams MakeWindowFight(const Mob& boss, double cooldown, double up,
                             double every) {
  CombatParams params = MakeParams(1.0, 0.0, {MakeType(&boss, 10.0, 1)});
  params.attacks.push_back(MakeSkill("Big Move", 100.0, cooldown));
  GiveBuff(params, up, every, /*factor=*/2.0);
  return params;
}

// When each use of the attack at `index` landed, in seconds from the start.
std::vector<double> SwingTimes(CombatSim& sim, const CombatParams& params,
                               int index, double seconds) {
  std::vector<double> times;
  int landed = 0;
  for (int step = 1; step * 0.01 < seconds; ++step) {
    sim.Advance(params, 0.01);
    const std::vector<AttackTally>& tallies = sim.by_attack();
    int now =
        index < static_cast<int>(tallies.size()) ? tallies[index].swings : 0;
    if (now > landed) {
      landed = now;
      times.push_back(step * 0.01);
    }
  }
  return times;
}

// The window doubles all damage, so a press inside it is worth two outside. The
// second press is ready at 21 with the window four seconds away, and waits
// instead of being spent in the gap.
TEST(CombatSimTest, ABigMoveWaitsForTheWindowComing) {
  Mob boss = MakeMob("Zakum", 1000000);
  CombatParams params =
      MakeWindowFight(boss, /*cooldown=*/20.0, /*up=*/3.0, /*every=*/25.0);

  CombatSim sim;
  std::vector<double> times = SwingTimes(sim, params, 1, 30.0);
  ASSERT_EQ(times.size(), 2u);
  EXPECT_NEAR(times[0], 1.0, 0.02);
  EXPECT_NEAR(times[1], 26.0, 0.02);
  EXPECT_NEAR(sim.by_attack()[1].damage, 400.0, 1e-6);  // both doubled
}

// The same fight against a boss that's nearly dead: the window is further off
// than the fight will last, so the press is used while there's still something
// to hit.
TEST(CombatSimTest, NothingIsHeldForAWindowTheFightWontReach) {
  Mob boss = MakeMob("Zakum", 440);
  CombatParams params =
      MakeWindowFight(boss, /*cooldown=*/20.0, /*up=*/3.0, /*every=*/25.0);

  CombatSim sim;
  std::vector<double> times = SwingTimes(sim, params, 1, 30.0);
  ASSERT_EQ(times.size(), 2u);
  EXPECT_NEAR(times[1], 21.0, 0.02);
  EXPECT_NEAR(sim.by_attack()[1].damage, 300.0, 1e-6);  // the second bare
}

// A cooldown that will be ready again before the window opens is used now and
// used again inside it. There's nothing to gain by waiting.
TEST(CombatSimTest, ACooldownBackBeforeTheWindowIsSpentNow) {
  Mob boss = MakeMob("Zakum", 1000000);
  CombatParams params =
      MakeWindowFight(boss, /*cooldown=*/4.0, /*up=*/5.0, /*every=*/20.0);

  CombatSim sim;
  std::vector<double> times = SwingTimes(sim, params, 1, 24.0);
  // Every five seconds throughout: the one at 16 is used with the window four
  // seconds away, and the next lands inside it.
  ASSERT_EQ(times.size(), 5u);
  EXPECT_NEAR(times[3], 16.0, 0.02);
  EXPECT_NEAR(times[4], 21.0, 0.02);
}

// Here the window boosts the normal attack and leaves the big move unchanged,
// so a press is worth less relative to the normal attack inside the window than
// outside. Nothing is held.
TEST(CombatSimTest, NothingIsHeldForAWindowThatLiftsTheFillerInstead) {
  Mob boss = MakeMob("Zakum", 1000000);
  CombatParams params =
      MakeWindowFight(boss, /*cooldown=*/20.0, /*up=*/3.0, /*every=*/25.0);
  params.buffed[1].attacks[1].damage_per_hit[0] = 100.0;

  CombatSim sim;
  std::vector<double> times = SwingTimes(sim, params, 1, 30.0);
  ASSERT_EQ(times.size(), 2u);
  EXPECT_NEAR(times[1], 21.0, 0.02);
}

// Holding the only attack would mean standing still, and the fight never idles
// to wait for a window.
TEST(CombatSimTest, NothingIsHeldWithNothingElseToSwing) {
  Mob boss = MakeMob("Zakum", 1000000);
  CombatParams params =
      MakeWindowFight(boss, /*cooldown=*/20.0, /*up=*/3.0, /*every=*/25.0);
  params.attacks.erase(params.attacks.begin());
  params.buffed[1].attacks.erase(params.buffed[1].attacks.begin());

  CombatSim sim;
  std::vector<double> times = SwingTimes(sim, params, 0, 30.0);
  ASSERT_EQ(times.size(), 2u);
  EXPECT_NEAR(times[1], 22.0, 0.02);
}

// Divine Punishment's shape with room in the bank: a charge every three seconds
// and up to three stored, each buying the whole short hold.
AttackOption MakeRoomyBank() {
  AttackOption punish = MakeSkill("Divine Punishment", 0.0, /*cooldown=*/0.0);
  punish.channel.pulses = 4;
  punish.channel.min_pulses = 2;
  punish.channel.pulse_seconds = 0.15;
  punish.channel.min_seconds = 0.3;
  punish.channel.charge_seconds = 3.0;
  punish.channel.max_charges = 3;
  punish.channel.pulses_per_charge = 4;
  punish.groups.push_back({{25.0}, SwingRolls{}});
  punish.damage_per_hit = {4 * 25.0};
  punish.swing_seconds = HoldSeconds(punish.channel, punish.channel.pulses);
  return punish;
}

// Charges cost nothing to store, so those gained just before a window are saved
// and spent inside it, until the bank is full.
TEST(CombatSimTest, ABankFillsIntoTheWindowAndIsSpentInside) {
  Mob boss = MakeMob("Zakum", 1000000);
  CombatParams params = MakeParams(1.0, 0.0, {MakeType(&boss, 10.0, 1)});
  params.attacks.push_back(MakeRoomyBank());
  GiveBuff(params, /*duration=*/3.0, /*cooldown=*/12.0, /*factor=*/2.0);

  CombatSim sim;
  std::vector<double> times = SwingTimes(sim, params, 1, 16.0);
  int before = 0;
  int inside = 0;
  for (double at : times) {
    if (at > 4.0 && at < 12.0) {
      ++before;
    } else if (at >= 12.0 && at < 15.0) {
      ++inside;
    }
  }
  // The bank started full and was emptied at once, then kept every charge
  // gained in the gap: two of them are used back to back when the window opens.
  EXPECT_EQ(before, 0);
  EXPECT_EQ(inside, 3);
}

}  // namespace
}  // namespace ms
