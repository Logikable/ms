/* The multiplayer server. It tracks every connected client and handles their
 * messages.
 *
 * Everything runs on one thread with non-blocking sockets on one poll, so
 * fights step in the same loop without locks.
 *
 * Accounts live in memory. An unknown id is adopted, so a restart does not cost
 * anyone their identity. The token only stops one live client from claiming
 * another's id.
 */
#ifndef MS_SERVER_SERVER_H_
#define MS_SERVER_SERVER_H_

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "server/fight.h"
#include "server/lobby.h"
#include "server/trade.h"
#include "src/multiplayer/protocol.h"
#include "src/net/socket.h"
#include "src/protos/boss.pb.h"
#include "src/protos/mob.pb.h"
#include "src/protos/multiplayer.pb.h"

namespace ms {

// The most clients at once. This is far above normal load and only stops a
// runaway from using up the process's file descriptors.
inline constexpr int kMaxSessions = 64;

class Server {
 public:
  // `listener` must be open and listening. The caller owns `bosses` and
  // `mobs`, and they must outlive the server. `seed` makes ids predictable in
  // tests, and `protocol_version` lets a test act as an old server.
  Server(Socket listener, const std::map<std::string, Boss>& bosses,
         const std::map<std::string, Mob>& mobs,
         unsigned int seed = std::random_device()(),
         int protocol_version = kMultiplayerVersion);

  // Runs one pass of the loop: waits up to `timeout` for socket activity,
  // reads incoming messages, sends queued ones, and drops finished sessions
  // and fights. `now` is when the pass began.
  void Step(std::chrono::steady_clock::time_point now,
            std::chrono::milliseconds timeout);

  // Stops taking connections and disconnects everyone with a maintenance
  // notice. main.cc calls this on SIGTERM.
  void Drain();
  // True once draining has finished and the process can exit.
  bool drained() const;

  // Connections that completed the handshake and are still open.
  int player_count() const;
  // Fights in progress.
  int fight_count() const {
    return static_cast<int>(fights_.size());
  }
  // All connections, including ones still in the handshake.
  int session_count() const {
    return static_cast<int>(sessions_.size());
  }

 private:
  // One connected client.
  struct Session {
    Socket socket;
    int64_t id = 0;
    // Raw bytes received but not yet decoded, and bytes waiting to be sent.
    std::string incoming;
    std::string outgoing;
    // False until a Hello is accepted. Any other message before that is
    // rejected.
    bool greeted = false;
    // Set after the final message is queued. The socket closes once the
    // outgoing buffer empties.
    bool closing = false;
    std::string account_id;
    PlayerInfo player;
    // The account open on this client's Inspect screen, or empty. The client
    // gets that player's sheet whenever it changes.
    std::string watching;
    std::chrono::steady_clock::time_point last_heard;
  };

  // Names a session for the log: its number, plus the player once they have
  // said hello.
  std::string Describe(const Session& session) const;

  // Times out quiet sessions, removes closed ones from their party, fight and
  // trade, and deletes them.
  void DropFinished(std::chrono::steady_clock::time_point now);
  // Sends lobby changes: each affected player's party, and the open party
  // list to everyone if it changed.
  void PublishLobby();
  // Sends the online roster to everyone if it changed.
  void PublishOnline();
  // Returns everyone connected, in arrival order, without their sheets.
  OnlinePlayers Roster() const;
  // Sends trade state changes, completions, and trade request notifications.
  void PublishTrades();
  // Sends `account_id`'s sheet to every client inspecting them. Called
  // whenever their character changes, to keep Inspect screens current.
  void PublishWatched(const std::string& account_id);

  // Creates the fight for a party whose start request succeeded.
  void OpenFight(const std::string& account_id, const StartFight& request);
  // Advances every fight by the real time since the last pass.
  void StepFights(std::chrono::steady_clock::time_point now);
  // Broadcasts each fight's state on the publish interval, and closes fights
  // that are done.
  void PublishFights(std::chrono::steady_clock::time_point now);
  // Sends `fight`'s state to its players and clears the damage lines sent.
  void PublishFight(PartyFight& fight);
  // Tells everyone still in `fight` how it ended, and returns the party to
  // the lobby.
  void CloseFight(const std::string& party_id, const PartyFight& fight);
  // Returns `account_id`'s fight, or null.
  PartyFight* FightOf(const std::string& account_id);
  // Applies one client's fight report.
  void HandleFightUpdate(Session& session, const FightUpdate& update);
  // Returns `account_id`'s open session, or null.
  Session* FindSession(const std::string& account_id);

  // Accepts waiting connections, up to kMaxSessions.
  void AcceptWaiting(std::chrono::steady_clock::time_point now);
  // Reads from `session` and handles every complete message. Returns false
  // when the connection should close.
  bool ReadSession(Session& session, std::chrono::steady_clock::time_point now);
  // Writes `session`'s queued bytes. Returns false when the connection should
  // close, including after a rejected client's last message is sent.
  bool WriteSession(Session& session);
  // Handles one message from `session`.
  void Handle(Session& session, const ClientMessage& message);
  void HandleHello(Session& session, const Hello& hello);
  // Handles one lobby request, sending a refusal back if it fails.
  void HandleLobby(Session& session, const ClientMessage& message);
  // Handles one trade request the same way.
  void HandleTrade(Session& session, const ClientMessage& message);
  // Stores the character a client sent, but with the session's account id
  // and a cleaned-up name.
  void SetPlayer(Session& session, const PlayerInfo& player);
  // Sets the player `session` is inspecting and sends their sheet. An empty
  // account means the screen closed. A player who is offline stays watched,
  // and their sheet is sent once they return.
  void HandleWatch(Session& session, const WatchPlayer& watch);

  // Queues `message` for `session`.
  void Send(Session& session, const ServerMessage& message);
  // Queues a refusal that leaves the connection up.
  void Refuse(Session& session, Refused::Reason reason,
              const std::string& message);
  // Queues a rejection and closes the connection once it has gone out.
  void Reject(Session& session, Rejected::Reason reason,
              const std::string& message);

  // Queues the open party list for a newly arrived `session`.
  void SendListing(Session& session);

  // Returns the account `hello` claims, or a new one, and sets `token`.
  // Returns empty if the token does not match the id.
  std::string ResolveAccount(const Hello& hello, std::string& token);
  Socket listener_;
  const std::map<std::string, Boss>* bosses_ = nullptr;
  const std::map<std::string, Mob>* mobs_ = nullptr;
  Lobby lobby_;
  Trades trades_;
  // Fights in progress, by party id. A party has at most one fight, and is
  // not listed in the lobby during it.
  std::map<std::string, std::unique_ptr<PartyFight>> fights_;
  // The number in the next fight's id, which is `<party id>-<number>`.
  int64_t next_fight_id_ = 1;
  // When the last pass ran, and when the next fight broadcast is due. Fights
  // are broadcast on a fixed interval, not every time the loop wakes.
  std::chrono::steady_clock::time_point stepped_at_;
  std::chrono::steady_clock::time_point publish_fights_at_;
  std::vector<std::unique_ptr<Session>> sessions_;
  int64_t next_session_id_ = 1;
  // Each account seen since startup and its token. Lost on restart, which is
  // fine because unknown ids are adopted.
  std::map<std::string, std::string> tokens_;
  std::mt19937 rng_;
  int protocol_version_ = kMultiplayerVersion;
  // Whether the roster changed since it was last sent.
  bool online_changed_ = false;
  bool draining_ = false;
};

}  // namespace ms

#endif  // MS_SERVER_SERVER_H_
