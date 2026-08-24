#include "server/server.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "server/ids.h"
#include "server/lobby.h"
#include "src/character/character.h"
#include "src/character/job_name.h"
#include "src/multiplayer/protocol.h"
#include "src/net/socket.h"
#include "src/protos/multiplayer.pb.h"

namespace ms {
namespace {

// The moment as the reset clock reads it. A fight on a daily is checked
// against the wall clock, and the loop runs on a steady one that does not
// know what day it is.
int64_t WallNow() {
  return static_cast<int64_t>(std::time(nullptr));
}

// What a player is told when their build is not the server's.
constexpr char kUpdateMessage[] =
    "This version of the game cannot play with others. Update it to join.";
constexpr char kMaintenanceMessage[] = "The server went down.";

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

// What an update changed, as a line for the log. A client sends one whenever
// the name, level or job it last told the server has moved.
std::string Became(const PlayerInfo& before, const PlayerInfo& after) {
  std::vector<std::string> changes;
  std::string name = DisplayName(after.name());
  if (name != before.name()) {
    changes.push_back(absl::StrCat("is now named ", name));
  }
  if (after.level() != before.level()) {
    changes.push_back(absl::StrCat("is now level ", after.level()));
  }
  if (after.job() != before.job()) {
    changes.push_back(absl::StrCat(
        "advances to ", ShortJobName(JobForAdvancement(after.job()))));
  }
  if (changes.empty()) {
    return "updates nothing";
  }
  return absl::StrJoin(changes, " and ");
}

// How often a fight is told to the party fighting it. Ten times a second: a
// bar and a damage number are watched, not aimed at.
constexpr std::chrono::milliseconds kFightPublishInterval(100);

// The fight's own state as the wire spells it. Only the three it can be in
// while it runs; the ways of being over ride FightEnded.
FightState::Stage StageOf(PartyFightState state) {
  switch (state) {
    case PartyFightState::kCountdown:
      return FightState::COUNTDOWN;
    case PartyFightState::kPhaseGap:
      return FightState::PHASE_GAP;
    default:
      return FightState::FIGHTING;
  }
}

FightEnded::Outcome OutcomeOf(PartyFightState state) {
  switch (state) {
    case PartyFightState::kWon:
      return FightEnded::CLEARED;
    case PartyFightState::kTimedOut:
      return FightEnded::TIMED_OUT;
    default:
      return FightEnded::ABANDONED;
  }
}

// How a finished fight reads in the log.
std::string Became(PartyFightState state) {
  switch (state) {
    case PartyFightState::kWon:
      return "cleared";
    case PartyFightState::kTimedOut:
      return "ran out of time on";
    default:
      return "abandoned";
  }
}

// What a lobby message asked for, as a line for the log.
std::string AskedFor(const ClientMessage& message) {
  switch (message.kind_case()) {
    case ClientMessage::kCreateParty:
      return "creates a party";
    case ClientMessage::kJoinParty:
      return absl::StrCat("joins party ", message.join_party().party_id());
    case ClientMessage::kLeaveParty:
      return "leaves the party";
    case ClientMessage::kStartFight:
      return absl::StrCat("starts ", message.start_fight().boss_key(),
                          " difficulty ",
                          message.start_fight().difficulty_index());
    case ClientMessage::kSetReady:
      return message.set_ready().ready() ? "is ready" : "is not ready";
    case ClientMessage::kKickMember:
      return absl::StrCat("kicks ", message.kick_member().account_id());
    case ClientMessage::kPromoteMember:
      return absl::StrCat("promotes ", message.promote_member().account_id());
    default:
      return "sends something unknown";
  }
}

}  // namespace

Server::Server(Socket listener, const std::map<std::string, Boss>& bosses,
               const std::map<std::string, Mob>& mobs, unsigned int seed)
    : listener_(std::move(listener)),
      bosses_(&bosses),
      mobs_(&mobs),
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

  // Ahead of the reads, so a report that arrives in the same pass as the end
  // of the count-in lands rather than falling into a fight that has not
  // started yet.
  StepFights(now);
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
  PublishFights(now);
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
    // A connection that never said hello is timed out like any other: a
    // socket opened and left silent is the one way a session could otherwise
    // be held forever.
    if (session->socket.valid() &&
        now - session->last_heard > kSessionTimeout) {
      LOG(INFO) << "Session " << session->id << " went quiet";
      session->socket.Close();
    }
    if (!session->socket.valid() && !session->account_id.empty()) {
      LOG(INFO) << Describe(*session) << " disconnected";
      PartyFight* fight = FightOf(session->account_id);
      if (fight != nullptr) {
        fight->Disconnect(session->account_id);
      }
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

void Server::OpenFight(const std::string& account_id,
                       const StartFight& request) {
  Party party = lobby_.StateFor(account_id);
  std::map<std::string, Boss>::const_iterator boss =
      bosses_->find(request.boss_key());
  if (party.id().empty() || boss == bosses_->end()) {
    return;
  }
  fights_[party.id()] =
      std::make_unique<PartyFight>(request.boss_key(), boss->second,
                                   request.difficulty_index(), *mobs_, party);
  // The first state a client sees is how it learns the fight has begun, so it
  // goes out now rather than on the next broadcast beat.
  PublishFight(*fights_[party.id()]);
}

PartyFight* Server::FightOf(const std::string& account_id) {
  std::string party_id = lobby_.StateFor(account_id).id();
  if (party_id.empty()) {
    return nullptr;
  }
  std::map<std::string, std::unique_ptr<PartyFight>>::iterator found =
      fights_.find(party_id);
  return found == fights_.end() ? nullptr : found->second.get();
}

void Server::HandleFightUpdate(Session& session, const FightUpdate& update) {
  PartyFight* fight = FightOf(session.account_id);
  if (fight != nullptr) {
    fight->Report(session.account_id, update);
  }
}

void Server::StepFights(std::chrono::steady_clock::time_point now) {
  double dt = std::chrono::duration<double>(now - stepped_at_).count();
  // The first pass has no last one to measure from.
  if (stepped_at_ == std::chrono::steady_clock::time_point()) {
    dt = 0.0;
  }
  stepped_at_ = now;
  for (std::pair<const std::string, std::unique_ptr<PartyFight>>& entry :
       fights_) {
    entry.second->Advance(dt);
  }
}

void Server::PublishFights(std::chrono::steady_clock::time_point now) {
  bool beat = now >= publish_fights_at_;
  if (beat) {
    publish_fights_at_ = now + kFightPublishInterval;
  }
  std::vector<std::string> finished;
  for (std::pair<const std::string, std::unique_ptr<PartyFight>>& entry :
       fights_) {
    if (beat) {
      PublishFight(*entry.second);
    }
    if (entry.second->done()) {
      finished.push_back(entry.first);
    }
  }
  for (const std::string& party_id : finished) {
    CloseFight(party_id, *fights_[party_id]);
    fights_.erase(party_id);
  }
}

void Server::PublishFight(PartyFight& fight) {
  ServerMessage message;
  FightState* state = message.mutable_fight_state();
  state->set_boss_key(fight.boss_key());
  state->set_difficulty_index(fight.difficulty_index());
  state->set_stage(StageOf(fight.state()));
  state->set_phase(fight.phase());
  state->set_seconds_left(fight.seconds_left());
  state->set_countdown_left(fight.countdown_left());
  for (double hp : fight.hp_fractions()) {
    state->add_hp_fractions(hp);
  }
  for (const FightPlayer& player : fight.players()) {
    FightPlayerState* drawn = state->add_players();
    drawn->set_account_id(player.account_id);
    drawn->set_name(player.name);
    drawn->set_spot(player.spot);
    drawn->set_present(player.present);
    drawn->set_attack_name(player.attack_name);
    drawn->set_attack_fraction(player.attack_fraction);
    for (const FightDamage& line : player.lines) {
      *drawn->add_lines() = line;
    }
  }
  for (const FightPlayer& player : fight.players()) {
    Session* session = FindSession(player.account_id);
    if (session != nullptr && !session->closing) {
      Send(*session, message);
    }
  }
  fight.TakeLines();
}

void Server::CloseFight(const std::string& party_id, const PartyFight& fight) {
  LOG(INFO) << "Party " << party_id << " " << Became(fight.state()) << " "
            << fight.boss_key();
  ServerMessage message;
  FightEnded* ended = message.mutable_fight_ended();
  ended->set_outcome(OutcomeOf(fight.state()));
  ended->set_share_count(fight.share_count());
  for (const FightPlayer& player : fight.players()) {
    Session* session = FindSession(player.account_id);
    if (player.present && session != nullptr && !session->closing) {
      Send(*session, message);
    }
  }
  lobby_.FinishFight(party_id);
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
  // After the states, so a player reads what happened to them against the
  // party they are in now rather than the one they were in.
  for (const LobbyEvent& event : lobby_.TakeEvents()) {
    Session* session = FindSession(event.account_id);
    if (session == nullptr) {
      continue;
    }
    ServerMessage message;
    *message.mutable_party_event() = event.event;
    Send(*session, message);
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
    LOG(INFO) << "Session " << session->id << " connected";
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
    case ClientMessage::kFightUpdate:
      // Ten of these a second per player. Nothing is logged for them.
      HandleFightUpdate(session, message.fight_update());
      return;
    case ClientMessage::KIND_NOT_SET:
      Reject(session, Rejected::REASON_MALFORMED, "Empty message.");
      return;
    default:
      HandleLobby(session, message);
      return;
  }
}

std::string Server::Describe(const Session& session) const {
  if (session.account_id.empty()) {
    return absl::StrCat("Session ", session.id);
  }
  return absl::StrCat("Session ", session.id, " ", session.player.name(), " (",
                      session.account_id, ")");
}

void Server::SetPlayer(Session& session, const PlayerInfo& player) {
  session.player = player;
  session.player.set_account_id(session.account_id);
  session.player.set_name(DisplayName(player.name()));
}

void Server::HandleLobby(Session& session, const ClientMessage& message) {
  if (message.kind_case() == ClientMessage::kUpdatePlayer) {
    // Logged before the change lands, so a rename names both sides of it.
    LOG(INFO) << Describe(session) << " "
              << Became(session.player, message.update_player().player());
    SetPlayer(session, message.update_player().player());
    lobby_.UpdatePlayer(session.player);
    return;
  }
  std::string asked = AskedFor(message);
  LobbyResult result;
  switch (message.kind_case()) {
    case ClientMessage::kCreateParty:
      result = lobby_.Create(session.player);
      break;
    case ClientMessage::kJoinParty:
      result = lobby_.Join(session.player, message.join_party().party_id());
      break;
    case ClientMessage::kLeaveParty:
      result = lobby_.Leave(session.account_id);
      break;
    case ClientMessage::kStartFight:
      result =
          lobby_.Start(session.account_id, message.start_fight(), WallNow());
      break;
    case ClientMessage::kSetReady:
      result = lobby_.SetReady(session.account_id, message.set_ready().ready());
      break;
    case ClientMessage::kKickMember:
      result =
          lobby_.Kick(session.account_id, message.kick_member().account_id());
      break;
    case ClientMessage::kPromoteMember:
      result = lobby_.Promote(session.account_id,
                              message.promote_member().account_id());
      break;
    default:
      Reject(session, Rejected::REASON_MALFORMED, "Unknown message.");
      return;
  }
  if (!result.ok) {
    LOG(INFO) << Describe(session) << " " << asked << ": refused, "
              << result.message;
    Refuse(session, result.reason, result.message);
    return;
  }
  LOG(INFO) << Describe(session) << " " << asked;
  if (message.kind_case() == ClientMessage::kStartFight) {
    OpenFight(session.account_id, message.start_fight());
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
  LOG(INFO) << Describe(session) << " arrived at level "
            << session.player.level();
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
  LOG(INFO) << Describe(session) << " turned away: " << message;
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
