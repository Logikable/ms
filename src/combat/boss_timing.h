/* The beats a boss fight is paced by.
 *
 * Their own header because both ends of a party fight keep to them: the
 * server counts the party in and holds the gap between phases, and a solo run
 * does the same for itself.
 */
#ifndef MS_SRC_COMBAT_BOSS_TIMING_H_
#define MS_SRC_COMBAT_BOSS_TIMING_H_

namespace ms {

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
