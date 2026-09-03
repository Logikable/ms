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

// How often a fight is stepped and redrawn, which is faster than the rest of
// the game: the arena shows a charge bar filling and the numbers a swing
// leaves, and both are wasted at a frame the swing fits inside.
//
// kTickMs because every swing in the game is a whole number of them --
// SwingIntervalSeconds rounds up to them -- so a frame this wide divides every
// swing exactly and never splits one. The fastest skills swing every 120ms,
// which is four frames.
inline constexpr std::chrono::milliseconds kBossFightStep(kTickMs);

// How often a fight crosses the wire, in both directions: the server tells the
// party what it is fighting this often, and a client reports the swings it has
// landed this often. Ten times a second -- a bar and a damage number are
// watched, not aimed at -- and faster on either end is work nobody sees, since
// the other end is not looking again until the next beat. A client's own
// screen still runs at kBossFightStep; this is only what it says out loud.
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
