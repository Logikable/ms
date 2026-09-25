/* Hyper Stats: the points a character earns from level 140, what a level of a
 * stat costs, and what it gives.
 *
 * Fourteen stats, the same for every job. Points are earned per level from 140
 * and can be spent on any of them; a stat's cost rises with its own level, so
 * the cost depends on the level, not the stat. Nothing is stored: the points
 * available are what the character's level has paid, minus what the current
 * allocation costs.
 *
 * A character keeps one allocation per preset slot, and the game picks between
 * them based on what the player is doing; see stat_preset.h.
 * CharacterInstance::AllocateHyperStat decides who can raise what.
 */
#ifndef MS_SRC_CHARACTER_HYPER_STATS_H_
#define MS_SRC_CHARACTER_HYPER_STATS_H_

#include "src/character/stat_preset.h"
#include "src/protos/character.pb.h"

namespace ms {

// The level the Hyper Stat pool opens, and the first level that adds to it.
inline constexpr int kHyperStatUnlockLevel = 140;

// A stat's max level in GMS.
inline constexpr int kMaxHyperStatLevel = 15;

// Until the 5th job advancement, GMS caps every stat five levels lower, so a
// 4th job stops at ten and a 5th job reaches fifteen.
inline constexpr int kHyperStatLevelsBeforeFifthJob = 5;
inline constexpr int kFifthJobStage = 5;

// The level Arcane Force unlocks, the same level Arcane Symbols do. The stat is
// useless without one.
inline constexpr int kArcaneForceHyperLevel = 200;

// Moves a save's farming and bossing allocations into `presets` and pads the
// list to kNumStatPresets. Safe to call more than once; every mutable PresetOf
// calls it first.
void MigrateHyperStats(HyperStats& stats);

// The allocation in `slot`. Returns an empty one if an unmigrated proto doesn't
// have that slot yet.
const HyperStatPreset& PresetOf(const HyperStats& stats, StatPreset slot);
HyperStatPreset& PresetOf(HyperStats& stats, StatPreset slot);

// Points reaching `level` gives: floor(level / 10) - 11, so 3 per level at 140,
// 4 at 150, and 19 at 300. Zero below the unlock level.
int HyperStatPointsAtLevel(int level);

// Every point a character at `level` has earned, spent and unspent. 339 at
// level 200, and 1,699 at 300.
int TotalHyperStatPoints(int level);

// Points needed to raise a stat from `level` - 1 to `level`. Zero for a level
// not in the table.
int HyperStatLevelCost(int level);

// Total points a stat at `level` has cost: 150 at level 10, 550 at 15.
int HyperStatTotalCost(int level);

// The highest level a stat can reach for a character at `job_stage`.
int MaxHyperStatLevel(int job_stage);

// Whether a character at `character_level` can put points into `field` at all.
// Only Arcane Force is ever locked.
bool HyperStatUnlocked(HyperStatField field, int character_level);

// The value of `field` at `level`, in the stat's own units: flat for the four
// main stats, ATT and Arcane Force, whole percents otherwise. GMS widens
// several steps partway up, so these are formulas instead of a table.
double HyperStatBonus(HyperStatField field, int level);

// The level of `field` in `preset`; 0 if nothing is spent on it.
int HyperStatLevel(const HyperStatPreset& preset, HyperStatField field);

// The total cost of every stat in `preset`.
int HyperStatPointsSpent(const HyperStatPreset& preset);

// Sets `field` to `level` in `preset`. A stat set to zero is removed, so an
// allocation only holds what has been spent.
void SetHyperStatLevel(HyperStatPreset& preset, HyperStatField field,
                       int level);

}  // namespace ms

#endif  // MS_SRC_CHARACTER_HYPER_STATS_H_
