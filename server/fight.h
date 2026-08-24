/* One party's fight, as the server keeps it.
 *
 * The clients do the fighting. Each of them steps its own CombatSim against
 * its own copy of the roster and reports what it landed, exactly as it does
 * alone; this holds the roster they all share, so one boss dies once however
 * many people are hitting it. It owns the phase, the clock and who stands
 * where, and every client takes those from here rather than from its own
 * count.
 *
 * Nothing here knows about sockets, and nothing here knows about the combat
 * engine: a fight is a pool of HP, a clock, and the beats between phases.
 */
#ifndef MS_SERVER_FIGHT_H_
#define MS_SERVER_FIGHT_H_

#include <map>
#include <string>
#include <vector>

#include "src/protos/boss.pb.h"
#include "src/protos/mob.pb.h"
#include "src/protos/multiplayer.pb.h"

namespace ms {

// Where a fight is up to. The three at the end are all ways of being over,
// held apart because each pays a different thing: a clear pays everyone still
// there, and the other two pay nobody.
enum class PartyFightState {
  kCountdown,
  kFighting,
  kPhaseGap,
  kWon,
  kTimedOut,
  // Everybody's client has gone. No clear, no reward, and nobody's entry is
  // spent -- see the disconnect rules in //server:server.
  kAbandoned,
};

// One player in the fight.
struct FightPlayer {
  std::string account_id;
  std::string name;
  // Which of the phase's player spots they stand on. -1 in a phase that named
  // none, whose players all stand at the origin.
  int spot = 0;
  // False once their client has gone. They stop dealing damage and are paid
  // nothing, and the fight goes on without them.
  bool present = true;
};

class PartyFight {
 public:
  // `boss` and `mobs` are the catalogs, owned by the caller and outliving the
  // fight. `difficulty_index` is an index into the boss's difficulties; an
  // invalid one makes a fight that is over before it starts.
  PartyFight(std::string boss_key, const Boss& boss, int difficulty_index,
             const std::map<std::string, Mob>& mobs, const Party& party);

  // Steps the countdown, the clock and the beats between phases by
  // elapsed_seconds of real time. Does nothing once the fight is over.
  void Advance(double elapsed_seconds);

  // Takes `damage` off the monster standing in `slot`. Damage from a player
  // who has gone, for a slot the phase does not hold, or landed while nothing
  // is being fought, is dropped.
  void Hit(const std::string& account_id, int slot, double damage);

  // Stands `account_id` on `spot`. Refused, and nothing moves, when the spot
  // is not one of this phase's or somebody else is already on it -- the
  // client walks first and is told here when it walked somewhere taken.
  bool MoveTo(const std::string& account_id, int spot);

  // Marks a player's client as gone. The fight is abandoned once the last one
  // has: nobody is left watching, and a fight nobody watches pays nothing.
  void Disconnect(const std::string& account_id);

  PartyFightState state() const {
    return state_;
  }
  // True once the fight is over and its closing beat has been held: the
  // moment the server can let go of it.
  bool done() const;
  // True for a fight that is over, whether or not the beat is up.
  bool over() const;
  // Which phase is being fought, counting from 0.
  int phase() const {
    return phase_;
  }
  // What every monster of the phase has left, as a fraction of what it
  // started with, one per slot in the order the phase spawns them. This is
  // what a client names when it reports damage.
  const std::vector<double>& hp_fractions() const {
    return hp_fractions_;
  }
  const std::vector<FightPlayer>& players() const {
    return players_;
  }
  // How many players the fight started with, which is what the drops and the
  // meso are split by. A player who leaves does not make the rest richer.
  int share_count() const {
    return share_count_;
  }
  double seconds_left() const {
    return seconds_left_;
  }
  double countdown_left() const {
    return countdown_left_;
  }
  const std::string& boss_key() const {
    return boss_key_;
  }
  int difficulty_index() const {
    return difficulty_index_;
  }

 private:
  const BossDifficulty* difficulty() const;
  // The phase being fought, or null once the fight is over.
  const BossPhase* current_phase() const;
  // The player playing under `account_id`, or null.
  FightPlayer* Find(const std::string& account_id);
  // Fills the roster with what the phase spawns and stands everyone on a spot
  // of their own. Every phase is its own arena, so where a player walked to in
  // the last one means nothing here.
  void EnterPhase(int phase);
  // Ends the fight in `outcome`, holding it for the closing beat.
  void Finish(PartyFightState outcome);
  // Runs the clock and, when the roster empties, moves the fight on.
  void RunPhase(double dt);
  // Whether anything is still standing.
  bool AnyoneAlive() const;

  std::string boss_key_;
  const Boss* boss_ = nullptr;
  int difficulty_index_ = 0;
  const std::map<std::string, Mob>* mobs_ = nullptr;
  int phases_ = 0;
  int share_count_ = 0;

  PartyFightState state_ = PartyFightState::kCountdown;
  int phase_ = 0;
  double countdown_left_ = 0.0;
  double seconds_left_ = 0.0;
  // Seconds left of whatever beat is being held: the gap between phases, or
  // the pause at the end before the fight is let go of.
  double hold_left_ = 0.0;
  std::vector<double> hp_;
  std::vector<double> max_hp_;
  std::vector<double> hp_fractions_;
  std::vector<FightPlayer> players_;
};

}  // namespace ms

#endif  // MS_SERVER_FIGHT_H_
