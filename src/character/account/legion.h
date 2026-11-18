/* The Legion: the account's characters, given to every one of them.
 *
 * KMS's Overdrive version (1.2.416, June 2026), which replaced the grid with
 * points. Each character from level 60 has a rank by level, and two things come
 * of it:
 *
 *   - Points, from the highest-level characters up to the Legion's member
 *     count, spent on sixteen stats. The rank, from the 42 highest levels'
 *     total, sets the member count and opens the last eight stats.
 *   - A job effect from every ranked character, however many there are.
 *     Duplicates stack: two Heroes give STR twice.
 *
 * Nothing but the allocation is stored. The rest is read off the characters,
 * so a level-up or a deleted character changes it at once.
 */
#ifndef MS_SRC_CHARACTER_LEGION_H_
#define MS_SRC_CHARACTER_LEGION_H_

#include <map>
#include <string>
#include <vector>

#include "src/character/stat_preset.h"
#include "src/protos/character.pb.h"
#include "src/protos/legion.pb.h"

namespace ms {

// The account level that opens the Legion. GMS opens it at a total level of
// 500; the user's call.
inline constexpr int kLegionLevel = 150;
// How many of the highest levels the Legion level sums, from GMS.
inline constexpr int kLegionLevelsCounted = 42;
// The most points any one of the eight base stats takes.
inline constexpr int kLegionBaseStatCap = 15;
// The Legion level each rank adds, and the ranks there are: five tiers of five.
inline constexpr int kLegionLevelPerRank = 500;
inline constexpr int kLegionRanks = 25;

// A character's rank by level, from GMS; kNone below 60.
enum class CharacterRank { kNone, kB, kA, kS, kSS, kSSS };

CharacterRank CharacterRankFor(int level);
// "B" to "SSS", and "" for kNone.
std::string CharacterRankName(CharacterRank rank);
// The points a character of `rank` gives: 1 for B up to 5 for SSS.
int LegionPointsFor(CharacterRank rank);

// The Legion's rank for a Legion level: 0 below Nameless Legion Rank I (500),
// then 1 to kLegionRanks.
int LegionRankFor(int legion_level);
// "Nameless Legion Rank I" and so on; "" for rank 0.
std::string LegionRankName(int rank);
// How many characters give points at `rank`, from GMS: 9 at Nameless I up to 45
// at Supreme V. Rank 0 counts as Nameless I, since the Legion opens before it.
int LegionMemberSlots(int rank);
// The most points one stat may take at `rank`: kLegionBaseStatCap for the base
// eight, and for the other eight GMS's outer grid caps: 0 until Nameless IV,
// then 6, 13, 21, 30 and 40 at Heroic II.
int LegionStatCap(LegionStat stat, int rank);
// Whether `stat` is one of the eight the rank opens.
bool IsExpandedLegionStat(LegionStat stat);
// What one point in `stat` gives, in the stat's own units: flat for STR to
// Magic ATT and Status Resistance, a fraction for the rest. GMS's per-square
// grid values.
double LegionPerPoint(LegionStat stat);

// One character as the Legion sees them.
struct LegionMember {
  Job job = JOB_UNSPECIFIED;
  int level = 0;
};

// What every ranked character's job gives, summed. The four stats are final
// stats.
struct LegionJobEffects {
  int str = 0;
  int dex = 0;
  int int_ = 0;
  int luk = 0;
  double max_hp_pct = 0.0;
  double crit_rate = 0.0;
};

// What the account's characters add up to.
struct LegionSummary {
  int legion_level = 0;
  int rank = 0;
  // Characters who gave points, and the points they gave.
  int members = 0;
  int points = 0;
  LegionJobEffects job_effects;
};

// The job effect one character of `job` at `rank` gives. Nothing for kNone or a
// character with no 2nd job.
LegionJobEffects LegionJobEffectFor(Job job, CharacterRank rank);

// Sums the Legion from every character on the account, in any order.
LegionSummary SummarizeLegion(std::vector<LegionMember> members);

// The preset in `slot`; empty for one never filled.
const LegionPreset& PresetOf(const Legion& legion, StatPreset slot);
// The same, first padding the list to kNumStatPresets.
LegionPreset& PresetOf(Legion& legion, StatPreset slot);

// Points spent in `preset`, as stored.
int LegionPointsSpent(const LegionPreset& preset);

// What `preset` gives when the Legion has `points` and `rank`: each stat cut to
// its cap, then points granted in stat order until they run out. The stored
// allocation isn't changed, so levelling back up restores it.
std::map<LegionStat, int> EffectiveLegionPoints(const LegionPreset& preset,
                                                int rank, int points);

// Moves `delta` points into (or out of) `stat` in `slot`, as far as the stat's
// cap, the points left and zero allow. Returns how many moved.
int SpendLegionPoints(Legion& legion, StatPreset slot, LegionStat stat,
                      int delta, const LegionSummary& summary);
// Takes every point out of `slot`. Free, as in KMS.
void ResetLegionPreset(Legion& legion, StatPreset slot);
// Swaps two presets; the in-use marker moves with them.
void SwapLegionPresets(Legion& legion, StatPreset a, StatPreset b);

}  // namespace ms

#endif  // MS_SRC_CHARACTER_LEGION_H_
