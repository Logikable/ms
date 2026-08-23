/* The multiplayer server: every client connected to it, and what they are
 * allowed to say.
 *
 * One thread runs the whole thing. Every socket is non-blocking and one poll
 * covers all of them, so a session that stops reading slows nobody down, and
 * the fight simulation that lands later can step in the same pass without
 * anything having to be made thread-safe.
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

#include "src/net/socket.h"
#include "src/protos/multiplayer.pb.h"

namespace ms {

// The most clients at once. Far above anything expected; it is here so a
// runaway cannot take the process's file descriptors.
inline constexpr int kMaxSessions = 64;

class Server {
 public:
  // `listener` must be an open listening socket. `seed` fixes the stream the
  // account ids are drawn from, so a test can say what it will be handed.
  explicit Server(Socket listener, unsigned int seed = std::random_device()());

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

  // Queues `message` for `session`.
  void Send(Session& session, const ServerMessage& message);
  // Queues a refusal that leaves the connection up.
  void Refuse(Session& session, Refused::Reason reason,
              const std::string& message);
  // Queues a rejection and closes the connection once it has gone out.
  void Reject(Session& session, Rejected::Reason reason,
              const std::string& message);

  // The account `hello` claims, or a fresh one. Empty when the token does not
  // go with the id, which is the one way a Hello is turned away for anything
  // but its version.
  std::string ResolveAccount(const Hello& hello, std::string& token);
  // A new id or token: hex, drawn from the server's own stream.
  std::string NewId(int characters);

  Socket listener_;
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
