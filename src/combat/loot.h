/* What a kill pays: the meso a mob yields, and the accumulator that turns
 * fractional expected drops into whole items. Pure math -- the caller decides
 * who gets paid.
 */
#ifndef MS_SRC_COMBAT_LOOT_H_
#define MS_SRC_COMBAT_LOOT_H_

#include <cstdint>

#include "src/protos/mob.pb.h"

namespace ms {

// Fraction of meso retained after the player/monster level-difference penalty,
// in [0, 1]. level_difference = player_level - mob_level. Within +/-10 there is
// no penalty; beyond that the amount is reduced, more harshly when the player
// out-levels the mob. Applied last, after the base amount.
double MesoLevelPenalty(int level_difference);

// Expected meso one kill of `mob` yields a player at player_level: the 60% base
// drop chance times the mob's level-banded amount times the level penalty.
// Ignores the character's Mesos Obtained and item-drop-rate stats (deferred).
double ExpectedMesoPerKill(const Mob& mob, int player_level);

// Banks `kills` worth of a drop at `per_kill` expected items each into a
// fractional accumulator, returning whole items dropped and carrying the
// remainder in *accumulator. A non-finite or non-positive per_kill yields no
// drops. The expected-value counterpart to RNG drop rolls.
int64_t FlushDrops(double per_kill, int64_t kills, double* accumulator);

}  // namespace ms

#endif  // MS_SRC_COMBAT_LOOT_H_
