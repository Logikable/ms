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
  // There is no connection and one is being tried for: the server could not
  // be reached, went away, or turned this client away for a reason that may
  // pass -- a version that does not match, an account already being played.
  kUnavailable,
  // The server turned this connection away for good: it could not read what
  // this build sent. Nothing is retried, and the thread ends.
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
  // The last thing the server had to say to this player alone: an action it
  // would not take, or something that happened to their place in a party. The
  // serial climbs with each one, which is how a screen tells a new notice
  // from the one it has already shown.
  std::string notice;
  int64_t notice_serial = 0;
  // Whether that notice is a refusal, which is drawn as one.
  bool notice_is_refusal = false;
  // The version the server is built from, which it sends only when turning
  // this build away. 0 until it has said, which is every other state.
  int server_protocol_version = 0;
};

// How one pass over the connection ended, which is what the backoff reads.
enum class Attempt {
  // Greeted and served, then lost.
  kWelcomed,
  // Never got in.
  kFailed,
  // Turned away for a reason a later attempt may find changed.
  kRejected,
  // Turned away for good.
  kFinal,
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
  // Wakes a connection that is waiting to retry, so the next attempt happens
  // now rather than at the end of the backoff. Nothing if a connection is up,
  // or if the server turned this build away for good.
  void Reconnect();

  // The character as the lobby should see them: sent now if there is a
  // connection, and again with the next Hello.
  void SetPlayer(const PlayerInfo& player);

  MultiplayerSnapshot Snapshot() const;

  // Asks, queued for the connection thread to send. Each of them is answered
  // by the state in a later snapshot, or by a notice in one.
  void CreateParty();
  void JoinParty(const std::string& party_id);
  void LeaveParty();
  void SetReady(bool ready);
  void Kick(const std::string& account_id);
  void Promote(const std::string& account_id);
  void StartFight(const std::string& boss_key, int difficulty_index,
                  PartyMode mode);
  // What this client's fight has landed, where its player is standing, and
  // what they are winding up. Sent every step of a fight rather than queued
  // as an ask: a report that waited would land on a roster that had moved on.
  void SendFightUpdate(const FightUpdate& update);
  // Walks out of the fight in progress.
  void LeaveFight();

  // Every fight message the server has sent since the last call, in order.
  // Taken rather than read off the snapshot because the lines in them are
  // drawn once and would be lost by a frame that saw two.
  std::vector<ServerMessage> TakeFightMessages();

 private:
  // The thread's whole life: connect, talk, reconnect.
  void Run();
  // One connection, from the Hello to whatever ends it.
  Attempt RunConnection();
  // Waits out the backoff in slices, so that Stop() and Reconnect() are
  // answered promptly.
  void WaitToRetry(std::chrono::seconds wait);
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
  // Puts the lobby back to how a fresh connection starts, this attempt's
  // outcome included. What the player has asked for is left alone: an ask made
  // before the connection landed is one they still want.
  void ForgetLobby();

  std::string host_;
  int port_ = 0;
  int protocol_version_ = 0;
  std::atomic<bool> running_{false};
  // Set by Reconnect() and cleared by the wait it cuts short.
  std::atomic<bool> retry_now_{false};
  std::thread thread_;

  mutable std::mutex mutex_;
  MultiplayerSnapshot snapshot_;
  // How the connection in progress has ended so far, which is what the backoff
  // reads once it is over.
  Attempt outcome_ = Attempt::kFailed;
  PlayerInfo player_;
  std::vector<ClientMessage> queued_;
  // What the server has said about the fight and nobody has read yet.
  std::vector<ServerMessage> fight_;
};

}  // namespace ms

#endif  // MS_SRC_MULTIPLAYER_CLIENT_H_
