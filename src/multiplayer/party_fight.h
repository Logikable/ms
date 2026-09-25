/* The party's shared fight, from the point of view of the run fighting it.
 *
 * The server keeps the roster everyone is hitting and reports its remaining HP,
 * the current phase and the time left. This converts that into the SharedFight
 * a BossRun follows, and converts the run's damage back into a message. Nothing
 * here makes decisions about the fight.
 *
 * The frontend owns one of these, feeds the connection's messages into it every
 * tick, and opens the fight screen when it reports that a fight has started.
 */
#ifndef MS_SRC_MULTIPLAYER_PARTY_FIGHT_H_
#define MS_SRC_MULTIPLAYER_PARTY_FIGHT_H_

#include <string>
#include <vector>

#include "src/combat/fight_authority.h"
#include "src/multiplayer/client.h"

namespace ms {

class PartyFightAuthority : public FightAuthority {
 public:
  // `client` is the connection, owned by the caller and outliving this.
  explicit PartyFightAuthority(MultiplayerClient& client);

  // Reads everything the server has sent since the last call. Called every tick
  // before the run steps, so a run sees one consistent fight state.
  void Advance(const std::string& account_id);

  // Whether a fight is in progress. True from the first message about one until
  // Forget().
  bool fighting() const {
    return fighting_;
  }
  // Which fight, for the screen that builds a run from the same catalog entry.
  const std::string& boss_key() const {
    return boss_key_;
  }
  int difficulty_index() const {
    return difficulty_index_;
  }
  // Whether the fight was started as practice. Taken from the server, not this
  // client's own setting, so toggling it mid-fight can't change this client's
  // rewards.
  bool practice() const {
    return practice_;
  }
  // Takes this player out of the fight. Nothing more about it is processed,
  // however late its last messages arrive, and the party's next fight is told
  // apart by its name.
  void Leave();
  // Resets to no fight, for a screen that has finished with one.
  void Forget();

  void Report(const FightReport& report) override;
  bool Fetch(SharedFight& fight) override;

 private:
  void TakeState(const FightState& state, const std::string& account_id);
  void TakeEnd(const FightEnded& ended);

  MultiplayerClient* client_ = nullptr;
  SharedFight fight_;
  bool fighting_ = false;
  // False until the server has sent something about this fight; a run waits for
  // that before starting its countdown.
  bool told_ = false;
  std::string boss_key_;
  int difficulty_index_ = 0;
  bool practice_ = false;
  std::string fight_id_;
  // The fight this player left, whose last messages may still be arriving.
  std::string left_;
};

}  // namespace ms

#endif  // MS_SRC_MULTIPLAYER_PARTY_FIGHT_H_
