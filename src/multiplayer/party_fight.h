/* The party's shared fight, as the run fighting it sees one.
 *
 * The server keeps the roster everybody is hitting and says what is left of
 * it, which phase is up and what is on the clock. This turns that into the
 * SharedFight a BossRun follows, and turns what the run landed back into a
 * message. Nothing here decides anything about the fight.
 *
 * The frontend owns one of these, drains the connection into it every tick,
 * and opens the fight screen when it says a fight has begun.
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

  // Takes whatever the server has said since the last call. Called every tick,
  // before the run steps, so a run reads one fight rather than half of two.
  void Advance(const std::string& account_id);

  // Whether a fight is on. True from the first word of one until Forget().
  bool fighting() const {
    return fighting_;
  }
  // Which fight, for the screen that has to build a run against the same
  // catalog entry.
  const std::string& boss_key() const {
    return boss_key_;
  }
  int difficulty_index() const {
    return difficulty_index_;
  }
  // Puts it back to no fight at all, for a screen that has finished with one.
  void Forget();

  void Report(int phase, const std::vector<SharedLine>& lines, int spot,
              const std::string& attack_name, double attack_fraction) override;
  bool Fetch(SharedFight& fight) override;

 private:
  void TakeState(const FightState& state, const std::string& account_id);
  void TakeEnd(const FightEnded& ended);

  MultiplayerClient* client_ = nullptr;
  SharedFight fight_;
  bool fighting_ = false;
  // False until the server has said something about this fight, which is what
  // a run waits for rather than counting itself in.
  bool told_ = false;
  std::string boss_key_;
  int difficulty_index_ = 0;
};

}  // namespace ms

#endif  // MS_SRC_MULTIPLAYER_PARTY_FIGHT_H_
