#include "server/server.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "src/character/character.h"
#include "src/multiplayer/protocol.h"
#include "src/net/socket.h"
#include "src/protos/multiplayer.pb.h"

namespace ms {
namespace {

// What a player is told when their build is not the server's.
constexpr char kUpdateMessage[] =
    "This version of the game cannot play with others. Update it to join.";
constexpr char kMaintenanceMessage[] =
    "The server is being updated. Try again in a minute.";

// Characters in an account id and in the token that proves it. The id is
// short enough to read in a log line; the token is long enough that guessing
// one is not worth trying.
constexpr int kAccountIdCharacters = 16;
constexpr int kTokenCharacters = 32;

// The name a player is shown under when they send one that cannot be used.
constexpr char kFallbackName[] = "Adventurer";

// `name` as the lobby will show it: trimmed to the length a character name is
// allowed, and never empty.
std::string DisplayName(const std::string& name) {
  std::string trimmed = name.substr(0, kMaxUsernameLength);
  return trimmed.empty() ? kFallbackName : trimmed;
}

}  // namespace

Server::Server(Socket listener, unsigned int seed)
    : listener_(std::move(listener)), rng_(seed) {
}

void Server::Step(std::chrono::steady_clock::time_point now,
                  std::chrono::milliseconds timeout) {
  std::vector<PollTarget> targets;
  targets.reserve(sessions_.size() + 1);
  if (listener_.valid()) {
    PollTarget target;
    target.handle = listener_.handle();
    target.want_read = true;
    targets.push_back(target);
  }
  for (const std::unique_ptr<Session>& session : sessions_) {
    PollTarget target;
    target.handle = session->socket.handle();
    target.want_read = !session->closing;
    target.want_write = !session->outgoing.empty();
    targets.push_back(target);
  }
  Poll(targets, timeout);

  size_t next = 0;
  if (listener_.valid()) {
    if (targets[0].readable) {
      AcceptWaiting(now);
    }
    next = 1;
  }
  // Over the sessions that existed before the accept: the ones just taken on
  // were not polled and have nothing to say yet.
  size_t polled = targets.size() - next;
  for (size_t i = 0; i < polled; ++i) {
    Session& session = *sessions_[i];
    const PollTarget& target = targets[next + i];
    bool live = true;
    if (target.readable || target.closed) {
      live = ReadSession(session, now);
    }
    if (live && (target.writable || !session.outgoing.empty())) {
      live = WriteSession(session);
    }
    if (!live) {
      session.socket.Close();
    }
  }

  for (const std::unique_ptr<Session>& session : sessions_) {
    if (session->socket.valid() && session->greeted &&
        now - session->last_heard > kSessionTimeout) {
      LOG(INFO) << "Session " << session->id << " went quiet";
      session->socket.Close();
    }
  }
  sessions_.erase(std::remove_if(sessions_.begin(), sessions_.end(),
                                 [](const std::unique_ptr<Session>& session) {
                                   return !session->socket.valid();
                                 }),
                  sessions_.end());
}

void Server::Drain() {
  if (draining_) {
    return;
  }
  draining_ = true;
  listener_.Close();
  LOG(INFO) << "Draining " << sessions_.size() << " session(s)";
  for (const std::unique_ptr<Session>& session : sessions_) {
    Reject(*session, Rejected::REASON_MAINTENANCE, kMaintenanceMessage);
  }
}

bool Server::drained() const {
  return draining_ && sessions_.empty();
}

int Server::player_count() const {
  int count = 0;
  for (const std::unique_ptr<Session>& session : sessions_) {
    if (session->greeted && !session->closing) {
      ++count;
    }
  }
  return count;
}

void Server::AcceptWaiting(std::chrono::steady_clock::time_point now) {
  while (true) {
    std::optional<Socket> accepted = Accept(listener_);
    if (!accepted.has_value()) {
      return;
    }
    if (static_cast<int>(sessions_.size()) >= kMaxSessions) {
      LOG(WARNING) << "Turning a connection away: the server is full";
      return;
    }
    std::unique_ptr<Session> session = std::make_unique<Session>();
    session->socket = std::move(*accepted);
    session->id = next_session_id_++;
    session->last_heard = now;
    sessions_.push_back(std::move(session));
  }
}

bool Server::ReadSession(Session& session,
                         std::chrono::steady_clock::time_point now) {
  IoStatus status = Read(session.socket, session.incoming);
  if (status == IoStatus::kClosed || status == IoStatus::kError) {
    return false;
  }
  session.last_heard = now;
  while (true) {
    ClientMessage message;
    DecodeStatus decoded = Decode(session.incoming, message);
    if (decoded == DecodeStatus::kIncomplete) {
      return true;
    }
    if (decoded == DecodeStatus::kBroken) {
      Reject(session, Rejected::REASON_MALFORMED, "Unreadable message.");
      return true;
    }
    if (session.closing) {
      // Already on the way out; nothing it says now can be acted on.
      return true;
    }
    Handle(session, message);
  }
}

bool Server::WriteSession(Session& session) {
  IoStatus status = Write(session.socket, session.outgoing);
  if (status == IoStatus::kClosed || status == IoStatus::kError) {
    return false;
  }
  return !(session.closing && session.outgoing.empty());
}

void Server::Handle(Session& session, const ClientMessage& message) {
  if (!session.greeted) {
    if (message.kind_case() != ClientMessage::kHello) {
      Reject(session, Rejected::REASON_MALFORMED, "Say hello first.");
      return;
    }
    HandleHello(session, message.hello());
    return;
  }
  switch (message.kind_case()) {
    case ClientMessage::kPing: {
      ServerMessage pong;
      pong.mutable_pong();
      Send(session, pong);
      return;
    }
    case ClientMessage::kHello:
      Reject(session, Rejected::REASON_MALFORMED, "Already greeted.");
      return;
    default:
      // The lobby lands in the commit after this one.
      Refuse(session, Refused::REASON_UNSPECIFIED, "Parties are not open.");
      return;
  }
}

void Server::HandleHello(Session& session, const Hello& hello) {
  if (hello.protocol_version() != kMultiplayerVersion) {
    Reject(session, Rejected::REASON_UPDATE_REQUIRED, kUpdateMessage);
    return;
  }
  if (draining_) {
    Reject(session, Rejected::REASON_MAINTENANCE, kMaintenanceMessage);
    return;
  }
  std::string token;
  std::string account = ResolveAccount(hello, token);
  if (account.empty()) {
    Reject(session, Rejected::REASON_BAD_CREDENTIALS,
           "This account is being played somewhere else.");
    return;
  }
  session.greeted = true;
  session.account_id = account;
  session.player = hello.player();
  session.player.set_account_id(account);
  session.player.set_name(DisplayName(hello.player().name()));

  ServerMessage welcome;
  welcome.mutable_welcome()->set_account_id(account);
  welcome.mutable_welcome()->set_token(token);
  Send(session, welcome);
  LOG(INFO) << "Session " << session.id << " is " << session.player.name()
            << " (" << account << "), level " << session.player.level();
}

void Server::Send(Session& session, const ServerMessage& message) {
  if (!Encode(message, session.outgoing)) {
    LOG(ERROR) << "Could not encode a message for session " << session.id;
  }
}

void Server::Refuse(Session& session, Refused::Reason reason,
                    const std::string& message) {
  ServerMessage refusal;
  refusal.mutable_refused()->set_reason(reason);
  refusal.mutable_refused()->set_message(message);
  Send(session, refusal);
}

void Server::Reject(Session& session, Rejected::Reason reason,
                    const std::string& message) {
  if (session.closing) {
    return;
  }
  ServerMessage rejection;
  rejection.mutable_rejected()->set_reason(reason);
  rejection.mutable_rejected()->set_server_protocol_version(
      kMultiplayerVersion);
  rejection.mutable_rejected()->set_message(message);
  Send(session, rejection);
  session.closing = true;
}

std::string Server::ResolveAccount(const Hello& hello, std::string& token) {
  const std::string& claimed = hello.player().account_id();
  if (claimed.empty()) {
    std::string account = NewId(kAccountIdCharacters);
    token = NewId(kTokenCharacters);
    tokens_[account] = token;
    return account;
  }
  std::map<std::string, std::string>::iterator known = tokens_.find(claimed);
  if (known == tokens_.end()) {
    // An account from before a restart. Adopting it costs nothing and keeps
    // the player's identity across an update.
    token = hello.token().empty() ? NewId(kTokenCharacters) : hello.token();
    tokens_[claimed] = token;
    return claimed;
  }
  if (known->second != hello.token()) {
    return "";
  }
  token = known->second;
  return claimed;
}

std::string Server::NewId(int characters) {
  constexpr char kDigits[] = "0123456789abcdef";
  std::uniform_int_distribution<int> digit(0, 15);
  std::string id;
  id.reserve(characters);
  for (int i = 0; i < characters; ++i) {
    id.push_back(kDigits[digit(rng_)]);
  }
  return id;
}

}  // namespace ms
