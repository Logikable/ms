/* Connects the network connection to the game.
 *
 * The client knows nothing about the GameState, and the GameState knows nothing
 * about the connection. This is where the two meet: it introduces the character
 * being played, keeps that introduction current as they level, and writes the
 * account the server issues back where the save will keep it.
 *
 * The frontend owns one of these and calls Advance each tick. Every request the
 * screens make of the connection goes through client().
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

// How often an update with only new EXP is sent. Any other change to the sheet
// is sent immediately; EXP changes with every kill, and the EXP bar on someone
// else's Inspect screen doesn't need updating faster than this.
inline constexpr std::chrono::seconds kExpUpdatePeriod{1};

// The character as others may see them: stats, equipment, the passives behind
// both, and unspent points, with the bag, purse, buy-back shelf and honor
// cleared.
Character PublicSheet(const CharacterInstance& character);

// The character in `state` as the lobby should see them, including their sheet,
// under the save's account.
PlayerInfo PlayerFor(const GameState& state);

class MultiplayerSession {
 public:
  MultiplayerSession(std::string host, int port);

  // Opens the connection. Does nothing if the session is already started.
  void Start(GameState& state);
  // Closes the connection. The destructor also does this, through the client.
  void Stop();
  bool started() const {
    return started_;
  }

  // Keeps both sides in sync: the server's id for this player goes into the
  // account, and a character who has levelled or been renamed is introduced
  // again. Cheap enough for every tick, since it sends nothing when nothing
  // changed.
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
  // What the lobby was last sent, so a tick with no changes sends nothing.
  PlayerInfo told_;
  // When that was sent, for the EXP rate limit. The epoch until the first send,
  // which is long before kExpUpdatePeriod, so the first tick is never delayed.
  std::chrono::steady_clock::time_point sent_;
};

}  // namespace ms

#endif  // MS_SRC_MULTIPLAYER_SESSION_H_
