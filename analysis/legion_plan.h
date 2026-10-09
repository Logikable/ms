/* Chooses what a simulated account spends its Legion points on.
 *
 * Max mode's allocator (//src/character:legion_plan) re-measures every point,
 * which a measured fight makes too slow here. Instead each stat is measured
 * once, filled alone as far as it goes, and the points then go to the best
 * per-point worth first, each stat to its cap. Crit rate past the cap and
 * Ignore Defense piling up are averaged over the fill rather than followed.
 */
#ifndef MS_ANALYSIS_LEGION_PLAN_H_
#define MS_ANALYSIS_LEGION_PLAN_H_

#include <functional>

#include "src/character/character.h"
#include "src/character/stat_preset.h"
#include "src/game_state.h"
#include "src/protos/legion.pb.h"

namespace ms {

// What one point in each stat is worth to the rate, by LegionStat.
struct LegionWorth {
  double per_point[LegionStat_ARRAYSIZE] = {};
};

// Measures every stat in `preset`, each filled alone to its cap or the points
// the Legion has. `rate` must read that preset. The character and its Legion
// are restored before returning.
LegionWorth MeasureLegionWorth(GameState& state, StatPreset preset,
                               const std::function<double(GameState&)>& rate);

// Spends every point on `preset` of the character's Legion copy, best worth per
// point first, each stat to its cap, discarding what was there. A stat worth
// nothing still takes what is left, in stat order. Returns the points spent.
int SpendLegionByWorth(CharacterInstance& character, StatPreset preset,
                       const LegionWorth& worth);

}  // namespace ms

#endif  // MS_ANALYSIS_LEGION_PLAN_H_
