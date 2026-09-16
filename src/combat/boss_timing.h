/* The beats a boss fight is paced by.
 *
 * Their own header because both ends of a party fight keep to them: the
 * server counts the party in and holds the gap between phases, and a solo run
 * does the same for itself.
 */
#ifndef MS_SRC_COMBAT_BOSS_TIMING_H_
#define MS_SRC_COMBAT_BOSS_TIMING_H_

#include <chrono>

#include "src/combat/constants.h"

namespace ms {

// How often a fight is stepped and redrawn, faster than the rest of the game:
// a charge bar and the numbers a swing leaves are both wasted at a frame the
// swing fits inside. kTickMs because every swing is a whole number of them, so
// a frame this wide never splits one.
inline constexpr std::chrono::milliseconds kBossFightStep(kTickMs);

// How often a fight crosses the wire both ways. Ten times a second -- a bar
// and a damage number are watched, not aimed at -- and faster is work nobody
// sees, the other end not looking until its own beat. A client's screen still
// runs at kBossFightStep; this is what it says out loud.
inline constexpr std::chrono::milliseconds kFightPublishInterval(100);

// The pause before the fight starts, so the player can see what they are up
// against before anything moves.
inline constexpr double kBossCountdownSeconds = 3.0;
// The beat between a phase ending and the next arriving.
inline constexpr double kBossPhaseGapSeconds = 2.0;
// How long a finished fight is held before the screen goes back. An abort
// takes no hold at all: the player asked to leave, and there is nothing left
// on screen for them to watch.
inline constexpr double kBossEndHoldSeconds = 1.0;

}  // namespace ms

#endif  // MS_SRC_COMBAT_BOSS_TIMING_H_
