#include "server/server.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/log/log_entry.h"
#include "absl/log/log_sink.h"
#include "absl/log/log_sink_registry.h"
#include "src/multiplayer/protocol.h"
#include "src/net/socket.h"
#include "src/protos/boss.pb.h"
#include "src/protos/character.pb.h"
#include "src/protos/mob.pb.h"
#include "src/protos/multiplayer.pb.h"

namespace ms {
namespace {

using ::std::chrono::milliseconds;
using ::std::chrono::seconds;

// How long a test waits for something the server has already been asked for.
constexpr milliseconds kPatience(2000);

// One client of the server under test: it sends whole messages and reads
// whatever has come back, without ever blocking.
class TestClient {
 public:
  bool Open(int port) {
    std::optional<Socket> socket = Connect("127.0.0.1", port, kPatience);
    if (!socket.has_value()) {
      return false;
    }
    socket_ = std::move(*socket);
    return true;
  }

  void Send(const ClientMessage& message) {
    ASSERT_TRUE(Encode(message, outgoing_));
    while (!outgoing_.empty()) {
      ASSERT_NE(Write(socket_, outgoing_), IoStatus::kError);
    }
  }

  void SayHello(const std::string& name, const std::string& account_id = "",
                const std::string& token = "",
                int version = kMultiplayerVersion) {
    ClientMessage message;
    message.mutable_hello()->set_protocol_version(version);
    message.mutable_hello()->set_token(token);
    message.mutable_hello()->mutable_player()->set_account_id(account_id);
    message.mutable_hello()->mutable_player()->set_name(name);
    Send(message);
  }

  // The next message the server sent, if one has arrived whole.
  bool Take(ServerMessage& message) {
    IoStatus status = Read(socket_, incoming_);
    if (status == IoStatus::kClosed) {
      closed_ = true;
    }
    return Decode(incoming_, message) == DecodeStatus::kOk;
  }

  bool closed() const {
    return closed_;
  }

 private:
  Socket socket_;
  std::string incoming_;
  std::string outgoing_;
  bool closed_ = false;
};

// One fight for the parties in these tests to name.
// One fight, of one monster with 1000 HP, in a room with three places to
// stand.
constexpr int64_t kMobHp = 1000;

std::map<std::string, Boss> Bosses() {
  std::map<std::string, Boss> bosses;
  Boss& zakum = bosses["zakum"];
  zakum.set_name("Zakum");
  BossDifficulty* normal = zakum.add_difficulties();
  normal->set_name("Normal");
  normal->set_time_limit_seconds(300);
  BossPhase* phase = normal->add_phases();
  Spawn* spawn = phase->add_spawns();
  spawn->set_mob("zakum");
  spawn->add_spots()->set_x(1);
  for (int i = 0; i < 3; ++i) {
    phase->add_player_spots()->set_x(i);
  }
  return bosses;
}

std::map<std::string, Mob> Mobs() {
  std::map<std::string, Mob> mobs;
  mobs["zakum"].set_name("Zakum");
  mobs["zakum"].set_max_hp(kMobHp);
  return mobs;
}

// What a client asks for when it wants a party of its own.
ClientMessage CreatePartyMessage() {
  ClientMessage message;
  message.mutable_create_party();
  return message;
}

// Collects what the server logged, so a test can read it back.
class LogSpy : public absl::LogSink {
 public:
  LogSpy() {
    absl::AddLogSink(this);
  }
  ~LogSpy() override {
    absl::RemoveLogSink(this);
  }

  void Send(const absl::LogEntry& entry) override {
    lines_ += std::string(entry.text_message());
    lines_ += '\n';
  }

  bool Saw(const std::string& text) const {
    return lines_.find(text) != std::string::npos;
  }

 private:
  std::string lines_;
};

class ServerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(StartSockets());
    std::optional<Socket> listener = Listen(0);
    ASSERT_TRUE(listener.has_value());
    port_ = LocalPort(*listener);
    server_ = std::make_unique<Server>(std::move(*listener), bosses_, mobs_, 7);
  }

  // Runs the server until `ready` says the test can go on. False means it
  // never did.
  bool StepUntil(const std::function<bool()>& ready) {
    std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + kPatience;
    while (std::chrono::steady_clock::now() < deadline) {
      server_->Step(now_, milliseconds(5));
      if (ready()) {
        return true;
      }
    }
    return false;
  }

  // Runs the server until `client` has a message, and returns it.
  ServerMessage Await(TestClient& client) {
    ServerMessage message;
    EXPECT_TRUE(StepUntil([&]() { return client.Take(message); }));
    return message;
  }

  // Runs the server until `client` has a message of `kind`, passing over
  // whatever else arrives first.
  ServerMessage AwaitKind(TestClient& client, ServerMessage::KindCase kind) {
    ServerMessage message;
    EXPECT_TRUE(StepUntil(
        [&]() { return client.Take(message) && message.kind_case() == kind; }));
    return message;
  }

  // A client that has been welcomed and has read the list that came with it,
  // so that the next list it sees is one something changed.
  std::unique_ptr<TestClient> Greeted(const std::string& name,
                                      Welcome* welcome) {
    std::unique_ptr<TestClient> client = std::make_unique<TestClient>();
    EXPECT_TRUE(client->Open(port_));
    client->SayHello(name);
    *welcome = AwaitKind(*client, ServerMessage::kWelcome).welcome();
    AwaitKind(*client, ServerMessage::kPartyList);
    return client;
  }

  std::map<std::string, Boss> bosses_ = Bosses();
  std::map<std::string, Mob> mobs_ = Mobs();
  int port_ = 0;
  std::unique_ptr<Server> server_;
  // The clock the server is stepped against, moved by the tests that care.
  std::chrono::steady_clock::time_point now_ = std::chrono::steady_clock::now();
};

TEST_F(ServerTest, WelcomesANewPlayer) {
  Welcome welcome;
  std::unique_ptr<TestClient> client = Greeted("Dagger", &welcome);

  EXPECT_FALSE(welcome.account_id().empty());
  EXPECT_FALSE(welcome.token().empty());
  EXPECT_EQ(server_->player_count(), 1);
}

TEST_F(ServerTest, KnowsAPlayerComingBack) {
  Welcome welcome;
  std::unique_ptr<TestClient> first = Greeted("Dagger", &welcome);

  TestClient second;
  ASSERT_TRUE(second.Open(port_));
  second.SayHello("Dagger", welcome.account_id(), welcome.token());
  ServerMessage message = Await(second);
  ASSERT_EQ(message.kind_case(), ServerMessage::kWelcome);
  EXPECT_EQ(message.welcome().account_id(), welcome.account_id());
}

TEST_F(ServerTest, AdoptsAnAccountItHasNeverSeen) {
  // What every client does after the server restarts.
  TestClient client;
  ASSERT_TRUE(client.Open(port_));
  client.SayHello("Dagger", "0123456789abcdef", "an-old-token");

  ServerMessage message = Await(client);
  ASSERT_EQ(message.kind_case(), ServerMessage::kWelcome);
  EXPECT_EQ(message.welcome().account_id(), "0123456789abcdef");
  EXPECT_EQ(message.welcome().token(), "an-old-token");
}

TEST_F(ServerTest, RefusesTheWrongTokenForAKnownAccount) {
  Welcome welcome;
  std::unique_ptr<TestClient> first = Greeted("Dagger", &welcome);

  TestClient impostor;
  ASSERT_TRUE(impostor.Open(port_));
  impostor.SayHello("Dagger", welcome.account_id(), "not-the-token");

  ServerMessage message = Await(impostor);
  ASSERT_EQ(message.kind_case(), ServerMessage::kRejected);
  EXPECT_EQ(message.rejected().reason(), Rejected::REASON_BAD_CREDENTIALS);
  EXPECT_EQ(server_->player_count(), 1);
}

TEST_F(ServerTest, TurnsAwayAnotherVersion) {
  TestClient client;
  ASSERT_TRUE(client.Open(port_));
  client.SayHello("Dagger", "", "", kMultiplayerVersion + 1);

  ServerMessage message = Await(client);
  ASSERT_EQ(message.kind_case(), ServerMessage::kRejected);
  EXPECT_EQ(message.rejected().reason(), Rejected::REASON_UPDATE_REQUIRED);
  EXPECT_EQ(message.rejected().server_protocol_version(), kMultiplayerVersion);
  EXPECT_FALSE(message.rejected().message().empty());

  // The connection goes with it.
  EXPECT_TRUE(StepUntil([&]() { return server_->session_count() == 0; }));
}

TEST_F(ServerTest, WantsHelloFirst) {
  TestClient client;
  ASSERT_TRUE(client.Open(port_));
  ClientMessage ping;
  ping.mutable_ping();
  client.Send(ping);

  ServerMessage message = Await(client);
  ASSERT_EQ(message.kind_case(), ServerMessage::kRejected);
  EXPECT_EQ(message.rejected().reason(), Rejected::REASON_MALFORMED);
}

TEST_F(ServerTest, AnswersAHeartbeat) {
  Welcome welcome;
  std::unique_ptr<TestClient> client = Greeted("Dagger", &welcome);

  ClientMessage ping;
  ping.mutable_ping();
  client->Send(ping);
  EXPECT_EQ(AwaitKind(*client, ServerMessage::kPong).kind_case(),
            ServerMessage::kPong);
}

TEST_F(ServerTest, DropsASessionThatGoesQuiet) {
  Welcome welcome;
  std::unique_ptr<TestClient> client = Greeted("Dagger", &welcome);
  ASSERT_EQ(server_->session_count(), 1);

  now_ += kSessionTimeout + seconds(1);
  EXPECT_TRUE(StepUntil([&]() { return server_->session_count() == 0; }));
}

TEST_F(ServerTest, DropsAConnectionThatNeverSpoke) {
  TestClient client;
  ASSERT_TRUE(client.Open(port_));
  ASSERT_TRUE(StepUntil([&]() { return server_->session_count() == 1; }));

  // Nothing was ever said on it, so nothing but the clock can end it.
  now_ += kSessionTimeout + seconds(1);
  EXPECT_TRUE(StepUntil([&]() { return server_->session_count() == 0; }));
}

TEST_F(ServerTest, SendsEveryoneAwayWhenDraining) {
  Welcome welcome;
  std::unique_ptr<TestClient> client = Greeted("Dagger", &welcome);

  server_->Drain();
  ServerMessage message = AwaitKind(*client, ServerMessage::kRejected);
  EXPECT_EQ(message.rejected().reason(), Rejected::REASON_MAINTENANCE);
  EXPECT_TRUE(StepUntil([&]() { return server_->drained(); }));

  // Nobody new is taken on while it is going down.
  TestClient late;
  EXPECT_FALSE(late.Open(port_));
}

TEST_F(ServerTest, ShowsTheListToAPlayerArriving) {
  TestClient client;
  ASSERT_TRUE(client.Open(port_));
  client.SayHello("Dagger");

  // The welcome is followed by what there is to join, so a player arriving
  // has the lobby without asking for it.
  AwaitKind(client, ServerMessage::kWelcome);
  ServerMessage listing = AwaitKind(client, ServerMessage::kPartyList);
  EXPECT_EQ(listing.party_list().parties_size(), 0);
}

TEST_F(ServerTest, PutsAPartyInFrontOfEverybody) {
  Welcome first_welcome;
  std::unique_ptr<TestClient> first = Greeted("Dagger", &first_welcome);
  Welcome second_welcome;
  std::unique_ptr<TestClient> second = Greeted("Wand", &second_welcome);

  first->Send(CreatePartyMessage());
  ServerMessage state = AwaitKind(*first, ServerMessage::kPartyState);
  ASSERT_EQ(state.party_state().party().members_size(), 1);
  EXPECT_EQ(state.party_state().party().members(0).player().name(), "Dagger");
  std::string party_id = state.party_state().party().id();

  ServerMessage listing = AwaitKind(*second, ServerMessage::kPartyList);
  ASSERT_EQ(listing.party_list().parties_size(), 1);
  EXPECT_EQ(listing.party_list().parties(0).id(), party_id);

  ClientMessage join;
  join.mutable_join_party()->set_party_id(party_id);
  second->Send(join);
  ServerMessage joined = AwaitKind(*second, ServerMessage::kPartyState);
  EXPECT_EQ(joined.party_state().party().members_size(), 2);
  // The one who made it is told as well.
  ServerMessage changed = AwaitKind(*first, ServerMessage::kPartyState);
  EXPECT_EQ(changed.party_state().party().members_size(), 2);
}

// Everything a player does reaches the log, named for who did it, whether the
// lobby allowed it or not.
TEST_F(ServerTest, LogsEveryAction) {
  LogSpy log;
  Welcome leader_welcome;
  std::unique_ptr<TestClient> leader = Greeted("Dagger", &leader_welcome);
  Welcome member_welcome;
  std::unique_ptr<TestClient> member = Greeted("Wand", &member_welcome);

  leader->Send(CreatePartyMessage());
  ServerMessage state = AwaitKind(*leader, ServerMessage::kPartyState);
  std::string party_id = state.party_state().party().id();

  ClientMessage join;
  join.mutable_join_party()->set_party_id(party_id);
  member->Send(join);
  AwaitKind(*member, ServerMessage::kPartyState);

  ClientMessage ready;
  ready.mutable_set_ready()->set_ready(true);
  member->Send(ready);
  AwaitKind(*member, ServerMessage::kPartyState);

  // A member cannot kick, so this one is refused and says so.
  ClientMessage kick;
  kick.mutable_kick_member()->set_account_id(leader_welcome.account_id());
  member->Send(kick);
  AwaitKind(*member, ServerMessage::kRefused);

  EXPECT_TRUE(log.Saw("connected"));
  EXPECT_TRUE(log.Saw("Dagger (" + leader_welcome.account_id() + ")"));
  EXPECT_TRUE(log.Saw("creates a party"));
  EXPECT_TRUE(log.Saw("joins party " + party_id));
  EXPECT_TRUE(log.Saw("is ready"));
  EXPECT_TRUE(log.Saw("kicks " + leader_welcome.account_id() + ": refused"));

  member.reset();
  EXPECT_TRUE(StepUntil([&]() {
    return log.Saw("Wand (" + member_welcome.account_id() + ") disconnected");
  }));
}

TEST_F(ServerTest, LogsWhatAnUpdateChanged) {
  LogSpy log;
  Welcome welcome;
  std::unique_ptr<TestClient> client = Greeted("Dagger", &welcome);

  ClientMessage renamed;
  PlayerInfo* player = renamed.mutable_update_player()->mutable_player();
  player->set_name("Blade");
  client->Send(renamed);
  EXPECT_TRUE(StepUntil([&]() {
    return log.Saw("Dagger (" + welcome.account_id() + ") is now named Blade");
  }));

  ClientMessage advanced;
  player = advanced.mutable_update_player()->mutable_player();
  player->set_name("Blade");
  player->set_level(60);
  player->set_job(JOB_ADVANCEMENT_CRUSADER);
  client->Send(advanced);
  EXPECT_TRUE(StepUntil(
      [&]() { return log.Saw("is now level 60 and advances to Crusader"); }));
}

TEST_F(ServerTest, RefusesAFightItCannotRun) {
  Welcome welcome;
  std::unique_ptr<TestClient> client = Greeted("Dagger", &welcome);
  client->Send(CreatePartyMessage());
  AwaitKind(*client, ServerMessage::kPartyState);

  ClientMessage start;
  start.mutable_start_fight()->set_boss_key("balrog");
  client->Send(start);

  ServerMessage refused = AwaitKind(*client, ServerMessage::kRefused);
  EXPECT_EQ(refused.refused().reason(), Refused::REASON_UNKNOWN_BOSS);
  EXPECT_FALSE(refused.refused().message().empty());
}

// A party of two in a fight, with both clients holding nothing unread.
class FightTest : public ServerTest {
 protected:
  void SetUp() override {
    ServerTest::SetUp();
    leader_ = Greeted("Dagger", &leader_welcome_);
    member_ = Greeted("Wand", &member_welcome_);
    leader_->Send(CreatePartyMessage());
    std::string party_id = AwaitKind(*leader_, ServerMessage::kPartyState)
                               .party_state()
                               .party()
                               .id();
    ClientMessage join;
    join.mutable_join_party()->set_party_id(party_id);
    member_->Send(join);
    AwaitKind(*member_, ServerMessage::kPartyState);
    ClientMessage ready;
    ready.mutable_set_ready()->set_ready(true);
    member_->Send(ready);
    AwaitKind(*member_, ServerMessage::kPartyState);

    ClientMessage start;
    start.mutable_start_fight()->set_boss_key("zakum");
    leader_->Send(start);
  }

  // Runs the server on a clock that keeps moving, which is what a fight needs:
  // its beats come round on the clock rather than on the sockets. Bounded, so
  // a fight cannot run the session timeout out from under its own clients.
  bool TickUntil(const std::function<bool()>& ready) {
    for (int pass = 0; pass < 400; ++pass) {
      now_ += milliseconds(20);
      server_->Step(now_, milliseconds(1));
      if (ready()) {
        return true;
      }
    }
    return false;
  }

  // The next fight state `client` is sent that `ready` accepts.
  FightState AwaitFight(TestClient& client,
                        const std::function<bool(const FightState&)>& ready) {
    FightState found;
    ServerMessage message;
    EXPECT_TRUE(TickUntil([&]() {
      if (!client.Take(message) ||
          message.kind_case() != ServerMessage::kFightState ||
          !ready(message.fight_state())) {
        return false;
      }
      found = message.fight_state();
      return true;
    }));
    return found;
  }

  // The next fight state at all, whatever it says.
  FightState AwaitFight(TestClient& client) {
    return AwaitFight(client, [](const FightState&) { return true; });
  }

  FightEnded AwaitEnd(TestClient& client) {
    FightEnded found;
    ServerMessage message;
    EXPECT_TRUE(TickUntil([&]() {
      if (!client.Take(message) ||
          message.kind_case() != ServerMessage::kFightEnded) {
        return false;
      }
      found = message.fight_ended();
      return true;
    }));
    return found;
  }

  // What one client says it landed on the only monster in the fight.
  ClientMessage Landed(int64_t damage) {
    ClientMessage message;
    FightUpdate* update = message.mutable_fight_update();
    update->set_attack_name("Blizzard");
    update->set_attack_fraction(0.25);
    FightDamage* line = update->add_lines();
    line->set_damage(damage);
    return message;
  }

  // Both clients past the count-in, holding nothing unread about the fight.
  void CountIn() {
    AwaitFight(*leader_, [](const FightState& state) {
      return state.stage() == FightState::FIGHTING;
    });
    AwaitFight(*member_, [](const FightState& state) {
      return state.stage() == FightState::FIGHTING;
    });
  }

  Welcome leader_welcome_;
  Welcome member_welcome_;
  std::unique_ptr<TestClient> leader_;
  std::unique_ptr<TestClient> member_;
};

TEST_F(FightTest, EveryoneIsToldTheFightHasBegun) {
  FightState state = AwaitFight(*member_);

  EXPECT_EQ(state.boss_key(), "zakum");
  ASSERT_EQ(state.hp_fractions_size(), 1);
  EXPECT_EQ(state.hp_fractions(0), 1.0);
  ASSERT_EQ(state.players_size(), 2);
  // One spot each, in the order the party holds them.
  EXPECT_EQ(state.players(0).spot(), 0);
  EXPECT_EQ(state.players(1).spot(), 1);
  EXPECT_TRUE(state.players(0).present());
  EXPECT_EQ(server_->fight_count(), 1);
}

TEST_F(FightTest, WhatOnePlayerLandsTheOtherWatches) {
  CountIn();
  leader_->Send(Landed(250));

  FightState state = AwaitFight(*member_, [](const FightState& drawn) {
    return drawn.players_size() > 0 && drawn.players(0).lines_size() > 0;
  });
  EXPECT_EQ(state.hp_fractions(0), 0.75);
  EXPECT_EQ(state.players(0).lines(0).damage(), 250);
  EXPECT_EQ(state.players(0).attack_name(), "Blizzard");

  // Passed on once, not on every broadcast after it.
  state = AwaitFight(*member_);
  EXPECT_EQ(state.players(0).lines_size(), 0);
  EXPECT_EQ(state.hp_fractions(0), 0.75);
}

TEST_F(FightTest, AClearEndsTheFightAndGivesThePartyBack) {
  CountIn();
  leader_->Send(Landed(kMobHp / 2));
  member_->Send(Landed(kMobHp / 2));

  FightEnded ended = AwaitEnd(*leader_);
  EXPECT_EQ(ended.outcome(), FightEnded::CLEARED);
  // Both of them were there when it started, so a clear is halves.
  EXPECT_EQ(ended.share_count(), 2);
  AwaitEnd(*member_);
  EXPECT_EQ(server_->fight_count(), 0);

  // The party is open again, and nobody's readiness carried over.
  ServerMessage listed = AwaitKind(*leader_, ServerMessage::kPartyList);
  ASSERT_EQ(listed.party_list().parties_size(), 1);
  ASSERT_EQ(listed.party_list().parties(0).members_size(), 2);
  for (const PartyMember& member : listed.party_list().parties(0).members()) {
    EXPECT_FALSE(member.ready());
  }
}

TEST_F(FightTest, TheLastClientOutAbandonsTheFight) {
  CountIn();
  member_.reset();
  EXPECT_TRUE(TickUntil([&]() { return server_->player_count() == 1; }));
  EXPECT_EQ(server_->fight_count(), 1);

  leader_.reset();
  EXPECT_TRUE(TickUntil([&]() { return server_->fight_count() == 0; }));
}

TEST_F(ServerTest, TellsAPlayerTheyWereRemoved) {
  Welcome leader_welcome;
  std::unique_ptr<TestClient> leader = Greeted("Dagger", &leader_welcome);
  Welcome member_welcome;
  std::unique_ptr<TestClient> member = Greeted("Wand", &member_welcome);

  leader->Send(CreatePartyMessage());
  ServerMessage made = AwaitKind(*leader, ServerMessage::kPartyState);
  ClientMessage join;
  join.mutable_join_party()->set_party_id(made.party_state().party().id());
  member->Send(join);
  AwaitKind(*member, ServerMessage::kPartyState);

  ClientMessage kick;
  kick.mutable_kick_member()->set_account_id(member_welcome.account_id());
  leader->Send(kick);

  // The state says they are in nothing; the event says why.
  ServerMessage state = AwaitKind(*member, ServerMessage::kPartyState);
  EXPECT_TRUE(state.party_state().party().id().empty());
  ServerMessage event = AwaitKind(*member, ServerMessage::kPartyEvent);
  EXPECT_EQ(event.party_event().kind(), PartyEvent::KICKED);
  EXPECT_FALSE(event.party_event().message().empty());
}

TEST_F(ServerTest, CutsALongNameDown) {
  TestClient client;
  ASSERT_TRUE(client.Open(port_));
  client.SayHello("A Very Long Name Indeed");
  ASSERT_EQ(AwaitKind(client, ServerMessage::kWelcome).kind_case(),
            ServerMessage::kWelcome);

  client.Send(CreatePartyMessage());
  ServerMessage state = AwaitKind(client, ServerMessage::kPartyState);
  ASSERT_EQ(state.party_state().party().members_size(), 1);
  EXPECT_EQ(state.party_state().party().members(0).player().name(),
            "A Very Long ");
}

TEST_F(ServerTest, TakesAPartyWithThePlayerWhoLeft) {
  Welcome first_welcome;
  std::unique_ptr<TestClient> first = Greeted("Dagger", &first_welcome);
  Welcome second_welcome;
  std::unique_ptr<TestClient> second = Greeted("Wand", &second_welcome);

  first->Send(CreatePartyMessage());
  ASSERT_EQ(
      AwaitKind(*second, ServerMessage::kPartyList).party_list().parties_size(),
      1);

  first.reset();
  ServerMessage listing = AwaitKind(*second, ServerMessage::kPartyList);
  EXPECT_EQ(listing.party_list().parties_size(), 0);
}

}  // namespace
}  // namespace ms
