#include "src/multiplayer/client.h"

#include <gtest/gtest.h>

#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <thread>
#include <vector>

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

// Waits until the client reaches the state the test wants.
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
  // A port that was listening and no longer is.
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

TEST_F(ClientTest, KeepsTryingWhenToldToUpdate) {
  MultiplayerClient client("127.0.0.1", server_.port(),
                           kMultiplayerVersion + 1);
  client.Start(Player("Dagger"), "");

  ASSERT_TRUE(WaitFor(client, [](const MultiplayerSnapshot& snapshot) {
    return snapshot.state == ConnectionState::kUnavailable;
  }));
  // The server's own version comes back with the refusal. It's what identifies
  // the mismatch, and the deploy check has nothing else to report.
  EXPECT_EQ(client.Snapshot().server_protocol_version, kMultiplayerVersion);
  // A mismatch doesn't end the connection: the client keeps waiting instead of
  // giving up, since either end could be updated.
  std::this_thread::sleep_for(milliseconds(100));
  EXPECT_EQ(client.Snapshot().state, ConnectionState::kUnavailable);
}

TEST_F(ClientTest, SaysWhichEndIsBehind) {
  MultiplayerClient ahead("127.0.0.1", server_.port(), kMultiplayerVersion - 1);
  ahead.Start(Player("Dagger"), "");
  ASSERT_TRUE(WaitFor(ahead, [](const MultiplayerSnapshot& snapshot) {
    return !snapshot.message.empty();
  }));
  EXPECT_EQ(ahead.Snapshot().message,
            "Update the game to play with others.\nClient: v" +
                std::to_string(kMultiplayerVersion - 1) + ", Server: v" +
                std::to_string(kMultiplayerVersion));

  MultiplayerClient behind("127.0.0.1", server_.port(),
                           kMultiplayerVersion + 1);
  behind.Start(Player("Wand"), "");
  ASSERT_TRUE(WaitFor(behind, [](const MultiplayerSnapshot& snapshot) {
    return !snapshot.message.empty();
  }));
  EXPECT_EQ(behind.Snapshot().message,
            "The server is running an older version. Trying again.\nClient: v" +
                std::to_string(kMultiplayerVersion + 1) + ", Server: v" +
                std::to_string(kMultiplayerVersion));
}

TEST_F(ClientTest, HealsWhenTheServerCatchesUp) {
  // The server is the one behind, which is what a skipped deploy looks like.
  // The client is correct and still can't connect.
  TestServer old_server(TestBosses(), TestMobs(), kMultiplayerVersion - 1);
  ASSERT_TRUE(old_server.Start());
  MultiplayerClient client("127.0.0.1", old_server.port());
  client.Start(Player("Dagger"), "");
  ASSERT_TRUE(WaitFor(client, [](const MultiplayerSnapshot& snapshot) {
    return snapshot.state == ConnectionState::kUnavailable &&
           snapshot.server_protocol_version == kMultiplayerVersion - 1;
  }));

  ASSERT_TRUE(old_server.RestartSpeaking(kMultiplayerVersion));
  client.Reconnect();
  EXPECT_TRUE(WaitUntilConnected(client))
      << "a deploy should let a client in without restarting the game";
}

TEST_F(ClientTest, KeepsTryingWhenTheTokenIsWrong) {
  MultiplayerClient first("127.0.0.1", server_.port());
  first.Start(Player("Dagger"), "");
  ASSERT_TRUE(WaitUntilConnected(first));
  std::string account = first.Snapshot().account_id;
  ASSERT_FALSE(account.empty());

  // The account is claimed with a token the server doesn't have for it. The
  // server's token map rejects it, and a restart clears that map, so the client
  // waits instead of giving up permanently.
  MultiplayerClient impostor("127.0.0.1", server_.port());
  impostor.Start(Player("Dagger", account), "not-the-token");
  ASSERT_TRUE(WaitFor(impostor, [](const MultiplayerSnapshot& snapshot) {
    return snapshot.state == ConnectionState::kUnavailable;
  }));
  EXPECT_FALSE(impostor.Snapshot().message.empty());
  std::this_thread::sleep_for(milliseconds(100));
  EXPECT_EQ(impostor.Snapshot().state, ConnectionState::kUnavailable);
}

TEST_F(ClientTest, CarriesATradeFromEndToEnd) {
  MultiplayerClient asker("127.0.0.1", server_.port());
  MultiplayerClient asked("127.0.0.1", server_.port());
  asker.Start(Player("Dagger"), "");
  asked.Start(Player("Wand"), "");
  ASSERT_TRUE(WaitUntilConnected(asker));
  ASSERT_TRUE(WaitUntilConnected(asked));

  asker.RequestTrade(asked.Snapshot().account_id);

  // The player who was asked gets a box to show and no trade yet.
  ASSERT_TRUE(WaitFor(asked, [](const MultiplayerSnapshot& snapshot) {
    return snapshot.notification_serial > 0;
  }));
  EXPECT_EQ(asked.Snapshot().notification,
            std::vector<std::string>({"Trade request from", "Dagger"}));
  EXPECT_TRUE(asked.Snapshot().trade.id().empty());
  ASSERT_TRUE(WaitFor(asker, [](const MultiplayerSnapshot& snapshot) {
    return !snapshot.trade.id().empty();
  }));
  EXPECT_FALSE(asker.Snapshot().trade.partner_joined());

  asked.RequestTrade(asker.Snapshot().account_id);
  ASSERT_TRUE(WaitFor(asker, [](const MultiplayerSnapshot& snapshot) {
    return snapshot.trade.partner_joined();
  }));

  TradeOffer offer;
  offer.set_meso(5000);
  offer.set_spell_traces(30);
  asked.SetTradeOffer(offer);
  ASSERT_TRUE(WaitFor(asker, [](const MultiplayerSnapshot& snapshot) {
    return snapshot.trade.theirs().meso() == 5000;
  }));
  EXPECT_EQ(asker.Snapshot().trade.mine().meso(), 0);

  asked.LeaveTrade();
  EXPECT_TRUE(WaitFor(asker, [](const MultiplayerSnapshot& snapshot) {
    return snapshot.trade.id().empty();
  }));
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
