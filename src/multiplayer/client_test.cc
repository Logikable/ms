#include "src/multiplayer/client.h"

#include <gtest/gtest.h>

#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <thread>

#include "server/test_server.h"
#include "src/net/socket.h"
#include "src/protos/multiplayer.pb.h"

namespace ms {
namespace {

using ::std::chrono::milliseconds;

constexpr milliseconds kPatience(4000);

PlayerInfo Player(const std::string& name, const std::string& account_id = "") {
  PlayerInfo player;
  player.set_name(name);
  player.set_level(140);
  player.set_account_id(account_id);
  return player;
}

// Waits for the client to reach the state the test is after.
bool WaitFor(const MultiplayerClient& client,
             const std::function<bool(const MultiplayerSnapshot&)>& ready) {
  std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now() + kPatience;
  while (std::chrono::steady_clock::now() < deadline) {
    if (ready(client.Snapshot())) {
      return true;
    }
    std::this_thread::sleep_for(milliseconds(2));
  }
  return false;
}

bool WaitUntilConnected(const MultiplayerClient& client) {
  return WaitFor(client, [](const MultiplayerSnapshot& snapshot) {
    return snapshot.state == ConnectionState::kConnected;
  });
}

class ClientTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(server_.Start());
  }

  TestServer server_;
};

TEST_F(ClientTest, ConnectsAndIsWelcomed) {
  MultiplayerClient client("127.0.0.1", server_.port());
  client.Start(Player("Dagger"), "");

  ASSERT_TRUE(WaitUntilConnected(client));
  MultiplayerSnapshot snapshot = client.Snapshot();
  EXPECT_FALSE(snapshot.account_id.empty());
  EXPECT_FALSE(snapshot.token.empty());
  EXPECT_TRUE(snapshot.message.empty());
  EXPECT_EQ(snapshot.parties.parties_size(), 0);
}

TEST_F(ClientTest, ComesBackAsTheSameAccount) {
  std::string account;
  std::string token;
  {
    MultiplayerClient first("127.0.0.1", server_.port());
    first.Start(Player("Dagger"), "");
    ASSERT_TRUE(WaitUntilConnected(first));
    account = first.Snapshot().account_id;
    token = first.Snapshot().token;
  }

  MultiplayerClient second("127.0.0.1", server_.port());
  second.Start(Player("Dagger", account), token);
  ASSERT_TRUE(WaitUntilConnected(second));
  EXPECT_EQ(second.Snapshot().account_id, account);
}

TEST_F(ClientTest, MakesAPartyEveryoneCanSee) {
  MultiplayerClient host("127.0.0.1", server_.port());
  host.Start(Player("Dagger"), "");
  ASSERT_TRUE(WaitUntilConnected(host));
  MultiplayerClient guest("127.0.0.1", server_.port());
  guest.Start(Player("Wand"), "");
  ASSERT_TRUE(WaitUntilConnected(guest));

  host.CreateParty();
  ASSERT_TRUE(WaitFor(host, [](const MultiplayerSnapshot& snapshot) {
    return !snapshot.party.id().empty();
  }));

  ASSERT_TRUE(WaitFor(guest, [](const MultiplayerSnapshot& snapshot) {
    return snapshot.parties.parties_size() == 1;
  }));
  std::string party_id = guest.Snapshot().parties.parties(0).id();
  guest.JoinParty(party_id);
  ASSERT_TRUE(WaitFor(guest, [](const MultiplayerSnapshot& snapshot) {
    return snapshot.party.members_size() == 2;
  }));

  guest.LeaveParty();
  ASSERT_TRUE(WaitFor(guest, [](const MultiplayerSnapshot& snapshot) {
    return snapshot.party.id().empty();
  }));
  EXPECT_TRUE(WaitFor(host, [](const MultiplayerSnapshot& snapshot) {
    return snapshot.party.members_size() == 1;
  }));
}

TEST_F(ClientTest, ShowsAPartyWhoItsMembersAreNow) {
  MultiplayerClient host("127.0.0.1", server_.port());
  host.Start(Player("Dagger"), "");
  ASSERT_TRUE(WaitUntilConnected(host));
  MultiplayerClient guest("127.0.0.1", server_.port());
  guest.Start(Player("Wand"), "");
  ASSERT_TRUE(WaitUntilConnected(guest));

  host.CreateParty();
  ASSERT_TRUE(WaitFor(guest, [](const MultiplayerSnapshot& snapshot) {
    return snapshot.parties.parties_size() == 1;
  }));
  guest.JoinParty(guest.Snapshot().parties.parties(0).id());
  ASSERT_TRUE(WaitFor(host, [](const MultiplayerSnapshot& snapshot) {
    return snapshot.party.members_size() == 2;
  }));

  PlayerInfo levelled = Player("Wand", guest.Snapshot().account_id);
  levelled.set_level(200);
  guest.SetPlayer(levelled);
  EXPECT_TRUE(WaitFor(host, [](const MultiplayerSnapshot& snapshot) {
    return snapshot.party.members_size() == 2 &&
           snapshot.party.members(1).player().level() == 200;
  }));
}

TEST_F(ClientTest, PassesOnWhatTheServerWouldNotDo) {
  MultiplayerClient client("127.0.0.1", server_.port());
  client.Start(Player("Dagger"), "");
  ASSERT_TRUE(WaitUntilConnected(client));

  // Leaving a party the player is not in.
  client.LeaveParty();
  ASSERT_TRUE(WaitFor(client, [](const MultiplayerSnapshot& snapshot) {
    return snapshot.notice_serial == 1;
  }));
  EXPECT_FALSE(client.Snapshot().notice.empty());
  EXPECT_TRUE(client.Snapshot().notice_is_refusal);
  EXPECT_TRUE(client.Snapshot().party.id().empty());
}

TEST_F(ClientTest, KeepsTryingWhenNobodyAnswers) {
  // A port that was listening and is not any more.
  std::optional<Socket> listener = Listen(0);
  ASSERT_TRUE(listener.has_value());
  int dead_port = LocalPort(*listener);
  listener->Close();

  MultiplayerClient client("127.0.0.1", dead_port);
  client.Start(Player("Dagger"), "");
  ASSERT_TRUE(WaitFor(client, [](const MultiplayerSnapshot& snapshot) {
    return snapshot.state == ConnectionState::kUnavailable;
  }));
  EXPECT_FALSE(client.Snapshot().message.empty());
}

TEST_F(ClientTest, StopsWhenToldToUpdate) {
  MultiplayerClient client("127.0.0.1", server_.port(),
                           kMultiplayerVersion + 1);
  client.Start(Player("Dagger"), "");

  ASSERT_TRUE(WaitFor(client, [](const MultiplayerSnapshot& snapshot) {
    return snapshot.state == ConnectionState::kRefused;
  }));
  EXPECT_FALSE(client.Snapshot().message.empty());
  // The server's own version comes back with the refusal, which is what
  // names the mismatch -- the deploy check has nothing else to report.
  EXPECT_EQ(client.Snapshot().server_protocol_version, kMultiplayerVersion);
  // It stays refused rather than going round again.
  std::this_thread::sleep_for(milliseconds(100));
  EXPECT_EQ(client.Snapshot().state, ConnectionState::kRefused);
}

TEST_F(ClientTest, SaysNothingAboutAVersionUntilTheServerRefuses) {
  MultiplayerClient client("127.0.0.1", server_.port());
  client.Start(Player("Dagger"), "");

  ASSERT_TRUE(WaitFor(client, [](const MultiplayerSnapshot& snapshot) {
    return snapshot.state == ConnectionState::kConnected;
  }));
  EXPECT_EQ(client.Snapshot().server_protocol_version, 0)
      << "a welcome carries no version to report";
}

}  // namespace
}  // namespace ms
