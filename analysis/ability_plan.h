/* What a character rolls their Inner Ability into, and what the honor buys.
 *
 * A climb that never spends its honor reads a character weaker than the one a
 * player would be standing in: the three Rare All Stats lines everybody starts
 * with are exactly what a reroll is there to replace. Two questions, and only
 * the first has a measured answer.
 *
 * What a line is WORTH is measured, the way the map and the book are: the
 * character holding one line of it, against the same character holding none.
 * That settles Attack against Magic Attack without a table anybody has to
 * keep, and it lets the two presets want different things -- which is what
 * having two of them is for.
 *
 * WHICH lines to hold through a reset is a strategy. A lock buys nothing while
 * what the character is short of is a RANK: rolling with nothing held is the
 * cheapest way up the ladder, and the ladder is where the lines worth holding
 * live at all. So the plan is two-phase -- hold nothing until the ability
 * reaches the rank being climbed to, then hold the two worth the most and roll
 * the third. Rank never falls and a held line is never rerolled, so from there
 * the ability only ratchets up.
 */
#ifndef MS_ANALYSIS_ABILITY_PLAN_H_
#define MS_ANALYSIS_ABILITY_PLAN_H_

#include <cstdint>
#include <functional>

#include "src/character/stat_preset.h"
#include "src/game_state.h"
#include "src/protos/character.pb.h"

namespace ms {

// What the character takes off whatever is in front of them. Called on a
// character this file has just changed, so it must read the state it is handed
// rather than any it captured.
using AbilityRate = std::function<double(GameState&)>;

// What one line of each type and rank pays, in the units the rate is in.
// Zero for a pairing the roll never produces.
struct AbilityWorth {
  double rate[AbilityLineType_ARRAYSIZE][AbilityRank_ARRAYSIZE] = {};

  double Of(const AbilityLine& line) const {
    return rate[line.type()][line.rank()];
  }
};

// Measures every pairing a roll can produce against `rate`: the character
// holding one line of it, less the same character holding none. Tried in
// `preset` and restored before returning, so `rate` must be one that reads
// that preset -- the crowd for farming, the fight for bossing.
AbilityWorth MeasureAbilityWorth(GameState& state, StatPreset preset,
                                 const AbilityRate& rate);

// Rerolls both presets until the pool cannot pay for another, holding the
// lines each is worth the most for once that preset has reached `climb_to`.
// Returns the honor it spent, which is nothing at all before level 160.
int64_t SpendHonorOnAbility(GameState& state, AbilityRank climb_to,
                            const AbilityWorth& farming,
                            const AbilityWorth& bossing);

}  // namespace ms

#endif  // MS_ANALYSIS_ABILITY_PLAN_H_
