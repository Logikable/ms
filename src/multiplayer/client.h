/* The game's end of the multiplayer connection.
 *
 * A background thread owns the socket. It connects, introduces the character,
 * keeps the heartbeat going, and reconnects on its own when the server goes
 * away -- so the game's loop never waits on the network. The screens read a
 * snapshot and push asks into a queue, and both cross the thread boundary
 * under one lock.
 *
 * Nothing here touches the GameState. What the connection learns -- the
 * account the server issued, the party the player is in -- comes out through
 * the snapshot, and the frontend decides what to do with it.
 */
#ifndef MS_SRC_MULTIPLAYER_CLIENT_H_
#define MS_SRC_MULTIPLAYER_CLIENT_H_

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "src/multiplayer/protocol.h"
#include "src/net/socket.h"
#include "src/protos/multiplayer.pb.h"

namespace ms {

// How long to wait before the first reconnection, and the longest it backs
// off to. A server being updated is back inside a minute, so the ceiling is
// what a player waits at worst.
inline constexpr std::chrono::seconds kFirstRetry(1);
inline constexpr std::chrono::seconds kLongestRetry(30);

// Where the connection is up to.
enum class ConnectionState {
  // Nobody has asked for a connection, or one has been stopped.
  kOffline,
  kConnecting,
  // Greeted, in the lobby, and listening.
  kConnected,
  // The server could not be reached or went away. Trying again.
  kUnavailable,
  // The server turned this build or this account away for good. Nothing is
  // retried; the message says what the player has to do.
  kRefused,
};

// Everything the screens draw, taken in one piece so that a frame cannot show
// half of one state and half of another.
struct MultiplayerSnapshot {
  ConnectionState state = ConnectionState::kOffline;
  // Why there is no connection, fit to show the player as it stands. Empty
  // while there is nothing wrong.
  std::string message;
  // What the server calls this player. Empty until it has said.
  std::string account_id;
  std::string token;
  // Every party open to be joined.
  PartyList parties;
  // The party this player is in. No id means they are in none.
  Party party;
  // The last thing the server would not do, and a number that climbs with
  // each one -- which is how a screen tells a new refusal from the one it has
  // already shown.
  std::string notice;
  int64_t notice_serial = 0;
};

class MultiplayerClient {
 public:
  // `protocol_version` is what the client introduces itself as. It is an
  // argument only so that a test can be an old build.
  MultiplayerClient(std::string host, int port,
                    int protocol_version = kMultiplayerVersion);
  ~MultiplayerClient();
  MultiplayerClient(const MultiplayerClient&) = delete;
  MultiplayerClient& operator=(const MultiplayerClient&) = delete;

  // Starts the thread and connects. `player` is the character to introduce,
  // carrying the account id and token from the save when there is one.
  void Start(const PlayerInfo& player, const std::string& token);
  // Stops the thread and closes the connection. Called by the destructor.
  void Stop();

  // The character as the lobby should see them. Sent on the next connection
  // rather than immediately: what a party is shown is who joined it.
  void SetPlayer(const PlayerInfo& player);

  MultiplayerSnapshot Snapshot() const;

  // Asks, queued for the connection thread to send. Each of them is answered
  // by the state in a later snapshot, or by a notice in one.
  void CreateParty(const std::string& boss_key, int difficulty_index,
                   PartyMode mode);
  void JoinParty(const std::string& party_id);
  void LeaveParty();
  void StartFight();

 private:
  // The thread's whole life: connect, talk, reconnect.
  void Run();
  // One connection, from the Hello to whatever ends it. Returns false when
  // there is no point trying again.
  bool RunConnection();
  // Opens a socket and sends the Hello. Nothing means the server could not be
  // reached.
  bool Open(Socket& socket);
  // Reads what has arrived and acts on it. False when the connection is over.
  bool Pump(Socket& socket, std::string& incoming, std::string& outgoing,
            std::chrono::steady_clock::time_point& last_ping);
  void Handle(const ServerMessage& message, bool& keep);
  // Moves everything the screens have asked for into `outgoing`.
  void SendQueued(std::string& outgoing);
  // Queues `message` for the thread to send.
  void Ask(const ClientMessage& message);

  void SetState(ConnectionState state, const std::string& message);
  // Puts the lobby back to how a fresh connection starts. What the player has
  // asked for is left alone: an ask made before the connection landed is one
  // they still want.
  void ForgetLobby();

  std::string host_;
  int port_ = 0;
  int protocol_version_ = 0;
  std::atomic<bool> running_{false};
  std::thread thread_;

  mutable std::mutex mutex_;
  MultiplayerSnapshot snapshot_;
  PlayerInfo player_;
  std::vector<ClientMessage> queued_;
};

}  // namespace ms

#endif  // MS_SRC_MULTIPLAYER_CLIENT_H_
