/* Inner Ability: three rolled stat lines a character has, and the reset that
 * rerolls them for honor.
 *
 * Every line has a rank, which decides its value. GMS gives each pairing a
 * range and rolls within it; this game always gives the top of the range, so a
 * line's value depends only on its type and rank and is never stored.
 *
 * The ability as a whole also has a rank. Its first line always has that rank,
 * the other two roll one or more ranks below it, and a reset can raise the
 * ability's rank but never lower it.
 *
 * A character keeps one setup per preset slot, and all are paid for from the
 * same honor pool.
 *
 * Pure math over the protos, like hyper_stats.h.
 * CharacterInstance::ResetAbility handles spending the honor.
 */
#ifndef MS_SRC_CHARACTER_INNER_ABILITY_H_
#define MS_SRC_CHARACTER_INNER_ABILITY_H_

#include <cstdint>
#include <random>

#include "src/character/stat_preset.h"
#include "src/protos/character.pb.h"

namespace ms {

// The level the Ability panel opens. Below it, the character's lines grant
// nothing.
inline constexpr int kInnerAbilityUnlockLevel = 160;

// Lines every ability has. At most two can be locked, since locking all three
// would leave nothing to reroll.
inline constexpr int kAbilityLines = 3;
inline constexpr int kMaxLockedAbilityLines = 2;

// What every character starts with, in every preset: three Rare All Stats
// lines, +10 all stats each.
inline constexpr AbilityLineType kDefaultAbilityLineType =
    ABILITY_LINE_TYPE_ALL_STATS;
inline constexpr AbilityRank kDefaultAbilityRank = ABILITY_RANK_RARE;

// Moves a save's farming and bossing setups into `presets` and pads the list to
// kNumStatPresets. Works like MigrateHyperStats and is called the same way.
void MigrateInnerAbility(InnerAbility& ability);

// The setup in `slot`. Returns an empty one if an unmigrated proto doesn't have
// that slot yet.
const AbilityPreset& PresetOf(const InnerAbility& ability, StatPreset slot);
AbilityPreset& PresetOf(InnerAbility& ability, StatPreset slot);

// The three Rare All Stats lines a new character starts with.
AbilityPreset DefaultAbilityPreset();

// The value of `type` at `rank`, in the line's own units: flat for stats and
// attack, whole percents for the rest, one stage for Attack Speed. Zero for a
// pairing GMS doesn't offer.
int AbilityLineValue(AbilityLineType type, AbilityRank rank);

// How likely `type` is to roll at `rank`. Zero means it can't roll there, which
// is how GMS gates lines. The ratios are GMS's; the scale isn't, since a roll
// normalises over what is available.
int AbilityTypeWeight(AbilityLineType type, AbilityRank rank);

// Honor a reset costs for an ability at `rank` with `locked` lines. The price
// depends on the ability's rank, not the locked lines' ranks, and GMS charges
// for locks on every reset instead of once; see the table.
int64_t AbilityResetCost(AbilityRank rank, int locked);

// The chance a reset at `rank` raises the ability one rank. Zero at Legendary,
// the top rank.
double AbilityRankUpChance(AbilityRank rank);

// How many lines in `preset` are locked.
int LockedAbilityLines(const AbilityPreset& preset);

// Locks or unlocks the line at `index`. A line of any rank can be locked; a
// third lock is refused, and unlocking always works. Returns whether the preset
// changed.
bool SetAbilityLineLocked(AbilityPreset& preset, int index, bool locked);

// Rerolls `preset` in place without charging: the ability rolls for a rank up,
// unlocked lines are discarded, and new lines are rolled to fill it back to
// three.
//
// The top line always ends at the ability's rank: a locked line there counts if
// it has that rank, otherwise a new line is rolled at that rank and the locked
// lines move down. No two lines share a type.
void RerollAbility(AbilityPreset& preset, std::mt19937& rng);

}  // namespace ms

#endif  // MS_SRC_CHARACTER_INNER_ABILITY_H_
