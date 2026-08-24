/* The multiplayer server: every client connected to it, and what they are
 * allowed to say.
 *
 * One thread runs the whole thing. Every socket is non-blocking and one poll
 * covers all of them, so a session that stops reading slows nobody down, and
 * the fights step in the same pass without anything having to be made
 * thread-safe.
 *
 * Accounts live in memory. The server hands out an id and a token to a player
 * it has never seen, and adopts one it does not recognise -- so a restart
 * costs nobody their identity, and the token only stops one live client from
 * claiming another's id.
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
#include "src/net/socket.h"
#include "src/protos/boss.pb.h"
#include "src/protos/mob.pb.h"
#include "src/protos/multiplayer.pb.h"

namespace ms {

// The most clients at once. Far above anything expected; it is here so a
// runaway cannot take the process's file descriptors.
inline constexpr int kMaxSessions = 64;

class Server {
 public:
  // `listener` must be an open listening socket. `bosses` and `mobs` are the
  // catalogs, owned by the caller and outliving the server: the fights a party
  // may ask for, and the monsters those fights stand up. `seed` fixes the
  // stream ids are drawn from, so a test can say what it will be handed.
  Server(Socket listener, const std::map<std::string, Boss>& bosses,
         const std::map<std::string, Mob>& mobs,
         unsigned int seed = std::random_device()());

  // One pass of the loop: wait up to `timeout` for a socket to be ready, take
  // what has arrived, send what is queued, and then let go of whatever the
  // clock says is over. `now` is the moment the pass began.
  void Step(std::chrono::steady_clock::time_point now,
            std::chrono::milliseconds timeout);

  // Stops taking connections and sends everyone away with a maintenance
  // notice. What SIGTERM means -- see //server:ms_server.
  void Drain();
  // True once draining is finished and the process can exit.
  bool drained() const;

  // Connections that have said hello and not yet gone.
  int player_count() const;
  // Fights being fought right now.
  int fight_count() const {
    return static_cast<int>(fights_.size());
  }
  // Connections at all, handshake or no.
  int session_count() const {
    return static_cast<int>(sessions_.size());
  }

 private:
  // One connected client.
  struct Session {
    Socket socket;
    int64_t id = 0;
    // What has arrived and not yet been read as a message, and what is waiting
    // to go out. Both are the socket's business, not the protocol's.
    std::string incoming;
    std::string outgoing;
    // False until a Hello has been accepted. Nothing else is listened to
    // before that.
    bool greeted = false;
    // Set once the last thing worth sending has been queued. The socket
    // closes as soon as it drains.
    bool closing = false;
    std::string account_id;
    PlayerInfo player;
    std::chrono::steady_clock::time_point last_heard;
  };

  // How a session is named in the log: its number, and the player at it once
  // one has said hello.
  std::string Describe(const Session& session) const;

  // Closes the sessions the last pass finished with, telling the lobby about
  // anyone who has gone, and drops the ones that have been quiet too long.
  void DropFinished(std::chrono::steady_clock::time_point now);
  // Sends everyone whatever the lobby changed: their own party to the players
  // it moved, and the open list to everybody once it has.
  void PublishLobby();

  // Stands up the fight a party has just been let into.
  void OpenFight(const std::string& account_id, const StartFight& request);
  // Runs every fight's clock on. Before the reads, so a report that arrives
  // in the same pass as the end of the count-in still lands.
  void StepFights(std::chrono::steady_clock::time_point now);
  // Sends each party what its fight looks like, on the beat, and lets go of
  // the ones that have finished.
  void PublishFights(std::chrono::steady_clock::time_point now);
  // Sends everyone in `fight` the state of it, and takes the lines it has just
  // passed on.
  void PublishFight(PartyFight& fight);
  // Tells everyone still in `fight` how it ended and hands the party back to
  // the lobby.
  void CloseFight(const std::string& party_id, const PartyFight& fight);
  // The fight `account_id` is in, or null.
  PartyFight* FightOf(const std::string& account_id);
  // Takes what one client's fight has landed.
  void HandleFightUpdate(Session& session, const FightUpdate& update);
  // The session `account_id` is playing on, or null if they have gone.
  Session* FindSession(const std::string& account_id);

  // Takes whatever connections are waiting, up to kMaxSessions.
  void AcceptWaiting(std::chrono::steady_clock::time_point now);
  // Reads `session`, handling every whole message that has arrived. Returns
  // false when the connection is finished with.
  bool ReadSession(Session& session, std::chrono::steady_clock::time_point now);
  // Pushes whatever `session` has queued. Returns false when the connection
  // is finished with, the drained close of a rejected client included.
  bool WriteSession(Session& session);
  // Acts on one message from `session`.
  void Handle(Session& session, const ClientMessage& message);
  void HandleHello(Session& session, const Hello& hello);
  // Answers one lobby ask, refusing it on the connection it came from.
  void HandleLobby(Session& session, const ClientMessage& message);
  // Takes the character a client sent, under the account and the name the
  // server allows rather than the ones it was handed.
  void SetPlayer(Session& session, const PlayerInfo& player);

  // Queues `message` for `session`.
  void Send(Session& session, const ServerMessage& message);
  // Queues a refusal that leaves the connection up.
  void Refuse(Session& session, Refused::Reason reason,
              const std::string& message);
  // Queues a rejection and closes the connection once it has gone out.
  void Reject(Session& session, Rejected::Reason reason,
              const std::string& message);

  // Queues the open party list for `session` alone, which is what a player
  // arriving needs before anything changes.
  void SendListing(Session& session);

  // The account `hello` claims, or a fresh one. Empty when the token does not
  // go with the id, which is the one way a Hello is turned away for anything
  // but its version.
  std::string ResolveAccount(const Hello& hello, std::string& token);
  Socket listener_;
  const std::map<std::string, Boss>* bosses_ = nullptr;
  const std::map<std::string, Mob>* mobs_ = nullptr;
  Lobby lobby_;
  // The fights being fought, by the party fighting each one. A party has at
  // most one, and it is not in the lobby list while it lasts.
  std::map<std::string, std::unique_ptr<PartyFight>> fights_;
  // When the last pass ran and when the next fight broadcast is due. A fight
  // is stepped by real time, and told to its party ten times a second rather
  // than every time a socket wakes the loop.
  std::chrono::steady_clock::time_point stepped_at_;
  std::chrono::steady_clock::time_point publish_fights_at_;
  std::vector<std::unique_ptr<Session>> sessions_;
  int64_t next_session_id_ = 1;
  // Every account the server has seen since it started, and the token that
  // proves it. Forgotten on restart, which costs nothing: an id the server
  // does not know is adopted.
  std::map<std::string, std::string> tokens_;
  std::mt19937 rng_;
  bool draining_ = false;
};

}  // namespace ms

#endif  // MS_SERVER_SERVER_H_
