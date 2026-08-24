/* Somebody else deciding how a fight goes.
 *
 * A boss fought alone decides everything itself: what the monsters have left,
 * which phase is up, and what is on the clock. A boss fought with a party
 * takes all three from the server, which keeps the one roster everybody is
 * hitting. This is what that looks like from the run's side -- it reports what
 * it landed and reads back the fight as the authority has it.
 *
 * The run still swings for itself either way. Only the roster is shared, so a
 * player's own charge bar never waits on the network.
 */
#ifndef MS_SRC_COMBAT_FIGHT_AUTHORITY_H_
#define MS_SRC_COMBAT_FIGHT_AUTHORITY_H_

#include <cstdint>
#include <string>
#include <vector>

#include "src/combat/fight.h"

namespace ms {

// Where a run is up to. The three at the end are all ways of being finished,
// held apart because the screen says a different thing about each.
enum class BossRunState {
  kCountdown,
  kFighting,
  kPhaseGap,
  kWon,
  kTimedOut,
  kAborted,
};

// One line somebody else landed, as this run should draw it.
struct SharedLine {
  // Which player of the shared fight landed it, as an index into its players.
  int owner = 0;
  // Which monster of the phase it fell on, counted in the order the phase
  // spawns them. A monster's id is handed out per client and means nothing
  // across one.
  int slot = 0;
  // Shared by every line one attack put on one monster.
  int event = 0;
  DamageSource source;
  int64_t damage = 0;
  bool crit = false;
};

// One player of a shared fight.
struct SharedPlayer {
  std::string account_id;
  std::string name;
  // Which of the phase's player spots they stand on.
  int spot = -1;
  bool present = true;
  std::string attack_name;
  double attack_fraction = 0.0;
};

// The fight as its authority has it.
struct SharedFight {
  BossRunState state = BossRunState::kCountdown;
  int phase = 0;
  double seconds_left = 0.0;
  double countdown_left = 0.0;
  // What every monster of the phase has left, one per slot.
  std::vector<double> hp_fractions;
  std::vector<SharedPlayer> players;
  // Which of `players` is at this screen. -1 before the authority has said.
  int self = -1;
  // How many were in the fight when it began, which is what a clear is split
  // by. 0 until it is over.
  int share_count = 0;
  // What everybody else has landed since the last read. The player's own
  // lines are not in here: they drew those as they landed them.
  std::vector<SharedLine> lines;
};

class FightAuthority {
 public:
  virtual ~FightAuthority() = default;

  // What this run has landed since the last report, where its player is
  // standing, and what they are winding up. `phase` is the phase the lines
  // landed in, and their `owner` means nothing here -- they are all this
  // player's.
  virtual void Report(int phase, const std::vector<SharedLine>& lines, int spot,
                      const std::string& attack_name,
                      double attack_fraction) = 0;

  // The fight as it stands. False while nothing has arrived, which is where
  // a run waits rather than deciding anything for itself.
  virtual bool Fetch(SharedFight& fight) = 0;
};

}  // namespace ms

#endif  // MS_SRC_COMBAT_FIGHT_AUTHORITY_H_
