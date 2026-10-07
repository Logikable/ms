/* Soul Weapon: a boss's soul on a weapon, made from 10 of its soul shards.
 *
 * Departures from GMS, all deliberate: every soul is Magnificent, so the
 * regular souls, their expiry and the 1% roll are gone; the shards go straight
 * onto the weapon with no soul item between; and there are no soul skills or
 * Soul Collection. The soul gauge's ATT and MATT are always on, as KMS made
 * them in 1.2.419.
 *
 * Pure math over the protos, like potential.h. The caller spends the shards.
 */
#ifndef MS_SRC_ITEM_SOUL_H_
#define MS_SRC_ITEM_SOUL_H_

#include <map>
#include <random>
#include <string>

#include "src/item/potential.h"
#include "src/protos/boss.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"

namespace ms {

inline constexpr int kShardsPerSoul = 10;

// The level the Soul menu entry unlocks at, account-wide like flames.
inline constexpr int kSoulUnlockLevel = 130;

// The full soul gauge's ATT and MATT, the same at every tier we have (GMS's
// Tier D gives 15, and no boss here is Tier D).
inline constexpr int kSoulGaugeAttack = 20;

// Whether `proto` can carry a soul: a primary weapon.
bool TakesSoul(const EquipPrototype& proto);

// What `line` grants at `tier`: points, or a percent where
// SoulLineIsPercent says so.
int SoulLineValue(SoulTier tier, SoulLine line);
// Crit rate, ignored defence and boss damage are always percents; ATT, MATT
// and all stats are at SS only.
bool SoulLineIsPercent(SoulTier tier, SoulLine line);

// A soul of `shard`'s boss with one of the seven lines at equal odds.
Soul RollSoul(const ItemPrototype& shard, std::mt19937& rng);

// The soul's flat stats, the gauge's included. Its percents are in AddSoul.
EquipStats SoulStats(const Soul& soul);
// Adds the soul's percent line to `totals`. Nothing for an unset soul.
void AddSoul(const Soul& soul, PotentialTotals& totals);

// Sets each soul shard's currency_level to the lowest unlock level of a fight
// that drops it, so a tier's shards can be listed by boss. Computed rather
// than stored, like FillTokenShelves, so it can't drift from the drop tables.
void FillShardLevels(const std::map<std::string, Boss>& bosses,
                     std::map<std::string, ItemPrototype>& items);

}  // namespace ms

#endif  // MS_SRC_ITEM_SOUL_H_
