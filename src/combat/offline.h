/* Paying the player for the time the game was closed.
 *
 * The rate is measured, never modelled: a cold CombatSim is stepped through a
 * sample of the absence on the map the player logged off from, and the kills
 * it reports are scaled up to the whole of it. There is one combat engine, so
 * what a returning player is paid cannot drift from what they would have
 * watched.
 *
 * Two things are frozen for the whole absence: the character's level and what
 * they had spent. A player who climbs ten levels offline farms all of it at
 * the rate they left at -- the AP, the SP and the level itself only arrive
 * when they come back and see it. Nothing quietly plays the character better
 * than they left them.
 *
 * The one approximation is death. A sample cannot prove a map is survivable
 * forever, so a trend is fitted to the pool across it: one draining faster
 * than it swings projects the moment it runs out, and the player is credited
 * to there and sent home. See ApplyOfflineProgress.
 */
#ifndef MS_SRC_COMBAT_OFFLINE_H_
#define MS_SRC_COMBAT_OFFLINE_H_

#include <cstdint>
#include <string>

#include "src/combat/combat.h"
#include "src/game_state.h"

namespace ms {

// The stretch of fighting actually stepped before the rest of an absence is
// scaled from it. Long enough to hold dozens of respawn beats even at the
// slowest pacing band, so a kill pattern that repeats over several beats is
// inside it many times over -- one beat is not a sample, it is the best beat
// the character ever has.
constexpr double kOfflineSampleSeconds = 600.0;

// How finely that sample is stepped: the live tick, so the fight meets the
// same step sizes offline that it does in front of the player.
constexpr double kOfflineStepSeconds = 0.1;

// What an absence paid, for the pop-up that shows it. `farmed` is false when
// there was nothing to farm -- no map, no weapon, no mobs -- which is not a
// failure, just a player who left standing in town.
struct OfflineReport {
  bool farmed = false;
  // How long the game was closed, and how much of that was actually farmed.
  // The two differ only when the character fell partway through: what they
  // were paid stops there, but they were still away the whole time.
  double absence = 0.0;
  double seconds = 0.0;
  int64_t kills = 0;
  int start_level = 0;
  int end_level = 0;
  RewardTally rewards;
  // Whether the map ran the player out of HP. They are on Maple Island when
  // they come back, and nothing was farmed after the moment they fell.
  bool died = false;
  std::string map_name;
};

// Seconds a player was away, from a save's stamp to now. Zero for a save
// written before the stamp existed, and for a clock that has gone backwards --
// neither is an absence anyone should be paid for.
double AbsenceSeconds(int64_t last_seen_unix_seconds, int64_t now_unix_seconds);

// Farms `state`'s current map for `seconds` of absence and pays the character
// for it, returning what they earned.
//
// The absence is stepped in full when it is shorter than the sample; past
// that, the sample's kills are scaled to what is left. Three samples are
// credited only up to the fall and leave the player on Maple Island: one that
// dies, one whose pool is draining fast enough to run out before the player
// returns, and one that held but came within a twentieth of empty doing it.
OfflineReport ApplyOfflineProgress(GameState& state, double seconds);

}  // namespace ms

#endif  // MS_SRC_COMBAT_OFFLINE_H_
