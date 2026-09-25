/* Chooses what a simulated character spends Hyper Stat points on.
 *
 * The allocator itself is //src/character:hyper_plan, shared with --mode=max.
 * Only the rate differs. The game rates a character by combat power, which is
 * cheap enough for seeding. Here the rate is the damage the character actually
 * deals in a measured fight, which is more accurate but slower.
 */
#ifndef MS_ANALYSIS_HYPER_PLAN_H_
#define MS_ANALYSIS_HYPER_PLAN_H_

#include <functional>

#include "src/character/hyper_plan.h"
#include "src/character/stat_preset.h"
#include "src/game_state.h"

namespace ms {

// Damage rate the character deals against its current target. It's called on a
// character this file has just changed, so it must read the state passed in,
// not any state it captured.
using MeasuredHyperRate = std::function<double(GameState&)>;

// Measures every stat at every level it could be raised to. Each is tried in
// `preset`, and the character is restored before returning. So `rate` must read
// that preset, and may leave the rest of the state however it likes.
HyperWorth MeasureHyperWorth(GameState& state, StatPreset preset,
                             const MeasuredHyperRate& rate);

// Spends the whole pool on `preset`, best value per point first, discarding the
// previous allocation. Returns the points spent.
int SpendHyperStats(GameState& state, StatPreset preset,
                    const HyperWorth& worth);

}  // namespace ms

#endif  // MS_ANALYSIS_HYPER_PLAN_H_
