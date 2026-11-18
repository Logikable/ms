/* Timing constants for boss fights. They have their own header because both the
 * server (for party fights) and the client (for solo runs) use them.
 */
#ifndef MS_SRC_COMBAT_BOSS_TIMING_H_
#define MS_SRC_COMBAT_BOSS_TIMING_H_

#include <chrono>

#include "src/combat/constants.h"

namespace ms {

// How often a boss fight is stepped and redrawn. Faster than the rest of the
// game, since charge bars and damage numbers would be lost at a slower frame
// rate. Every attack takes a whole number of kTickMs, so a frame never splits
// one.
inline constexpr std::chrono::milliseconds kBossFightStep(kTickMs);

// How often fight state is sent over the network: ten times a second is enough
// for bars and damage numbers, and the other side only reads it on its own
// timer. The client still redraws every kBossFightStep.
inline constexpr std::chrono::milliseconds kFightPublishInterval(100);

// The pause before the fight starts, so the player can see the boss first.
inline constexpr double kBossCountdownSeconds = 3.0;
// The pause between one phase ending and the next starting.
inline constexpr double kBossPhaseGapSeconds = 2.0;
// How long a finished fight stays on screen before returning. Aborting skips
// this, since the player chose to leave.
inline constexpr double kBossEndHoldSeconds = 1.0;

}  // namespace ms

#endif  // MS_SRC_COMBAT_BOSS_TIMING_H_
