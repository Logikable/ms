/* Chooses what a character spends their Hyper Stat points on.
 *
 * A stat level's value is measured on the character who will spend the points,
 * not ranked from a hand-kept table. That settles ATT versus Critical Damage
 * without debate and lets each job's stats decide: a Bow Master values STR at a
 * quarter of DEX and Max HP at nothing, without either being written anywhere.
 *
 * Which to buy is then arithmetic: a level's price rises with the level, so
 * points go to whichever next level pays the most per point, repeatedly, until
 * nothing is affordable. An allocation can be discarded and redone for free, so
 * a character who has outgrown one just runs it again.
 *
 * The caller supplies the rating: the game rates a character by combat power,
 * which is cheap enough to use when creating characters, while //analysis rates
 * them by damage in a measured fight, which is more accurate but costs a fight.
 */
#ifndef MS_SRC_CHARACTER_HYPER_PLAN_H_
#define MS_SRC_CHARACTER_HYPER_PLAN_H_

#include <functional>

#include "src/character/character.h"
#include "src/character/hyper_stats.h"
#include "src/character/stat_preset.h"
#include "src/protos/character.pb.h"

namespace ms {

// What the character is worth. It is called on a character this file has just
// changed, so it must read the character it is given, not one it captured.
using HyperRate = std::function<double(CharacterInstance&)>;

// What each stat gives at each of its levels, compared to the character with
// none of it. Zero for a level the character can't reach or a stat their level
// hasn't unlocked.
struct HyperWorth {
  double rate[HyperStatField_ARRAYSIZE][kMaxHyperStatLevel + 1] = {};
};

// Measures every stat at every level it could reach, one stat at a time on an
// empty allocation, since two raised together would affect each other's value.
// `preset` is left empty afterwards, because every caller spends next.
HyperWorth MeasureHyperWorth(CharacterInstance& character, StatPreset preset,
                             const HyperRate& rate);

// Spends the whole pool on `preset`, best value per point first, discarding any
// previous allocation. Returns the points spent.
int SpendHyperStats(CharacterInstance& character, StatPreset preset,
                    const HyperWorth& worth);

}  // namespace ms

#endif  // MS_SRC_CHARACTER_HYPER_PLAN_H_
