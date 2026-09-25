/* Sacred Power: the Grandis stat, and how meeting a map's requirement affects a
 * fight.
 *
 * It works like Arcane Force (a map requires an amount, and the gap to what the
 * character has scales both damage dealt and taken), but GMS steps it per point
 * instead of by the share met, and its range is narrower: a twentieth of the
 * damage at the bottom, 1.25x at the top.
 */
#ifndef MS_SRC_CHARACTER_SACRED_POWER_H_
#define MS_SRC_CHARACTER_SACRED_POWER_H_

#include "src/character/arcane_force.h"

namespace ms {

// The level Grandis opens, along with the Sacred Power row. Below it no map
// requires any and the character can't have any.
inline constexpr int kGrandisLevel = 260;

// The factors for `owned` Sacred Power on a map requiring `required`. Below the
// requirement: 1% less damage dealt per point short, down to 5%, and 1.5x
// damage taken within 50 points, 2x beyond that. Above it: 1% more damage dealt
// per two points, up to 125%; damage taken never goes below 1x.
ForceFactors SacredFactorsFor(int owned, int required);

}  // namespace ms

#endif  // MS_SRC_CHARACTER_SACRED_POWER_H_
