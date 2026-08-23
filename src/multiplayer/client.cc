#include "src/multiplayer/client.h"

#include <algorithm>
#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "src/multiplayer/protocol.h"
#include "src/net/socket.h"
#include "src/protos/multiplayer.pb.h"

namespace ms {
namespace {

// How long a connection attempt is given before it is called a failure.
constexpr std::chrono::seconds kConnectTimeout(5);
// How long one pass of the connection waits on the socket. Short enough that
// Stop() is answered promptly and the heartbeat is never late.
constexpr std::chrono::milliseconds kPumpTimeout(50);

constexpr char kUnreachableMessage[] = "Cannot reach the server. Trying again.";
constexpr char kLostMessage[] = "Lost the connection. Trying again.";

// Pushes the whole of `outgoing`, waiting on a socket that fills up. Only the
// Hello goes out this way: everything after it rides the connection's own
// pass over the socket.
bool WriteAll(const Socket& socket, std::string& outgoing) {
  while (!outgoing.empty()) {
    IoStatus status = Write(socket, outgoing);
    if (status == IoStatus::kOk) {
      continue;
    }
    if (status != IoStatus::kWouldBlock) {
      return false;
    }
    std::vector<PollTarget> targets(1);
    targets[0].handle = socket.handle();
    targets[0].want_write = true;
    if (!Poll(targets, kPumpTimeout)) {
      return false;
    }
  }
  return true;
}

}  // namespace

MultiplayerClient::MultiplayerClient(std::string host, int port,
                                     int protocol_version)
    : host_(std::move(host)), port_(port), protocol_version_(protocol_version) {
}

MultiplayerClient::~MultiplayerClient() {
  Stop();
}

void MultiplayerClient::Start(const PlayerInfo& player,
                              const std::string& token) {
  if (running_) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    player_ = player;
    snapshot_ = MultiplayerSnapshot();
    snapshot_.state = ConnectionState::kConnecting;
    snapshot_.account_id = player.account_id();
    snapshot_.token = token;
  }
  running_ = true;
  thread_ = std::thread([this]() { Run(); });
}

void MultiplayerClient::Stop() {
  if (!running_) {
    return;
  }
  running_ = false;
  if (thread_.joinable()) {
    thread_.join();
  }
  std::lock_guard<std::mutex> lock(mutex_);
  snapshot_.state = ConnectionState::kOffline;
  snapshot_.parties.Clear();
  snapshot_.party.Clear();
}

void MultiplayerClient::SetPlayer(const PlayerInfo& player) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    player_ = player;
  }
  // Told to the server as well as kept for the next Hello, so a party the
  // player is already in shows them as they are now.
  ClientMessage message;
  *message.mutable_update_player()->mutable_player() = player;
  Ask(message);
}

MultiplayerSnapshot MultiplayerClient::Snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return snapshot_;
}

void MultiplayerClient::CreateParty() {
  ClientMessage message;
  message.mutable_create_party();
  Ask(message);
}

void MultiplayerClient::JoinParty(const std::string& party_id) {
  ClientMessage message;
  message.mutable_join_party()->set_party_id(party_id);
  Ask(message);
}

void MultiplayerClient::LeaveParty() {
  ClientMessage message;
  message.mutable_leave_party();
  Ask(message);
}

void MultiplayerClient::SetReady(bool ready) {
  ClientMessage message;
  message.mutable_set_ready()->set_ready(ready);
  Ask(message);
}

void MultiplayerClient::Kick(const std::string& account_id) {
  ClientMessage message;
  message.mutable_kick_member()->set_account_id(account_id);
  Ask(message);
}

void MultiplayerClient::Promote(const std::string& account_id) {
  ClientMessage message;
  message.mutable_promote_member()->set_account_id(account_id);
  Ask(message);
}

void MultiplayerClient::StartFight(const std::string& boss_key,
                                   int difficulty_index, PartyMode mode) {
  ClientMessage message;
  message.mutable_start_fight()->set_boss_key(boss_key);
  message.mutable_start_fight()->set_difficulty_index(difficulty_index);
  message.mutable_start_fight()->set_mode(mode);
  Ask(message);
}

void MultiplayerClient::Run() {
  StartSockets();
  std::chrono::seconds wait = kFirstRetry;
  while (running_) {
    if (!RunConnection()) {
      return;
    }
    if (!running_) {
      return;
    }
    // Waited out in slices, so that Stop() does not have to sit through it.
    std::chrono::steady_clock::time_point until =
        std::chrono::steady_clock::now() + wait;
    while (running_ && std::chrono::steady_clock::now() < until) {
      std::this_thread::sleep_for(kPumpTimeout);
    }
    wait = std::min(wait * 2, kLongestRetry);
  }
}

bool MultiplayerClient::RunConnection() {
  ForgetLobby();
  Socket socket;
  if (!Open(socket)) {
    SetState(ConnectionState::kUnavailable, kUnreachableMessage);
    return true;
  }
  std::string incoming;
  std::string outgoing;
  std::chrono::steady_clock::time_point last_ping =
      std::chrono::steady_clock::now();
  while (running_ && Pump(socket, incoming, outgoing, last_ping)) {
  }
  // A rejection has already said what was wrong; anything else ended without
  // a word, and losing the connection is the story.
  std::lock_guard<std::mutex> lock(mutex_);
  if (snapshot_.state == ConnectionState::kRefused) {
    return false;
  }
  if (running_ && snapshot_.state != ConnectionState::kUnavailable) {
    snapshot_.state = ConnectionState::kUnavailable;
    snapshot_.message = kLostMessage;
  }
  return true;
}

bool MultiplayerClient::Open(Socket& socket) {
  SetState(ConnectionState::kConnecting, "");
  std::optional<Socket> opened = Connect(host_, port_, kConnectTimeout);
  if (!opened.has_value()) {
    return false;
  }
  socket = std::move(*opened);

  ClientMessage hello;
  hello.mutable_hello()->set_protocol_version(protocol_version_);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    hello.mutable_hello()->set_token(snapshot_.token);
    *hello.mutable_hello()->mutable_player() = player_;
    hello.mutable_hello()->mutable_player()->set_account_id(
        snapshot_.account_id);
  }
  std::string outgoing;
  return Encode(hello, outgoing) && WriteAll(socket, outgoing);
}

bool MultiplayerClient::Pump(Socket& socket, std::string& incoming,
                             std::string& outgoing,
                             std::chrono::steady_clock::time_point& last_ping) {
  SendQueued(outgoing);
  std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
  if (now - last_ping >= kHeartbeatInterval) {
    last_ping = now;
    ClientMessage ping;
    ping.mutable_ping();
    Encode(ping, outgoing);
  }

  std::vector<PollTarget> targets(1);
  targets[0].handle = socket.handle();
  targets[0].want_read = true;
  targets[0].want_write = !outgoing.empty();
  Poll(targets, kPumpTimeout);

  if (targets[0].readable || targets[0].closed) {
    IoStatus status = Read(socket, incoming);
    if (status == IoStatus::kClosed || status == IoStatus::kError) {
      return false;
    }
  }
  if (!outgoing.empty() && Write(socket, outgoing) == IoStatus::kError) {
    return false;
  }
  while (true) {
    ServerMessage message;
    DecodeStatus decoded = Decode(incoming, message);
    if (decoded == DecodeStatus::kIncomplete) {
      return true;
    }
    if (decoded == DecodeStatus::kBroken) {
      return false;
    }
    bool keep = true;
    Handle(message, keep);
    if (!keep) {
      return false;
    }
  }
}

void MultiplayerClient::Handle(const ServerMessage& message, bool& keep) {
  std::lock_guard<std::mutex> lock(mutex_);
  switch (message.kind_case()) {
    case ServerMessage::kWelcome:
      snapshot_.account_id = message.welcome().account_id();
      snapshot_.token = message.welcome().token();
      snapshot_.state = ConnectionState::kConnected;
      snapshot_.message.clear();
      return;
    case ServerMessage::kPartyList:
      snapshot_.parties = message.party_list();
      return;
    case ServerMessage::kPartyState:
      snapshot_.party = message.party_state().party();
      return;
    case ServerMessage::kRefused:
      snapshot_.notice = message.refused().message();
      snapshot_.notice_is_refusal = true;
      ++snapshot_.notice_serial;
      return;
    case ServerMessage::kPartyEvent:
      // Down the same channel as a refusal: both are the server speaking to
      // this player alone, and one screen shows either.
      snapshot_.notice = message.party_event().message();
      snapshot_.notice_is_refusal = false;
      ++snapshot_.notice_serial;
      return;
    case ServerMessage::kRejected:
      // Maintenance passes; the other reasons do not, and retrying would only
      // be turned away again.
      snapshot_.message = message.rejected().message();
      snapshot_.state =
          message.rejected().reason() == Rejected::REASON_MAINTENANCE
              ? ConnectionState::kUnavailable
              : ConnectionState::kRefused;
      keep = false;
      return;
    case ServerMessage::kPong:
    case ServerMessage::KIND_NOT_SET:
      return;
  }
}

void MultiplayerClient::SendQueued(std::string& outgoing) {
  std::vector<ClientMessage> asks;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (snapshot_.state != ConnectionState::kConnected) {
      return;
    }
    asks.swap(queued_);
  }
  for (const ClientMessage& ask : asks) {
    Encode(ask, outgoing);
  }
}

void MultiplayerClient::Ask(const ClientMessage& message) {
  std::lock_guard<std::mutex> lock(mutex_);
  queued_.push_back(message);
}

void MultiplayerClient::SetState(ConnectionState state,
                                 const std::string& message) {
  std::lock_guard<std::mutex> lock(mutex_);
  snapshot_.state = state;
  snapshot_.message = message;
}

void MultiplayerClient::ForgetLobby() {
  std::lock_guard<std::mutex> lock(mutex_);
  snapshot_.parties.Clear();
  snapshot_.party.Clear();
}

}  // namespace ms
