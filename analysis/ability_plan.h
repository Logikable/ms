/* Chooses a character's Inner Ability lines and spends honor on rerolls.
 *
 * A climb that never spends honor measures a weaker character than a real
 * player would have. The three Rare All Stats lines everyone starts with are
 * exactly what rerolls are meant to replace. There are two questions, and only
 * the first has a measured answer.
 *
 * A line's value is measured, like maps and skills: the character holding one
 * line of it, against the same character holding none. That settles Attack
 * against Magic Attack without a hand-kept table, and lets the two presets want
 * different lines, which is the point of having two.
 *
 * Which lines to lock through a reroll is a strategy, shaped by one rule: only
 * the top line has the ability's own rank. Lines two and three roll a rank
 * below, so a preset has exactly one strong line and two fillers, and the real
 * question is which line ends up on top.
 *
 * So the plan runs in three phases. Lock nothing while climbing ranks: a lock
 * doesn't help when the character needs a rank, and it makes every roll cost
 * more. Keep locking nothing until the rank's best line lands on top, since a
 * locked top line is never rerolled and locking the wrong one ends the chase
 * for good. Once it lands, lock it and the best filler, and reroll the third
 * until no line is dead weight. Then stop and leave the pool to the other
 * preset.
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
