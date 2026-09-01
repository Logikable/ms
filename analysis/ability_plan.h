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
 * WHICH lines to hold through a reset is a strategy, and it is shaped by one
 * rule of the roll: only the TOP line ever carries the ability's own rank.
 * Lines two and three roll a rank below it, so a preset holds exactly one
 * line worth having and two fillers, and the whole question is which line
 * ends up on top.
 *
 * So the plan is a chase in three phases. Hold nothing while the rank is
 * still being climbed -- a lock buys nothing when what the character is short
 * of is a rank, and it makes every roll of the ladder dearer. Hold nothing
 * after that either, while the best line the rank can produce is not yet on
 * top: a held top line is never rerolled, so holding the wrong one there
 * strands the chase for good. Once it lands, hold it and the best filler, and
 * roll the third until nothing on the sheet is dead weight -- then stop, and
 * let the other preset have the pool.
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

// Rerolls both presets until each has settled or the pool cannot pay for
// another, chasing the best line `climb_to` can produce for each. Returns the
// honor it spent, which is nothing at all before level 160.
int64_t SpendHonorOnAbility(GameState& state, AbilityRank climb_to,
                            const AbilityWorth& farming,
                            const AbilityWorth& bossing);

}  // namespace ms

#endif  // MS_ANALYSIS_ABILITY_PLAN_H_
