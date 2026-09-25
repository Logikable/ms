#include "src/multiplayer/party_fight.h"

#include <gtest/gtest.h>

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <thread>

#include "server/test_server.h"
#include "src/combat/boss_timing.h"
#include "src/combat/fight_authority.h"
#include "src/multiplayer/client.h"
#include "src/protos/multiplayer.pb.h"

namespace ms {
namespace {

using ::std::chrono::milliseconds;

// How long a round trip may take before the test treats it as lost. Generous:
// passing tests never wait this long, and the suite runs these alongside
// fifteen other targets on a sixteen-core machine.
constexpr milliseconds kPatience(15000);

PlayerInfo Player(const std::string& name) {
  PlayerInfo player;
  player.set_name(name);
  player.set_level(140);
  return player;
}

// One client, its account, and the fight it's following.
struct Peer {
  explicit Peer(int port) : client("127.0.0.1", port), fight(client) {
  }

  MultiplayerClient client;
  PartyFightAuthority fight;
  std::string account_id;
};

// Two clients in a party, one leading, both in the fight it started: the whole
// protocol driven from the game's side.
class PartyFightTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(server_.Start());
    leader_ = std::make_unique<Peer>(server_.port());
    member_ = std::make_unique<Peer>(server_.port());
    leader_->client.Start(Player("Dagger"), "");
    member_->client.Start(Player("Wand"), "");
    ASSERT_TRUE(Await([&]() {
      leader_->account_id = leader_->client.Snapshot().account_id;
      member_->account_id = member_->client.Snapshot().account_id;
      return !leader_->account_id.empty() && !member_->account_id.empty();
    }));

    leader_->client.CreateParty();
    ASSERT_TRUE(Await(
        [&]() { return !leader_->client.Snapshot().party.id().empty(); }));
    member_->client.JoinParty(leader_->client.Snapshot().party.id());
    ASSERT_TRUE(Await([&]() {
      return leader_->client.Snapshot().party.members_size() == 2;
    }));
    member_->client.SetReady(true);
    ASSERT_TRUE(Await([&]() {
      const Party& party = leader_->client.Snapshot().party;
      return party.members_size() == 2 && party.members(1).ready();
    }));
    leader_->client.StartFight("zakum", 0, PARTY_MODE_SHARED);
    // The countdown is three real seconds. These tests are about what crosses
    // the network, so the server is stepped past it instead of waiting; four
    // tests waiting it out took a quarter of the whole suite's time.
    ASSERT_TRUE(Await([&]() {
      SharedFight fight;
      return leader_->fight.Fetch(fight) &&
             fight.state == BossRunState::kCountdown;
    }));
    server_.SkipAhead(
        milliseconds(static_cast<int>(1000 * kBossCountdownSeconds) + 100));
  }

  // Runs both sides until `ready`, feeding each client's fight the server's
  // messages along the way.
  bool Await(const std::function<bool()>& ready) {
    std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + kPatience;
    while (std::chrono::steady_clock::now() < deadline) {
      leader_->fight.Advance(leader_->account_id);
      member_->fight.Advance(member_->account_id);
      if (ready()) {
        return true;
      }
      std::this_thread::sleep_for(milliseconds(2));
    }
    return false;
  }

  TestServer server_;
  std::unique_ptr<Peer> leader_;
  std::unique_ptr<Peer> member_;
};

TEST_F(PartyFightTest, BothEndsLearnTheFightHasBegun) {
  ASSERT_TRUE(Await([&]() {
    return leader_->fight.fighting() && member_->fight.fighting();
  }));
  EXPECT_EQ(member_->fight.boss_key(), "zakum");
  EXPECT_EQ(member_->fight.difficulty_index(), 0);

  SharedFight fight;
  ASSERT_TRUE(member_->fight.Fetch(fight));
  ASSERT_EQ(fight.players.size(), 2u);
  // Each side knows which player it is, and reads the other's name.
  EXPECT_EQ(fight.self, 1);
  EXPECT_EQ(fight.players[0].name, "Dagger");
  EXPECT_EQ(fight.hp_fractions.size(), 1u);
  EXPECT_EQ(fight.hp_fractions[0], 1.0);

  // A run waits on a fight nobody has been told about.
  Peer alone(server_.port());
  SharedFight nothing;
  EXPECT_FALSE(alone.fight.Fetch(nothing));
}

TEST_F(PartyFightTest, WhatOneLandsTheOtherReadsBack) {
  ASSERT_TRUE(Await([&]() {
    SharedFight fight;
    return leader_->fight.Fetch(fight) &&
           fight.state == BossRunState::kFighting;
  }));

  SharedLine line;
  line.slot = 0;
  line.event = 4;
  line.source.origin = DamageOrigin::kOwnClock;
  line.source.index = 2;
  line.damage = kTestMobHp / 4;
  line.crit = true;
  leader_->fight.Report({0, {line}, 0, "Blizzard", 0.5, 0.0, 3});

  SharedFight seen;
  ASSERT_TRUE(Await([&]() {
    SharedFight fight;
    if (!member_->fight.Fetch(fight) || fight.lines.empty()) {
      return false;
    }
    seen = fight;
    return true;
  }));
  ASSERT_EQ(seen.lines.size(), 1u);
  EXPECT_EQ(seen.lines[0].owner, 0);
  EXPECT_EQ(seen.lines[0].slot, 0);
  EXPECT_EQ(seen.lines[0].damage, kTestMobHp / 4);
  EXPECT_TRUE(seen.lines[0].crit);
  EXPECT_EQ(seen.lines[0].source.origin, DamageOrigin::kOwnClock);
  EXPECT_EQ(seen.lines[0].source.index, 2);
  EXPECT_EQ(seen.players[0].attack_name, "Blizzard");
  EXPECT_EQ(seen.players[0].buff_count, 3);
  EXPECT_EQ(seen.hp_fractions[0], 0.75);

  // Read once: the run drew them, and they expire on its own screen.
  SharedFight again;
  ASSERT_TRUE(member_->fight.Fetch(again));
  EXPECT_TRUE(again.lines.empty());
}

TEST_F(PartyFightTest, AClearIsToldToEveryone) {
  ASSERT_TRUE(Await([&]() {
    SharedFight fight;
    return leader_->fight.Fetch(fight) &&
           fight.state == BossRunState::kFighting;
  }));

  SharedLine line;
  line.slot = 0;
  line.damage = kTestMobHp;
  leader_->fight.Report({0,
                         {line},
                         0,
                         "Blizzard",
                         0.5,
                         0.0,
                         0,
                         {{"Blizzard", 1.0 * kTestMobHp, 1, 1}}});

  SharedFight ended;
  ASSERT_TRUE(Await([&]() {
    SharedFight fight;
    if (!member_->fight.Fetch(fight) || fight.state != BossRunState::kWon) {
      return false;
    }
    ended = fight;
    return true;
  }));
  EXPECT_EQ(ended.share_count, 2);
  // Everyone's table, including the player who never reported.
  ASSERT_EQ(ended.breakdowns.size(), 2u);
  EXPECT_EQ(ended.breakdowns[0].account_id, leader_->account_id);
  EXPECT_EQ(ended.breakdowns[0].name, "Dagger");
  ASSERT_EQ(ended.breakdowns[0].rows.size(), 1u);
  EXPECT_EQ(ended.breakdowns[0].rows[0].skill, "Blizzard");
  EXPECT_EQ(ended.breakdowns[0].rows[0].damage, kTestMobHp);
  EXPECT_EQ(ended.breakdowns[0].rows[0].casts, 1);
  EXPECT_TRUE(ended.breakdowns[1].rows.empty());
}

// The server deals the drops, so a certain drop always falls, to exactly one of
// them: never both and never neither.
TEST_F(PartyFightTest, ACertainDropFallsToOneOfThem) {
  ASSERT_TRUE(Await([&]() {
    SharedFight fight;
    return leader_->fight.Fetch(fight) &&
           fight.state == BossRunState::kFighting;
  }));

  SharedLine line;
  line.slot = 0;
  line.damage = kTestMobHp;
  leader_->fight.Report({0, {line}, 0, "Blizzard", 0.5});

  SharedFight for_leader;
  SharedFight for_member;
  ASSERT_TRUE(Await([&]() {
    SharedFight fight;
    if (leader_->fight.Fetch(fight) && fight.state == BossRunState::kWon) {
      for_leader = fight;
    }
    if (member_->fight.Fetch(fight) && fight.state == BossRunState::kWon) {
      for_member = fight;
    }
    return for_leader.state == BossRunState::kWon &&
           for_member.state == BossRunState::kWon;
  }));

  ASSERT_EQ(for_leader.awards.size() + for_member.awards.size(), 1u);
  const SharedAward& won =
      for_leader.awards.empty() ? for_member.awards[0] : for_leader.awards[0];
  EXPECT_EQ(won.drop.item(), kTestDrop);
  EXPECT_EQ(won.count, 1);
}

TEST_F(PartyFightTest, WalkingOutEndsItForWhoeverIsLeft) {
  ASSERT_TRUE(Await([&]() {
    SharedFight fight;
    return member_->fight.Fetch(fight) &&
           fight.state == BossRunState::kFighting;
  }));

  member_->client.LeaveFight();
  SharedFight seen;
  ASSERT_TRUE(Await([&]() {
    SharedFight fight;
    if (!leader_->fight.Fetch(fight) || fight.players.size() < 2 ||
        fight.players[1].present) {
      return false;
    }
    seen = fight;
    return true;
  }));
  // The fight continues for the one still in it.
  EXPECT_EQ(seen.state, BossRunState::kFighting);
  EXPECT_TRUE(seen.players[0].present);
}

}  // namespace
}  // namespace ms
