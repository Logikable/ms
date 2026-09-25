/* The game's side of the multiplayer connection.
 *
 * A background thread owns the socket. It connects, introduces the character,
 * sends the heartbeat, and reconnects by itself when the server goes away, so
 * the game loop never waits on the network. Screens read a snapshot and push
 * requests into a queue; both cross the thread boundary under one lock.
 *
 * Nothing here touches the GameState. What the connection learns, such as the
 * account the server issued or the player's party, comes out through the
 * snapshot, and the frontend decides what to do with it.
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

// The wait before the first reconnection attempt, and the longest the backoff
// grows to. An updating server is back within a minute, so the maximum is the
// worst a player waits.
inline constexpr std::chrono::seconds kFirstRetry(1);
inline constexpr std::chrono::seconds kLongestRetry(30);

// The connection's current state.
enum class ConnectionState {
  // No connection has been requested, or it was stopped.
  kOffline,
  kConnecting,
  // Greeted, in the lobby, and listening.
  kConnected,
  // There's no connection and one is being attempted: the server couldn't be
  // reached, went away, or rejected this client for a reason that may pass,
  // such as a version mismatch or an account already in use.
  kUnavailable,
  // The server permanently rejected this connection because it couldn't read
  // what this build sent. Nothing is retried, and the thread ends.
  kRefused,
};

// Everything the screens draw, taken as one copy so a frame can't show half of
// one state and half of another.
struct MultiplayerSnapshot {
  ConnectionState state = ConnectionState::kOffline;
  // Why there's no connection, ready to show the player. Empty when nothing is
  // wrong.
  std::string message;
  // The server's id for this player. Empty until the server sends it.
  std::string account_id;
  std::string token;
  // Every party open to join.
  PartyList parties;
  // Everyone connected, with name and level only. The Players list draws these.
  OnlinePlayers online;
  // The player being viewed on the Inspect screen, including their sheet. Empty
  // until the server answers a watch request, and cleared whenever the watch
  // changes, so a screen never shows the previous player under this one's name.
  PlayerInfo watched;
  // The party this player is in. No id means none.
  Party party;
  // The trade this player is in. No id means none, which closes the trade
  // screen when the partner leaves.
  TradeState trade;
  // What the last completed trade gives this player, and a serial that
  // increases with each one so the screen can tell a new payment from one it
  // already applied. Their own offer has already left them, so the screen
  // removes it and adds this.
  TradeOffer trade_received;
  int64_t trade_serial = 0;
  // The latest message for the gold notification box, one string per line, and
  // a serial that increases with each one so a screen can tell a new message
  // from one it already showed.
  std::vector<std::string> notification;
  int64_t notification_serial = 0;
  // The last message the server sent to this player alone: an action it
  // refused, or a change to their place in a party. The serial increases with
  // each one, so a screen can tell a new notice from one it already showed.
  std::string notice;
  int64_t notice_serial = 0;
  // Whether that notice is a refusal, which is drawn differently.
  bool notice_is_refusal = false;
  // The server's protocol version, sent only when it rejects this build. 0
  // until then, which is every other state.
  int server_protocol_version = 0;
};

// How one connection attempt ended, which the backoff uses.
enum class Attempt {
  // Welcomed and served, then lost.
  kWelcomed,
  // Never connected.
  kFailed,
  // Rejected for a reason that may change by the next attempt.
  kRejected,
  // Rejected permanently.
  kFinal,
};

class MultiplayerClient {
 public:
  // `protocol_version` is what the client identifies as. It's a parameter only
  // so a test can act as an old build.
  MultiplayerClient(std::string host, int port,
                    int protocol_version = kMultiplayerVersion);
  ~MultiplayerClient();
  MultiplayerClient(const MultiplayerClient&) = delete;
  MultiplayerClient& operator=(const MultiplayerClient&) = delete;

  // Starts the thread and connects. `player` is the character to introduce,
  // with the account id and token from the save if there is one.
  void Start(const PlayerInfo& player, const std::string& token);
  // Stops the thread and closes the connection. Called by the destructor.
  void Stop();
  // Wakes a connection waiting to retry, so the next attempt happens now
  // instead of after the backoff. Does nothing if connected, or if the server
  // rejected this build permanently.
  void Reconnect();

  // The character as the lobby should see them: sent now if connected, and
  // again with the next Hello.
  void SetPlayer(const PlayerInfo& player);

  MultiplayerSnapshot Snapshot() const;

  // Requests, queued for the connection thread to send. Each is answered by
  // state in a later snapshot, or by a notice.
  void CreateParty();
  void JoinParty(const std::string& party_id);
  void LeaveParty();
  void SetReady(bool ready);
  void Kick(const std::string& account_id);
  void Promote(const std::string& account_id);
  // Asks `account_id` to trade. The player who was asked answers the same way:
  // the second request opens the trade for both.
  void RequestTrade(const std::string& account_id);
  // Sets this player's whole offer.
  void SetTradeOffer(const TradeOffer& offer);
  // Accepts the trade as it stands, or withdraws acceptance.
  void AcceptTrade(bool accepted);
  // Answers the finalize dialog. Cancelling clears this player's acceptance and
  // leaves the other player's.
  void ConfirmTrade(bool confirmed);
  // Leaves the trade, which ends it for both.
  void LeaveTrade();
  // Requests `account_id`'s sheet, and a new one whenever it changes. An empty
  // account stops watching, which is what closing the screen does.
  void WatchPlayer(const std::string& account_id);
  // `options` are the leader's settings, which the server applies to every
  // member before starting the fight.
  void StartFight(const std::string& boss_key, int difficulty_index,
                  PartyMode mode, const BossOptions& options = BossOptions());
  // What this client's fight has dealt, where its player is standing, and what
  // they're charging. Sent every step of a fight instead of queued as a
  // request, since a delayed report would arrive after the roster had changed.
  void SendFightUpdate(const FightUpdate& update);
  // Leaves the fight in progress.
  void LeaveFight();

  // Every fight message the server has sent since the last call, in order.
  // Taken instead of read from the snapshot because their damage lines are
  // drawn once, and a frame that saw two snapshots would lose some.
  std::vector<ServerMessage> TakeFightMessages();

 private:
  // The thread's whole loop: connect, communicate, reconnect.
  void Run();
  // One connection, from the Hello until it ends.
  Attempt RunConnection();
  // Waits out the backoff in short slices, so Stop() and Reconnect() respond
  // quickly.
  void WaitToRetry(std::chrono::seconds wait);
  // Opens a socket and sends the Hello. Returns false if the server couldn't be
  // reached.
  bool Open(Socket& socket);
  // Reads what has arrived and handles it. Returns false when the connection is
  // over.
  bool Pump(Socket& socket, std::string& incoming, std::string& outgoing,
            std::chrono::steady_clock::time_point& last_ping);
  void Handle(const ServerMessage& message, bool& keep);
  // Moves everything the screens have requested into `outgoing`.
  void SendQueued(std::string& outgoing);
  // Queues `message` for the thread to send.
  void Ask(const ClientMessage& message);

  void SetState(ConnectionState state, const std::string& message);
  // Resets the lobby to how a new connection starts, including this attempt's
  // outcome. The player's requests are kept, since a request made before the
  // connection was up is still wanted.
  void ForgetLobby();

  std::string host_;
  int port_ = 0;
  int protocol_version_ = 0;
  std::atomic<bool> running_{false};
  // Set by Reconnect() and cleared by the wait it interrupts.
  std::atomic<bool> retry_now_{false};
  std::thread thread_;

  mutable std::mutex mutex_;
  MultiplayerSnapshot snapshot_;
  // How the current connection has ended so far, which the backoff reads when
  // it's over.
  Attempt outcome_ = Attempt::kFailed;
  PlayerInfo player_;
  std::vector<ClientMessage> queued_;
  // Fight messages from the server that haven't been read yet.
  std::vector<ServerMessage> fight_;
};

}  // namespace ms

#endif  // MS_SRC_MULTIPLAYER_CLIENT_H_
