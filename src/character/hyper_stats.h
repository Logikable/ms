/* Hyper Stats: the points a character earns past level 140, what a level of
 * one costs, and what it is worth.
 *
 * Fifteen stats, the same fifteen for every job. Points are earned per level
 * from 140 and spent on any of them; a stat's cost climbs with its own level,
 * so the cost belongs to the level rather than to the stat. Nothing is
 * banked: what a character has to spend is the points their level has paid
 * out, less what the allocation in front of them costs.
 *
 * A character keeps two allocations -- one for farming and one for bossing --
 * and the game picks between them by what the player is doing.
 *
 * Pure math over the protos, like arcane_force.h. Who is allowed to raise
 * what is CharacterInstance::AllocateHyperStat's business.
 */
#ifndef MS_SRC_CHARACTER_HYPER_STATS_H_
#define MS_SRC_CHARACTER_HYPER_STATS_H_

#include "src/protos/character.pb.h"

namespace ms {

// The level the Hyper Stat pool opens at, and the first level to pay into it.
inline constexpr int kHyperStatUnlockLevel = 140;

// As far as a stat goes in GMS.
inline constexpr int kMaxHyperStatLevel = 15;

// Until the 5th job advancement GMS holds every stat five levels short of
// that. No character here takes a 5th job, so ten is the real ceiling.
inline constexpr int kHyperStatLevelsBeforeFifthJob = 5;
inline constexpr int kFifthJobStage = 5;

// The level Arcane Force opens at, which is the level Arcane Symbols do. The
// stat is worth nothing without one.
inline constexpr int kArcaneForceHyperLevel = 200;

// Which of a character's two allocations is in play. Farming is the one
// everything reads unless it is told otherwise; a boss fight asks for the
// other.
enum class HyperPreset { kFarming, kBossing };

// The allocation `preset` names.
const HyperStatPreset& PresetOf(const HyperStats& stats, HyperPreset preset);
HyperStatPreset& PresetOf(HyperStats& stats, HyperPreset preset);

// Points reaching `level` pays out: floor(level / 10) - 11, so 3 a level at
// 140, 4 at 150, and 19 at 300. Zero below the unlock level.
int HyperStatPointsAtLevel(int level);

// Every point a character at `level` has ever been paid, spent and unspent
// together. 339 at level 200, and 1,699 at 300.
int TotalHyperStatPoints(int level);

// Points that raising a stat from `level` - 1 to `level` costs. Zero for a
// level off the table.
int HyperStatLevelCost(int level);

// Points a stat at `level` has cost altogether: 150 at level 10, 550 at 15.
int HyperStatTotalCost(int level);

// The highest level a stat may reach for a character at `job_stage`.
int MaxHyperStatLevel(int job_stage);

// Whether a character at `character_level` may put points into `field` at
// all. Only Arcane Force is ever held back.
bool HyperStatUnlocked(HyperStatField field, int character_level);

// What `field` at `level` is worth, in the units the stat is stated in:
// flat for the four stats, ATT and Arcane Force, and whole percents for
// everything else. Zero at level 0.
//
// GMS states several of these with a step that widens partway up, which is
// why they are formulas rather than a table -- see the .cc.
double HyperStatBonus(HyperStatField field, int level);

// The level `field` is raised to in `preset`, 0 for a stat with nothing spent
// on it.
int HyperStatLevel(const HyperStatPreset& preset, HyperStatField field);

// What every stat in `preset` has cost altogether.
int HyperStatPointsSpent(const HyperStatPreset& preset);

// Raises or lowers `field` to `level` in `preset`. A stat set back to zero is
// dropped, so an allocation carries only what it has spent on.
void SetHyperStatLevel(HyperStatPreset& preset, HyperStatField field,
                       int level);

}  // namespace ms

#endif  // MS_SRC_CHARACTER_HYPER_STATS_H_
