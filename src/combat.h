#ifndef MS_SRC_COMBAT_H_
#define MS_SRC_COMBAT_H_

#include <cstdint>
#include <vector>

#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/mob.pb.h"

namespace ms {

// GMS global respawn tick: every 7.56s the server refills up to one mob per
// spawn point. A map's full-clear kill cap is spawn_count / this.
constexpr double kRespawnIntervalSeconds = 7.56;

// Our game runs this many times slower than GMS; the one global pacing knob.
// Kill cycles and the cosmetic combat view are both stretched by it.
constexpr double kGameSpeedFactor = 10.0;

// Offensive parameters feeding the GMS damage formula. Modifier fields default
// to their identity (no-effect) value; real values graduate in one at a time as
// gear, skills, etc. produce them.
struct OffenseStats {
  int primary = 0;             // total primary stat
  int secondary = 0;           // total secondary stat
  int attack = 0;              // total weapon/gear attack
  double mastery = 0.15;       // 0..1; raises min damage (beginner placeholder)
  double skill_pct = 1.0;      // skill damage multiplier (1.0 == 100%)
  int lines = 1;               // hits per attack
  double damage_pct = 0.0;     // additive %dmg, as fraction
  double boss_pct = 0.0;       // additive boss %dmg; applies only vs bosses
  double crit_rate = 0.0;      // 0..1
  double crit_dmg = 0.0;       // crit damage bonus, atop the hidden 0.35 base
  double final_dmg_pct = 0.0;  // final damage, as fraction
  double ied = 0.0;            // ignore enemy defense, 0..1
  double ier = 0.0;            // ignore elemental resistance, 0..1
};

// Builds OffenseStats from a character's job and summed (allocated + equipped)
// stats. Job picks primary/secondary; attack and boss_pct/ied come from gear;
// the rest keep identity defaults until skills/gear supply them.
OffenseStats OffenseStatsFor(Job job, const AllocatedStats& allocated,
                             const EquipStats& equipped);

// Expected damage of one full attack against `mob` (crit averaged over its
// rate, no RNG). The GMS damage chain; mob PDR and boss flag come from the Mob.
double ExpectedAttackDamage(const OffenseStats& offense, const Mob& mob);

// Seconds between swings: base_delay_ms * (20 - stage) / 16, rounded up to 30ms
// ticks. Stage 1..10, 10 fastest, 4 == base.
double SwingIntervalSeconds(int base_delay_ms, int attack_speed_stage);

// Base swing animation (ms) for a weapon type — the stage-4 reference scaled by
// SwingIntervalSeconds. A weapon-class property, not per-item.
int BaseAttackDelayMs(EquipType equip_type);

// Wall-clock seconds between kills at one spawn slot: discrete hits
// (ceil(max_hp / damage_per_hit), overkill wasted) give a kill time, rounded up
// to whole respawn ticks, then stretched by the game-speed factor. +inf if the
// mob can't be damaged.
double KillCycleSeconds(
    double damage_per_hit, double swing_interval_seconds, const Mob& mob,
    double respawn_interval_seconds = kRespawnIntervalSeconds);

// Effective seconds between kills of each mob type when farming the whole map,
// index-aligned with `mobs`. spawn_count slots split evenly across the N types
// cycle in parallel: period_m = KillCycleSeconds(...) * N / spawn_count. +inf
// when a mob can't be killed or no slots feed it.
std::vector<double> MapKillPeriods(
    const OffenseStats& offense, const EquipPrototype& weapon,
    const std::vector<const Mob*>& mobs, int spawn_count,
    double respawn_interval_seconds = kRespawnIntervalSeconds);

// Advances a fractional-kill accumulator by elapsed_seconds at
// `period_seconds`, returning whole kills completed and carrying the remainder
// in *accumulator. A non-finite or non-positive period yields no kills. Keeps
// rewards discrete: callers grant a mob's full EXP/drops per whole kill.
int64_t FlushKills(double period_seconds, double elapsed_seconds,
                   double* accumulator);

// Banks `kills` worth of a drop at `per_kill` expected items each into a
// fractional accumulator, returning whole items dropped and carrying the
// remainder in *accumulator. A non-finite or non-positive per_kill yields no
// drops. The expected-value counterpart to RNG drop rolls.
int64_t FlushDrops(double per_kill, int64_t kills, double* accumulator);

// Fraction of meso retained after the player/monster level-difference penalty,
// in [0, 1]. level_difference = player_level - mob_level. Within +/-10 there is
// no penalty; beyond that the amount is reduced, more harshly when the player
// out-levels the mob. Applied last, after the base amount.
double MesoLevelPenalty(int level_difference);

// Expected meso one kill of `mob` yields a player at player_level: the 60% base
// drop chance times the mob's level-banded amount times the level penalty.
// Ignores the character's Mesos Obtained and item-drop-rate stats (deferred).
double ExpectedMesoPerKill(const Mob& mob, int player_level);

}  // namespace ms

#endif  // MS_SRC_COMBAT_H_
