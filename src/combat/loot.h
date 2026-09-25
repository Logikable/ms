/* What a kill pays: the meso a mob drops, and the rolls that turn a per-kill
 * rate into whole items. Pure math; the caller decides who gets paid.
 *
 * A kill always pays the mob's full value. GMS reduces rewards when the player
 * outlevels the mob, to protect its shared economy from farming old maps. We
 * have no open market, so that rule would only punish players who move up
 * early.
 *
 * Rewards are rolled randomly. A 1-in-5000 drop is a chance on every kill, not
 * a guaranteed drop on kill 5000, and meso varies across GMS's range for each
 * band. Averages match the old fixed-payout model, so balance is unchanged; two
 * players with the same kills just end up with different amounts.
 */
#ifndef MS_SRC_COMBAT_LOOT_H_
#define MS_SRC_COMBAT_LOOT_H_

#include <cstdint>
#include <random>

#include "src/protos/mob.pb.h"

namespace ms {

// Fraction of kills that drop meso: 60% base, raised by drop rate, capped at
// 100%.
double MesoDropChance(double item_drop_pct);

// Average size of one meso drop from `mob`: its level times its band's average
// multiplier, times the Heroic 6x. This is the amount per drop, not per kill;
// MesoDropChance is the chance.
double MeanMesoPerDrop(const Mob& mob);

// Expected meso per kill: the drop chance times the average amount, including
// the Heroic 6x. The caller applies the character's own meso bonus.
//
// The world multiplier lives here rather than in AddMeso, because GMS only
// multiplies monster drops, not NPC sales. RollMeso averages to this.
double ExpectedMesoPerKill(const Mob& mob, double item_drop_pct);

// Meso paid for `kills` of `mob`. Each kill rolls the drop chance, and each
// drop is the mob's level times a multiplier within 20% of its band's average,
// as in GMS.
int64_t RollMeso(const Mob& mob, int64_t kills, double item_drop_pct,
                 std::mt19937& rng);

// Items dropped over `kills` at `per_kill` each. A rate below 1 is a chance per
// kill; above 1, the whole part always drops and the remainder is rolled.
int64_t RollDrops(double per_kill, int64_t kills, std::mt19937& rng);

// The drop rate for one line of a boss's table. Stackable items multiply the
// whole rate, so 250% drop rate on a certain drop gives two plus a 50% chance
// at a third. Gear only raises the fractional part, and the whole part stays as
// written: a boss drops its gear once per clear.
double BossDropRate(const MobDrop& drop, double item_drop_pct);

}  // namespace ms

#endif  // MS_SRC_COMBAT_LOOT_H_
