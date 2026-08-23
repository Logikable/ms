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
#include "server/ids.h"
#include "server/lobby.h"
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

// A second stream out of one seed. The lobby draws party ids and the server
// draws account ids; seeded alike, the two hand out the same strings, which
// reads as a bug even though it is not.
unsigned int OtherStream(unsigned int seed) {
  return seed ^ 0x9e3779b9u;
}

// The name a player is shown under when they send one that cannot be used.
constexpr char kFallbackName[] = "Adventurer";

// `name` as the lobby will show it: trimmed to the length a character name is
// allowed, and never empty.
std::string DisplayName(const std::string& name) {
  std::string trimmed = name.substr(0, kMaxUsernameLength);
  return trimmed.empty() ? kFallbackName : trimmed;
}

}  // namespace

Server::Server(Socket listener, const std::map<std::string, Boss>& bosses,
               unsigned int seed)
    : listener_(std::move(listener)),
      lobby_(bosses, OtherStream(seed)),
      rng_(seed) {
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
    if (targets[next + i].readable || targets[next + i].closed) {
      if (!ReadSession(session, now)) {
        session.socket.Close();
      }
    }
  }
  // Before the writes, so that whatever the reads changed goes out in the
  // same pass rather than a poll later.
  PublishLobby();
  for (const std::unique_ptr<Session>& session : sessions_) {
    if (session->socket.valid() && !session->outgoing.empty() &&
        !WriteSession(*session)) {
      session->socket.Close();
    }
  }
  DropFinished(now);
}

void Server::DropFinished(std::chrono::steady_clock::time_point now) {
  for (const std::unique_ptr<Session>& session : sessions_) {
    if (session->socket.valid() && session->greeted &&
        now - session->last_heard > kSessionTimeout) {
      LOG(INFO) << "Session " << session->id << " went quiet";
      session->socket.Close();
    }
    if (!session->socket.valid() && !session->account_id.empty()) {
      lobby_.Disconnect(session->account_id);
      session->account_id.clear();
    }
  }
  sessions_.erase(std::remove_if(sessions_.begin(), sessions_.end(),
                                 [](const std::unique_ptr<Session>& session) {
                                   return !session->socket.valid();
                                 }),
                  sessions_.end());
  // A player leaving changes what the others can see, and the sessions left
  // to tell are the ones still here.
  PublishLobby();
}

void Server::PublishLobby() {
  for (const std::string& account : lobby_.TakeChanged()) {
    Session* session = FindSession(account);
    if (session == nullptr) {
      continue;
    }
    ServerMessage state;
    *state.mutable_party_state()->mutable_party() = lobby_.StateFor(account);
    Send(*session, state);
  }
  if (!lobby_.TakeListingChanged()) {
    return;
  }
  ServerMessage listing;
  *listing.mutable_party_list() = lobby_.Listed();
  for (const std::unique_ptr<Session>& session : sessions_) {
    if (session->greeted && !session->closing && session->socket.valid()) {
      Send(*session, listing);
    }
  }
}

Server::Session* Server::FindSession(const std::string& account_id) {
  for (const std::unique_ptr<Session>& session : sessions_) {
    if (session->socket.valid() && !session->closing &&
        session->account_id == account_id) {
      return session.get();
    }
  }
  return nullptr;
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
    case ClientMessage::KIND_NOT_SET:
      Reject(session, Rejected::REASON_MALFORMED, "Empty message.");
      return;
    default:
      HandleLobby(session, message);
      return;
  }
}

void Server::SetPlayer(Session& session, const PlayerInfo& player) {
  session.player = player;
  session.player.set_account_id(session.account_id);
  session.player.set_name(DisplayName(player.name()));
}

void Server::HandleLobby(Session& session, const ClientMessage& message) {
  LobbyResult result;
  switch (message.kind_case()) {
    case ClientMessage::kUpdatePlayer:
      SetPlayer(session, message.update_player().player());
      lobby_.UpdatePlayer(session.player);
      return;
    case ClientMessage::kCreateParty:
      result = lobby_.Create(session.player, message.create_party());
      break;
    case ClientMessage::kJoinParty:
      result = lobby_.Join(session.player, message.join_party().party_id());
      break;
    case ClientMessage::kLeaveParty:
      result = lobby_.Leave(session.account_id);
      break;
    case ClientMessage::kStartFight:
      result = lobby_.Start(session.account_id);
      break;
    default:
      Reject(session, Rejected::REASON_MALFORMED, "Unknown message.");
      return;
  }
  if (!result.ok) {
    Refuse(session, result.reason, result.message);
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
  SetPlayer(session, hello.player());

  ServerMessage welcome;
  welcome.mutable_welcome()->set_account_id(account);
  welcome.mutable_welcome()->set_token(token);
  Send(session, welcome);
  SendListing(session);
  LOG(INFO) << "Session " << session.id << " is " << session.player.name()
            << " (" << account << "), level " << session.player.level();
}

void Server::SendListing(Session& session) {
  ServerMessage listing;
  *listing.mutable_party_list() = lobby_.Listed();
  Send(session, listing);
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
    std::string account = RandomHexId(rng_, kAccountIdCharacters);
    token = RandomHexId(rng_, kTokenCharacters);
    tokens_[account] = token;
    return account;
  }
  std::map<std::string, std::string>::iterator known = tokens_.find(claimed);
  if (known == tokens_.end()) {
    // An account from before a restart. Adopting it costs nothing and keeps
    // the player's identity across an update.
    token = hello.token().empty() ? RandomHexId(rng_, kTokenCharacters)
                                  : hello.token();
    tokens_[claimed] = token;
    return claimed;
  }
  if (known->second != hello.token()) {
    return "";
  }
  token = known->second;
  return claimed;
}

}  // namespace ms
