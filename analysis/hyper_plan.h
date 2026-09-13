/* What a simulated character spends their Hyper Stat points on.
 *
 * The allocator itself is //src/character:hyper_plan, which both this and
 * --mode=max drive. What differs is the rate: the game asks what a character
 * is worth by their combat power against the fight ahead of them, which is
 * cheap enough to seed with; here it is what they actually take off whatever
 * is in front of them, which is truer and costs a measured fight.
 */
#ifndef MS_ANALYSIS_HYPER_PLAN_H_
#define MS_ANALYSIS_HYPER_PLAN_H_

#include <functional>

#include "src/character/hyper_plan.h"
#include "src/character/stat_preset.h"
#include "src/game_state.h"

namespace ms {

// What the character takes off whatever is in front of them. Called on a
// character this file has just changed, so it must read the state it is handed
// rather than any it captured.
using MeasuredHyperRate = std::function<double(GameState&)>;

// Measures every stat at every level it could be raised to. Tried in `preset`
// and the character put back before returning, so `rate` must be one that
// reads that preset -- and may leave the rest of the state however it likes.
HyperWorth MeasureHyperWorth(GameState& state, StatPreset preset,
                             const MeasuredHyperRate& rate);

// Spends the whole pool on `preset`, best value per point first, throwing away
// whatever was allocated before. Returns the points it spent.
int SpendHyperStats(GameState& state, StatPreset preset,
                    const HyperWorth& worth);

}  // namespace ms

#endif  // MS_ANALYSIS_HYPER_PLAN_H_
