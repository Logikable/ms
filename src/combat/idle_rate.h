/* The closed-form kill-rate model: kills computed from elapsed time, without
 * stepping the fight.
 *
 * This is NOT how live combat works -- fight.h steps that in real time. This is
 * the model offline catch-up will need, since it has to settle hours of absence
 * in one call rather than replaying them. It is kept and tested against that
 * day; nothing in the live path calls it.
 */
#ifndef MS_SRC_COMBAT_IDLE_RATE_H_
#define MS_SRC_COMBAT_IDLE_RATE_H_

#include <cstdint>
#include <vector>

#include "src/combat/constants.h"
#include "src/combat/damage.h"
#include "src/protos/equip.pb.h"
#include "src/protos/mob.pb.h"

namespace ms {

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

}  // namespace ms

#endif  // MS_SRC_COMBAT_IDLE_RATE_H_
