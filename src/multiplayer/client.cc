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

// How long a connection attempt may take before it counts as failed.
constexpr std::chrono::seconds kConnectTimeout(5);
// How long one pass of the connection waits on the socket. Short enough that
// Stop() responds quickly and the heartbeat is never late. It's also the delay
// on every party action, since a message queued during a wait is sent at the
// start of the next pass.
constexpr std::chrono::milliseconds kPumpTimeout(5);
// How long each sleep lasts while waiting between connection attempts. Nothing
// depends on it, so it wakes rarely.
constexpr std::chrono::milliseconds kRetrySleep(50);

constexpr char kUnreachableMessage[] = "Cannot reach the server.";
constexpr char kLostMessage[] = "Lost connection.";

// The text to show for a rejection. A version mismatch is worded here instead
// of taken from the server, because the client knows both versions, and which
// one is behind decides what the player can do. The two versions go on a second
// line, since that's the first thing anyone asks.
std::string RejectionMessage(const Rejected& rejected, int our_version) {
  if (rejected.reason() != Rejected::REASON_UPDATE_REQUIRED) {
    return rejected.message();
  }
  std::string versions = "\nClient: v" + std::to_string(our_version) +
                         ", Server: v" +
                         std::to_string(rejected.server_protocol_version());
  if (rejected.server_protocol_version() > our_version) {
    return "Update the game to play with others." + versions;
  }
  return "The server is running an older version. Trying again." + versions;
}

// Sends all of `outgoing`, waiting whenever the socket is full. Only the Hello
// is sent this way; everything after it goes through the connection's normal
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
  snapshot_.online.Clear();
  snapshot_.watched.Clear();
}

void MultiplayerClient::Reconnect() {
  retry_now_ = true;
}

void MultiplayerClient::SetPlayer(const PlayerInfo& player) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    player_ = player;
  }
  // Sent to the server as well as saved for the next Hello, so a party the
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

void MultiplayerClient::RequestTrade(const std::string& account_id) {
  ClientMessage message;
  message.mutable_request_trade()->set_account_id(account_id);
  Ask(message);
}

void MultiplayerClient::SetTradeOffer(const TradeOffer& offer) {
  ClientMessage message;
  *message.mutable_set_trade_offer()->mutable_offer() = offer;
  Ask(message);
}

void MultiplayerClient::AcceptTrade(bool accepted) {
  ClientMessage message;
  message.mutable_accept_trade()->set_accepted(accepted);
  Ask(message);
}

void MultiplayerClient::ConfirmTrade(bool confirmed) {
  ClientMessage message;
  message.mutable_confirm_trade()->set_confirmed(confirmed);
  Ask(message);
}

void MultiplayerClient::LeaveTrade() {
  ClientMessage message;
  message.mutable_leave_trade();
  Ask(message);
}

void MultiplayerClient::WatchPlayer(const std::string& account_id) {
  {
    // The current sheet belongs to whoever was being viewed before.
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.watched.Clear();
  }
  ClientMessage message;
  message.mutable_watch_player()->set_account_id(account_id);
  Ask(message);
}

void MultiplayerClient::StartFight(const std::string& boss_key,
                                   int difficulty_index, PartyMode mode,
                                   const BossOptions& options) {
  ClientMessage message;
  message.mutable_start_fight()->set_boss_key(boss_key);
  message.mutable_start_fight()->set_difficulty_index(difficulty_index);
  message.mutable_start_fight()->set_mode(mode);
  *message.mutable_start_fight()->mutable_options() = options;
  Ask(message);
}

void MultiplayerClient::SendFightUpdate(const FightUpdate& update) {
  ClientMessage message;
  *message.mutable_fight_update() = update;
  Ask(message);
}

void MultiplayerClient::LeaveFight() {
  ClientMessage message;
  message.mutable_leave_fight();
  Ask(message);
}

std::vector<ServerMessage> MultiplayerClient::TakeFightMessages() {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<ServerMessage> taken;
  taken.swap(fight_);
  return taken;
}

void MultiplayerClient::Run() {
  StartSockets();
  std::chrono::seconds wait = kFirstRetry;
  while (running_) {
    Attempt attempt = RunConnection();
    if (attempt == Attempt::kFinal || !running_) {
      return;
    }
    // A connection that was welcomed resets the backoff. A rejection jumps
    // straight to the maximum wait: something must change on the server before
    // retrying makes sense, so short waits would just add traffic.
    if (attempt == Attempt::kWelcomed) {
      wait = kFirstRetry;
    } else if (attempt == Attempt::kRejected) {
      wait = kLongestRetry;
    }
    WaitToRetry(wait);
    wait = std::min(wait * 2, kLongestRetry);
  }
}

void MultiplayerClient::WaitToRetry(std::chrono::seconds wait) {
  retry_now_ = false;
  std::chrono::steady_clock::time_point until =
      std::chrono::steady_clock::now() + wait;
  while (running_ && !retry_now_ && std::chrono::steady_clock::now() < until) {
    std::this_thread::sleep_for(kRetrySleep);
  }
  retry_now_ = false;
}

Attempt MultiplayerClient::RunConnection() {
  ForgetLobby();
  Socket socket;
  if (!Open(socket)) {
    SetState(ConnectionState::kUnavailable, kUnreachableMessage);
    return Attempt::kFailed;
  }
  std::string incoming;
  std::string outgoing;
  std::chrono::steady_clock::time_point last_ping =
      std::chrono::steady_clock::now();
  while (running_ && Pump(socket, incoming, outgoing, last_ping)) {
  }
  // A rejection already explained what went wrong. Anything else ended without
  // a message, so the lost connection is the explanation.
  std::lock_guard<std::mutex> lock(mutex_);
  if (outcome_ != Attempt::kRejected && outcome_ != Attempt::kFinal &&
      running_ && snapshot_.state != ConnectionState::kUnavailable) {
    snapshot_.state = ConnectionState::kUnavailable;
    snapshot_.message = kLostMessage;
  }
  return outcome_;
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
      outcome_ = Attempt::kWelcomed;
      return;
    case ServerMessage::kPartyList:
      snapshot_.parties = message.party_list();
      return;
    case ServerMessage::kPartyState:
      snapshot_.party = message.party_state().party();
      return;
    case ServerMessage::kOnlinePlayers:
      snapshot_.online = message.online_players();
      return;
    case ServerMessage::kPlayerSheet:
      snapshot_.watched = message.player_sheet().player();
      return;
    case ServerMessage::kTradeState:
      snapshot_.trade = message.trade_state();
      return;
    case ServerMessage::kTradeCompleted:
      // The trade ends with it: the server cancelled the trade to send this,
      // and an empty trade would otherwise look like the partner leaving.
      snapshot_.trade.Clear();
      snapshot_.trade_received = message.trade_completed().received();
      ++snapshot_.trade_serial;
      return;
    case ServerMessage::kNotification:
      snapshot_.notification.assign(message.notification().lines().begin(),
                                    message.notification().lines().end());
      ++snapshot_.notification_serial;
      return;
    case ServerMessage::kRefused:
      snapshot_.notice = message.refused().message();
      snapshot_.notice_is_refusal = true;
      ++snapshot_.notice_serial;
      return;
    case ServerMessage::kPartyEvent:
      // Uses the same channel as a refusal: both are the server speaking to
      // this player alone, and one screen shows either.
      snapshot_.notice = message.party_event().message();
      snapshot_.notice_is_refusal = false;
      ++snapshot_.notice_serial;
      return;
    case ServerMessage::kRejected:
      // Only a message the server couldn't read is final, since that means this
      // build is wrong. Every other reason is a condition on the server that
      // may change: the server updated, came back up, or the other session
      // released the account.
      snapshot_.server_protocol_version =
          message.rejected().server_protocol_version();
      snapshot_.message =
          RejectionMessage(message.rejected(), protocol_version_);
      if (message.rejected().reason() == Rejected::REASON_MALFORMED) {
        snapshot_.state = ConnectionState::kRefused;
        outcome_ = Attempt::kFinal;
      } else {
        snapshot_.state = ConnectionState::kUnavailable;
        outcome_ = Attempt::kRejected;
      }
      keep = false;
      return;
    case ServerMessage::kFightState:
    case ServerMessage::kFightEnded:
      // Kept as a whole message instead of merged into the snapshot: a fight
      // state carries everyone else's damage numbers, and a frame that read two
      // snapshots would draw one and lose the other.
      fight_.push_back(message);
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
  outcome_ = Attempt::kFailed;
  snapshot_.parties.Clear();
  snapshot_.party.Clear();
  snapshot_.online.Clear();
  snapshot_.watched.Clear();
  // A trade doesn't survive either: the server dropped it when the socket
  // closed.
  snapshot_.trade.Clear();
  // A fight doesn't survive losing the connection that was watching it.
  fight_.clear();
}

}  // namespace ms
