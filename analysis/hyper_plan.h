/* What a character spends their Hyper Stat points on.
 *
 * Shaped like ability_plan: what a level of a stat is WORTH is measured on the
 * character who will spend the points -- the crowd for farming, the fight for
 * bossing -- rather than ranked off a table somebody keeps. That settles ATT
 * against Critical Damage without an argument, and it lets the two presets
 * want different things, which is what having two of them is for.
 *
 * WHICH to buy is then arithmetic rather than strategy: a level's price climbs
 * with the level it reaches, so the pool goes on whichever next level pays the
 * most per point, over and over until nothing is affordable. The allocation is
 * free to throw away and redo, so a character who has outgrown one simply
 * takes it again.
 */
#ifndef MS_ANALYSIS_HYPER_PLAN_H_
#define MS_ANALYSIS_HYPER_PLAN_H_

#include <functional>

#include "src/character/hyper_stats.h"
#include "src/character/stat_preset.h"
#include "src/game_state.h"
#include "src/protos/character.pb.h"

namespace ms {

// What the character takes off whatever is in front of them. Called on a
// character this file has just changed, so it must read the state it is handed
// rather than any it captured.
using HyperRate = std::function<double(GameState&)>;

// What each stat pays at each of its levels, over the character holding none
// of it. Zero for a level the character cannot reach or a stat their level
// does not open.
struct HyperWorth {
  double rate[HyperStatField_ARRAYSIZE][kMaxHyperStatLevel + 1] = {};
};

// Measures every stat at every level it could be raised to. Tried in `preset`
// and put back before returning, so `rate` must be one that reads that preset.
HyperWorth MeasureHyperWorth(GameState& state, StatPreset preset,
                             const HyperRate& rate);

// Spends the whole pool on `preset`, best value per point first, throwing away
// whatever was allocated before. Returns the points it spent.
int SpendHyperStats(GameState& state, StatPreset preset,
                    const HyperWorth& worth);

}  // namespace ms

#endif  // MS_ANALYSIS_HYPER_PLAN_H_
