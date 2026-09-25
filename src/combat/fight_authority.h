/* Interface for letting someone else (the server) control a fight.
 *
 * A solo boss fight tracks everything itself: monster HP, the current phase,
 * and the timer. A party fight takes all three from the server, which keeps the
 * one set of monsters everyone is hitting. From the run's side, it reports the
 * damage it dealt and reads back the fight's state.
 *
 * Each run still simulates its own attacks. Only the monsters are shared, so a
 * player's charge bar never waits on the network.
 */
#ifndef MS_SRC_COMBAT_FIGHT_AUTHORITY_H_
#define MS_SRC_COMBAT_FIGHT_AUTHORITY_H_

#include <cstdint>
#include <string>
#include <vector>

#include "src/combat/damage_breakdown.h"
#include "src/combat/fight.h"
#include "src/protos/mob.pb.h"

namespace ms {

// Where a run is. The last three are all finished states, kept separate because
// the screen shows something different for each.
enum class BossRunState {
  kCountdown,
  kFighting,
  kPhaseGap,
  kWon,
  kTimedOut,
  kAborted,
};

// A damage line another player dealt, for this run to draw.
struct SharedLine {
  // Which player dealt it, as an index into the fight's players.
  int owner = 0;
  // Which monster it hit, by spawn order in the phase. Mob IDs are per client,
  // so they can't be shared.
  int slot = 0;
  // Shared by every line one attack put on one monster.
  int event = 0;
  // Which hit of that event it belongs to; the display flashes through them.
  // See DamageLine::strike.
  int strike = 0;
  DamageSource source;
  int64_t damage = 0;
  bool crit = false;
};

// One player in a shared fight.
struct SharedPlayer {
  std::string account_id;
  std::string name;
  // Which of the phase's player spots they stand on.
  int spot = -1;
  bool present = true;
  std::string attack_name;
  double attack_fraction = 0.0;
  int buff_count = 0;
};

// One item a clear paid this player. `drop` names the item; it has already been
// rolled, so its `per_kill` doesn't matter here.
struct SharedAward {
  MobDrop drop;
  int64_t count = 0;
};

// The fight's state according to the server.
struct SharedFight {
  BossRunState state = BossRunState::kCountdown;
  int phase = 0;
  double seconds_left = 0.0;
  double countdown_left = 0.0;
  // Each monster's remaining HP fraction, one per slot.
  std::vector<double> hp_fractions;
  std::vector<SharedPlayer> players;
  // Which entry in `players` is the local player. -1 until the server says.
  int self = -1;
  // How many players were in the fight when it started; a clear's reward is
  // split this many ways. 0 until the fight ends.
  int share_count = 0;
  // Damage other players dealt since the last fetch. The local player's own
  // lines aren't included; they were drawn as they landed.
  std::vector<SharedLine> lines;
  // What a clear paid this player. The server rolls the drops, so a guaranteed
  // drop is guaranteed and a single drop goes to exactly one player.
  std::vector<SharedAward> awards;
  // Every player's damage by skill, in `players` order. Empty until the fight
  // ends.
  std::vector<PlayerBreakdown> breakdowns;
};

// What one run reports: damage dealt since the last report, where its player
// stands, and the attack they are charging.
struct FightReport {
  // The phase the lines landed in.
  int phase = 0;
  // `owner` is ignored here; every line is this player's.
  std::vector<SharedLine> lines;
  int spot = 0;
  std::string attack_name;
  double attack_fraction = 0.0;
  // This player's item drop rate, used when rolling the clear's drops.
  double item_drop_pct = 0.0;
  int buff_count = 0;
  // This player's total damage this fight, by skill.
  std::vector<BreakdownRow> breakdown;
};

class FightAuthority {
 public:
  virtual ~FightAuthority() = default;

  // Sends the damage dealt since the last report and the player's current
  // state.
  virtual void Report(const FightReport& report) = 0;

  // Gets the fight's current state. Returns false if nothing has arrived yet;
  // the run then waits rather than deciding anything itself.
  virtual bool Fetch(SharedFight& fight) = 0;
};

}  // namespace ms

#endif  // MS_SRC_COMBAT_FIGHT_AUTHORITY_H_
