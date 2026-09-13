/* What a character spends their Hyper Stat points on.
 *
 * What a level of a stat is WORTH is measured on the character who will spend
 * the points, rather than ranked off a table somebody keeps. That settles ATT
 * against Critical Damage without an argument, and it lets a job's own stat
 * line decide: a Bow Master prices STR at a quarter of DEX and Max HP at
 * nothing, without either being named anywhere.
 *
 * WHICH to buy is then arithmetic rather than strategy: a level's price climbs
 * with the level it reaches, so the pool goes on whichever next level pays the
 * most per point, over and over until nothing is affordable. The allocation is
 * free to throw away and redo, so a character who has outgrown one simply
 * takes it again.
 *
 * The rate is the caller's: the game rates a character by their combat power,
 * which is cheap enough to seed a character with; //analysis rates them by
 * what they take off whatever is in front of them, which is truer and costs a
 * measured fight.
 */
#ifndef MS_SRC_CHARACTER_HYPER_PLAN_H_
#define MS_SRC_CHARACTER_HYPER_PLAN_H_

#include <functional>

#include "src/character/character.h"
#include "src/character/hyper_stats.h"
#include "src/character/stat_preset.h"
#include "src/protos/character.pb.h"

namespace ms {

// What the character is worth. Called on a character this file has just
// changed, so it must read the one it is handed rather than any it captured.
using HyperRate = std::function<double(CharacterInstance&)>;

// What each stat pays at each of its levels, over the character holding none
// of it. Zero for a level the character cannot reach or a stat their level
// does not open.
struct HyperWorth {
  double rate[HyperStatField_ARRAYSIZE][kMaxHyperStatLevel + 1] = {};
};

// Measures every stat at every level it could be raised to, one stat at a time
// over an empty allocation -- two raised together would price each other's.
// `preset` is left empty on return, since every caller follows with the spend.
HyperWorth MeasureHyperWorth(CharacterInstance& character, StatPreset preset,
                             const HyperRate& rate);

// Spends the whole pool on `preset`, best value per point first, throwing away
// whatever was allocated before. Returns the points it spent.
int SpendHyperStats(CharacterInstance& character, StatPreset preset,
                    const HyperWorth& worth);

}  // namespace ms

#endif  // MS_SRC_CHARACTER_HYPER_PLAN_H_
