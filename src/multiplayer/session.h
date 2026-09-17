/* What ties the connection to the game.
 *
 * The client knows nothing about the GameState and the GameState knows nothing
 * about the connection. This is the one place that knows both: it introduces
 * the character being played, keeps that introduction current as they level,
 * and writes the account the server issues back where the save will keep it.
 *
 * The frontend owns one of these and calls Advance from its tick. Everything
 * the screens ask of the connection goes through client().
 */
#ifndef MS_SRC_MULTIPLAYER_SESSION_H_
#define MS_SRC_MULTIPLAYER_SESSION_H_

#include <chrono>
#include <string>

#include "src/character/character.h"
#include "src/game_state.h"
#include "src/multiplayer/client.h"
#include "src/protos/multiplayer.pb.h"

namespace ms {

// How often an update carrying nothing but fresh EXP goes out. Everything
// else on the sheet is sent the moment it moves; EXP moves with every kill,
// and the exp bar on somebody else's Inspect screen is worth no more than
// this.
inline constexpr std::chrono::seconds kExpUpdatePeriod{1};

// The character as everyone else may see them: their stats, what they are
// wearing, the passives behind both and the points still to spend, with the
// bag, the purse, the buy-back shelf and their honor cleared.
Character PublicSheet(const CharacterInstance& character);

// The character in `state` as the lobby should see them, sheet included, under
// the account the save is carrying.
PlayerInfo PlayerFor(const GameState& state);

class MultiplayerSession {
 public:
  MultiplayerSession(std::string host, int port);

  // Opens the connection. Does nothing on a session already started.
  void Start(GameState& state);
  // Closes it. The destructor does this too, through the client.
  void Stop();
  bool started() const {
    return started_;
  }

  // Keeps the two ends in step: what the server calls this player goes into
  // the account, and a character who has levelled or been renamed is
  // introduced again. Cheap enough for every tick -- it sends nothing when
  // nothing has changed.
  void Advance(GameState& state);

  MultiplayerSnapshot Snapshot() const {
    return client_.Snapshot();
  }
  MultiplayerClient& client() {
    return client_;
  }

 private:
  MultiplayerClient client_;
  bool started_ = false;
  // What the lobby was last told, so that a tick changing nothing sends
  // nothing.
  PlayerInfo told_;
  // When that went out, for the EXP floor. Epoch until the first send, which
  // is well past kExpUpdatePeriod ago -- the first tick is never held back.
  std::chrono::steady_clock::time_point sent_;
};

}  // namespace ms

#endif  // MS_SRC_MULTIPLAYER_SESSION_H_
