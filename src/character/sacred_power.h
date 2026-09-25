/* Sacred Power: the Grandis stat, and what meeting a map's requirement does
 * to a fight.
 *
 * Arcane Force's twin -- a map asks for a number, and the gap to what the
 * character carries scales both sides -- but GMS steps it by the POINT rather
 * than by the share met, and caps it lower: a twentieth of the damage at the
 * bottom, a quarter again at the top.
 */
#ifndef MS_SRC_CHARACTER_SACRED_POWER_H_
#define MS_SRC_CHARACTER_SACRED_POWER_H_

#include "src/character/arcane_force.h"

namespace ms {

// The level Grandis opens at, and with it the Sacred Power row: below it no
// map asks for any and the character can carry none.
inline constexpr int kGrandisLevel = 260;

// The factors for `owned` Sacred Power against a map asking `required`. Short
// of it, 1% less dealt per point down to 5%, and 1.5x taken within 50 points,
// 2x past that. Over it, 1% more dealt per two points up to 125%; what is
// taken never drops below 1x.
ForceFactors SacredFactorsFor(int owned, int required);

}  // namespace ms

#endif  // MS_SRC_CHARACTER_SACRED_POWER_H_
