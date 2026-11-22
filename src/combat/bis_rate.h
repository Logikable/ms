/* The rate --mode=bis ranks its choices by: damage per second against the
 * toughest boss the character's level has unlocked, played out by the
 * measurement fight rather than read off one swing, so cooldowns, buff
 * duration and attack speed count.
 */
#ifndef MS_SRC_COMBAT_BIS_RATE_H_
#define MS_SRC_COMBAT_BIS_RATE_H_

#include "src/game_state.h"

namespace ms {

// Damage per second at 1x against a never-dying copy of MaxBossTarget, over
// twice the slowest cooldown the character holds and never under three
// minutes. Falls back to BisSwingRate when no boss has opened.
double BisFightRate(GameState& state);

}  // namespace ms

#endif  // MS_SRC_COMBAT_BIS_RATE_H_
