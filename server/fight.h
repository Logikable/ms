/* The server's copy of one party's boss fight.
 *
 * Each client runs its own CombatSim, as in solo play, and reports the damage
 * it deals. This class holds the shared mob HP, so a boss dies once no matter
 * how many players hit it. It also owns the phase, the timer and player
 * positions, and clients follow its values rather than their own.
 *
 * It knows nothing about sockets or the combat engine. To the server, a fight
 * is HP pools, a timer, and pauses between phases.
 */
#ifndef MS_SERVER_FIGHT_H_
#define MS_SERVER_FIGHT_H_

#include <map>
#include <random>
#include <string>
#include <vector>

#include "src/protos/boss.pb.h"
#include "src/protos/mob.pb.h"
#include "src/protos/multiplayer.pb.h"

namespace ms {

// The stage a fight is in. The last three are all end states. Only a win pays
// out, to everyone still present.
enum class PartyFightState {
  kCountdown,
  kFighting,
  kPhaseGap,
  kWon,
  kTimedOut,
  // Every client disconnected. There is no clear or reward, and no entry is
  // used up.
  kAbandoned,
};

// One player in the fight.
struct FightPlayer {
  std::string account_id;
  std::string name;
  // Index into the phase's player spots, or -1 if the phase has none and
  // everyone stands at the origin.
  int spot = 0;
  // False once their client disconnects. They deal no more damage and get no
  // reward, and the fight goes on without them.
  bool present = true;
  // The attack they are charging, for the progress bar other players see.
  std::string attack_name;
  double attack_fraction = 0.0;
  int buff_count = 0;
  // Their Item Drop Rate as a fraction. Drops roll against the party's best.
  double item_drop_pct = 0.0;
  // Their drops. Empty until the fight is won.
  std::vector<FightAward> awards;
  // Damage they dealt since the last broadcast. The server relays these to
  // the other players and then clears them.
  std::vector<FightDamage> lines;
  // Their damage by skill, as last reported. Kept after they leave so the
  // final table still lists them.
  std::vector<FightBreakdownRow> breakdown;
};

class PartyFight {
 public:
  // The caller owns `boss` and `mobs`, and they must outlive the fight. An
  // invalid `difficulty_index` makes a fight that is already over.
  PartyFight(std::string id, std::string boss_key, const Boss& boss,
             int difficulty_index, const std::map<std::string, Mob>& mobs,
             const Party& party, const BossOptions& options = BossOptions());

  // Unique per fight, so a client that left one can tell it apart from the
  // party's next fight.
  const std::string& id() const {
    return id_;
  }

  // Advances the countdown, timer and phase gaps by `elapsed_seconds` of real
  // time. Does nothing once done().
  void Advance(double elapsed_seconds);

  // Applies one client's report. Its damage comes off the mobs and is queued
  // for other players to see, and its spot and current attack are recorded.
  // Damage from an earlier phase is dropped, but the spot is still taken.
  void Report(const std::string& account_id, const FightUpdate& update);

  // Clears every player's damage lines after a broadcast sends them.
  void TakeLines();

  // Deals `damage` to the mob in `slot`. Ignored if the player has left, the
  // slot is out of range, or the fight is not in progress.
  void Hit(const std::string& account_id, int slot, double damage);

  // Moves `account_id` to `spot`. Returns false and moves nobody if the spot
  // is not in this phase or someone else is on it. The client moves first,
  // and this tells it when the spot was taken.
  bool MoveTo(const std::string& account_id, int spot);

  // Marks a player's client as disconnected. The fight is abandoned once
  // every player has disconnected.
  void Disconnect(const std::string& account_id);

  PartyFightState state() const {
    return state_;
  }
  // True once the fight is over and its end pause has passed, so the server
  // can discard it.
  bool done() const;
  // True once the fight is over, even during the end pause.
  bool over() const;
  // The current phase, counting from 0.
  int phase() const {
    return phase_;
  }
  // Each mob's remaining HP as a fraction, one per slot in spawn order.
  // Clients use these slot numbers when reporting damage.
  const std::vector<double>& hp_fractions() const {
    return hp_fractions_;
  }
  const std::vector<FightPlayer>& players() const {
    return players_;
  }
  // How many players the fight started with. Drops and meso are split this
  // many ways, so a player leaving does not raise the others' share.
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
  // The options the fight started with. Sent with every state so clients do
  // not rely on their own settings.
  const BossOptions& options() const {
    return options_;
  }
  int difficulty_index() const {
    return difficulty_index_;
  }

 private:
  const BossDifficulty* difficulty() const;
  // Returns the current phase, or null if there is none.
  const BossPhase* current_phase() const;
  // Returns the player with `account_id`, or null.
  FightPlayer* Find(const std::string& account_id);
  // Spawns the phase's mobs and resets every player to their own spot. Each
  // phase is a new arena, so positions from the last one do not carry over.
  void EnterPhase(int phase);
  // Ends the fight with `outcome` and starts the end pause. A win also deals
  // drops.
  void Finish(PartyFightState outcome);
  // Rolls each drop once for the whole party and gives it to a random player
  // still present. One shared roll keeps a guaranteed drop guaranteed and a
  // one-off drop from landing twice.
  void DealDrops();
  // Runs the timer, and moves to the next phase or ends the fight once every
  // mob is dead.
  void RunPhase(double dt);
  // Returns whether any mob has HP left.
  bool AnyoneAlive() const;

  std::string id_;
  std::string boss_key_;
  const Boss* boss_ = nullptr;
  int difficulty_index_ = 0;
  const std::map<std::string, Mob>* mobs_ = nullptr;
  BossOptions options_;
  int phases_ = 0;
  int share_count_ = 0;

  PartyFightState state_ = PartyFightState::kCountdown;
  int phase_ = 0;
  double countdown_left_ = 0.0;
  double seconds_left_ = 0.0;
  // Seconds left in the current pause: the gap between phases, or the pause
  // at the end before the fight is discarded.
  double hold_left_ = 0.0;
  std::vector<double> hp_;
  std::vector<double> max_hp_;
  std::mt19937 rng_{std::random_device{}()};
  std::vector<double> hp_fractions_;
  std::vector<FightPlayer> players_;
};

}  // namespace ms

#endif  // MS_SERVER_FIGHT_H_
