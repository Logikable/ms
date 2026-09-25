/* Chooses a character's Inner Ability lines and spends honor on rerolls.
 *
 * A line's value is measured: the character holding one line of it, against the
 * same character holding none. That lets the two presets want different lines.
 *
 * Only the top line has the ability's own rank, so the plan runs in three
 * phases. Lock nothing while climbing ranks, since a lock makes every roll cost
 * more. Keep locking nothing until the rank's best line lands on top. Then lock
 * it and the best filler, and reroll the third until no line is dead weight.
 */
#ifndef MS_ANALYSIS_ABILITY_PLAN_H_
#define MS_ANALYSIS_ABILITY_PLAN_H_

#include <cstdint>
#include <functional>

#include "src/character/stat_preset.h"
#include "src/game_state.h"
#include "src/protos/character.pb.h"

namespace ms {

// Damage rate the character deals against its current target. It's called on a
// character this file has just changed, so it must read the state passed in,
// not any state it captured.
using AbilityRate = std::function<double(GameState&)>;

// Value of one line of each type and rank, in the rate's units. Zero for a
// combination the roll never produces.
struct AbilityWorth {
  double rate[AbilityLineType_ARRAYSIZE][AbilityRank_ARRAYSIZE] = {};

  double Of(const AbilityLine& line) const {
    return rate[line.type()][line.rank()];
  }
};

// Measures every combination a roll can produce against `rate`: the character
// holding one line of it, minus the same character holding none. Each is tried
// in `preset` and the character is restored before returning, so `rate` must
// read that preset (the crowd for farming, the fight for bossing).
AbilityWorth MeasureAbilityWorth(GameState& state, StatPreset preset,
                                 const AbilityRate& rate);

// Rerolls `preset` until it settles or the honor runs out, chasing the best
// line `climb_to` can produce. Returns the honor spent. Works on one preset,
// not both: the pool is shared, and splitting it would finish neither.
int64_t SpendHonorOnAbility(GameState& state, AbilityRank climb_to,
                            StatPreset preset, const AbilityWorth& worth);

}  // namespace ms

#endif  // MS_ANALYSIS_ABILITY_PLAN_H_
