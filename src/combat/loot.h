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

// Share of kills that drop meso at all. The base is 60%, raised by the rate
// and capped at certain.
double MesoDropChance(double item_drop_pct);

// What one meso drop off `mob` averages: its level times the band's mean
// multiplier, at the Heroic 6x. The AMOUNT, not the per-kill expectation --
// MesoDropChance is the chance, and showing both would count it twice.
double MeanMesoPerDrop(const Mob& mob);

// Expected meso per kill: the drop chance times the level-banded amount, at
// the Heroic 6x. The character's own bonus is the caller's to apply.
//
// The world rate belongs here rather than in AddMeso, which pays sales too:
// GMS multiplies what a MONSTER drops, never what an NPC pays. This is what
// RollMeso averages, and what the meso curve is drawn from.
double ExpectedMesoPerKill(const Mob& mob, double item_drop_pct);

// Meso `kills` of `mob` actually paid: each kill takes the drop chance, and
// each drop is the mob's level times a multiplier drawn across the band --
// GMS gives every band its mean plus or minus a fifth.
int64_t RollMeso(const Mob& mob, int64_t kills, double item_drop_pct,
                 std::mt19937& rng);

// Items `kills` of a drop at `per_kill` yielded. Below one is a chance per
// kill; above one pays its whole part every time and rolls the rest.
int64_t RollDrops(double per_kill, int64_t kills, std::mt19937& rng);

// The rate one line of a boss's table rolls at. What the rate buys depends on
// what falls: a STACKABLE takes the plain multiply, so 250% on a certain drop
// is two outright and a coin flip for a third, where GEAR takes a better
// chance and nothing more -- the whole part stands as the table wrote it and
// only the fraction is lifted. A boss pays its table once, and one necklace is
// what the fight is worth.
double BossDropRate(const MobDrop& drop, double item_drop_pct);

}  // namespace ms

#endif  // MS_SRC_COMBAT_LOOT_H_
