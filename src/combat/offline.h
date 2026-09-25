/* Pays the player for the time the game was closed.
 *
 * The rate is measured, not estimated: a fresh CombatSim runs a sample of the
 * absence on the player's current map, and its kills are scaled up to the full
 * time. Since it uses the same combat engine, offline pay matches what the
 * player would have earned watching.
 *
 * The character's level and spent points stay frozen for the whole absence. A
 * player who gains ten levels offline farms all of it at their starting rate,
 * and gets the AP and SP when they return. Nothing plays the character better
 * than they left it.
 *
 * Death is the one approximation. A sample can't prove a map is safe forever,
 * so a trend line is fitted to the player's HP across it. If HP drains faster
 * than it recovers, the player is paid up to when it would run out and sent
 * home. See ApplyOfflineProgress.
 */
#ifndef MS_SRC_COMBAT_OFFLINE_H_
#define MS_SRC_COMBAT_OFFLINE_H_

#include <cstdint>
#include <string>

#include "src/combat/combat.h"
#include "src/game_state.h"

namespace ms {

// How long the sample runs before the rest of the absence is scaled from it.
// Long enough for dozens of respawns at the slowest game speed, so one lucky
// respawn doesn't set the rate.
constexpr double kOfflineSampleSeconds = 600.0;

// Sample step size. Matches the live tick so the fight behaves the same
// offline.
constexpr double kOfflineStepSeconds = 0.1;

// What an absence paid, for the pop-up that shows it. `farmed` is false when
// there was nothing to farm (no map, weapon or mobs), such as a player who
// logged off in town. That is not an error.
struct OfflineReport {
  bool farmed = false;
  // How long the game was closed, and how much of that was farmed. They differ
  // only if the character died partway through.
  double absence = 0.0;
  double seconds = 0.0;
  int64_t kills = 0;
  int start_level = 0;
  int end_level = 0;
  RewardTally rewards;
  // Whether the player ran out of HP. They return on Maple Island, and nothing
  // was farmed after they died.
  bool died = false;
  std::string map_name;
};

// Seconds between the save's timestamp and now. Zero for saves from before the
// timestamp existed, or if the clock went backwards.
double AbsenceSeconds(int64_t last_seen_unix_seconds, int64_t now_unix_seconds);

// Farms `state`'s map for `seconds` of absence and pays for it.
//
// An absence shorter than the sample is simulated in full. Longer ones scale
// the sample's kills to the remaining time. The player is paid only up to their
// death, and sent to Maple Island, if the sample dies, drains HP fast enough to
// run out before they return, or drops within a tenth of empty.
OfflineReport ApplyOfflineProgress(GameState& state, double seconds);

}  // namespace ms

#endif  // MS_SRC_COMBAT_OFFLINE_H_
