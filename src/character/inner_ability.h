/* Inner Ability: three rolled stat lines a character carries, and the reset
 * that rerolls them for honor.
 *
 * Every line has a rank, and the rank decides what the line is worth. GMS
 * states each pairing as a range and rolls within it; this game always hands
 * over the top of the range, so a line's value is a function of its type and
 * its rank and is never stored.
 *
 * The ability as a whole has a rank too. Its first line always carries that
 * rank, the other two are rolled a rung or more below it, and a reset can
 * carry the whole ability up a rank but never down.
 *
 * A character keeps one setup per StatPreset, and both are paid for out of
 * the one honor pool.
 *
 * Pure math over the protos, like hyper_stats.h. Spending the honor is
 * CharacterInstance::ResetAbility's business.
 */
#ifndef MS_SRC_CHARACTER_INNER_ABILITY_H_
#define MS_SRC_CHARACTER_INNER_ABILITY_H_

#include <cstdint>
#include <random>

#include "src/character/stat_preset.h"
#include "src/protos/character.pb.h"

namespace ms {

// The level the Ability panel opens at. Below it the lines a character is
// holding grant nothing.
inline constexpr int kInnerAbilityUnlockLevel = 160;

// Lines every ability has, and the most a reset may hold. Locking all three
// would leave nothing to reroll.
inline constexpr int kAbilityLines = 3;
inline constexpr int kMaxLockedAbilityLines = 2;

// What every character starts with, in both presets: three Rare All Stats
// lines, which is +10 all stat apiece.
inline constexpr AbilityLineType kDefaultAbilityLineType =
    ABILITY_LINE_TYPE_ALL_STATS;
inline constexpr AbilityRank kDefaultAbilityRank = ABILITY_RANK_RARE;

// The setup `preset` names.
const AbilityPreset& PresetOf(const InnerAbility& ability, StatPreset preset);
AbilityPreset& PresetOf(InnerAbility& ability, StatPreset preset);

// The three Rare All Stats lines a new character is handed.
AbilityPreset DefaultAbilityPreset();

// What `type` at `rank` is worth, in the units the line is stated in: flat for
// the stats, Max HP and the two attacks, whole percents for the rest, and a
// single faster-swing stage for Attack Speed. Zero for a pairing GMS does not
// offer, which is the same set AbilityTypeWeight gives no weight.
int AbilityLineValue(AbilityLineType type, AbilityRank rank);

// How heavily `type` is favoured in a roll at `rank`, against the other types
// rolling there. Zero says the type does not roll at that rank at all, which
// is how GMS's gating is stated -- Boss Damage below Unique, Attack Speed
// below Legendary. Relative sizes are GMS's; the scale is not, since a roll
// normalises over whatever types are still available.
int AbilityTypeWeight(AbilityLineType type, AbilityRank rank);

// Honor a reset costs for an ability at `rank` holding `locked` lines. The
// price is the ABILITY's rank, whatever the ranks of the lines being held, and
// GMS prices the locks per reset rather than once -- see the table.
int64_t AbilityResetCost(AbilityRank rank, int locked);

// Chance a reset at `rank` carries the ability to the rank above. Zero at
// Legendary, which is the top.
double AbilityRankUpChance(AbilityRank rank);

// Lines `preset` is currently holding.
int LockedAbilityLines(const AbilityPreset& preset);

// Locks or unlocks the line at `index`. Any line may be held whatever its
// rank; a third lock is refused, and unlocking is always allowed. Returns
// whether the preset changed.
bool SetAbilityLineLocked(AbilityPreset& preset, int index, bool locked);

// Rerolls `preset` in place, paying nothing -- the honor is the caller's to
// take. In order: the ability rolls for its rank up, every unheld line is
// thrown away, and what is left is rolled back to three.
//
// The top line always ends at the ability's rank. A held line already sitting
// there satisfies that; otherwise a fresh line is rolled at the rank and put
// on top, and the held lines slide down a slot to make room. No two lines
// ever share a type.
void RerollAbility(AbilityPreset& preset, std::mt19937& rng);

}  // namespace ms

#endif  // MS_SRC_CHARACTER_INNER_ABILITY_H_
