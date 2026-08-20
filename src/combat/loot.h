/* What a kill pays: the meso a mob yields, and the rolls that turn a rate per
 * kill into whole items. Pure math -- the caller decides who gets paid.
 *
 * A kill pays what the mob is worth and nothing else. GMS scales the reward by
 * the gap between the player's level and the mob's, which guards a shared
 * economy against risk-free farming of old maps; we have no trading, so the
 * rule protects nothing and only taxes climbing onto the next map early.
 *
 * Rewards are rolled, not banked. A rate of one in five thousand is a chance
 * taken five thousand times, not a counter that pays out on the five
 * thousandth kill, and meso comes in the range GMS drops it in rather than at
 * the middle of that range. Every mean here is the mean the expected-value
 * model had, so nothing rebalances; what changes is that two players who
 * killed the same monsters no longer hold the same purse.
 */
#ifndef MS_SRC_COMBAT_LOOT_H_
#define MS_SRC_COMBAT_LOOT_H_

#include <cstdint>
#include <random>

#include "src/protos/mob.pb.h"

namespace ms {

// Share of kills that drop meso at all, for a character carrying
// `item_drop_pct` extra drop rate. The base is 60%, raised by the rate and
// capped at every kill: a drop that is already certain cannot be made more so.
double MesoDropChance(double item_drop_pct);

// Expected meso one kill of `mob` yields: the drop chance above times the
// mob's level-banded amount, at the Heroic world's 6x rate. The character's own
// meso bonus is applied by the caller, which is where the passives are already
// resolved.
//
// The world rate belongs here and not in AddMeso, which pays out sales as
// well: GMS multiplies what a monster drops, never what an NPC pays.
//
// What RollMeso averages, and the number the meso curve is drawn from. A sim
// measuring the economy wants the mean rather than one sample of it.
double ExpectedMesoPerKill(const Mob& mob, double item_drop_pct);

// Meso `kills` of `mob` actually paid. Each kill takes the drop chance, and
// each drop is worth the mob's level times a multiplier drawn uniformly across
// the band's range -- GMS gives every band its mean plus or minus a fifth. The
// character's own meso bonus is applied by the caller, as above.
int64_t RollMeso(const Mob& mob, int64_t kills, double item_drop_pct,
                 std::mt19937& rng);

// Items `kills` of a drop at `per_kill` each yielded. A rate below one is the
// chance each kill takes; a rate above one pays its whole part every time and
// rolls the rest. A non-finite or non-positive rate yields no drops.
int64_t RollDrops(double per_kill, int64_t kills, std::mt19937& rng);

}  // namespace ms

#endif  // MS_SRC_COMBAT_LOOT_H_
