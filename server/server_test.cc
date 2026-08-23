#include "server/server.h"

#include <gtest/gtest.h>

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "src/multiplayer/protocol.h"
#include "src/net/socket.h"
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

class ServerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(StartSockets());
    std::optional<Socket> listener = Listen(0);
    ASSERT_TRUE(listener.has_value());
    port_ = LocalPort(*listener);
    server_ = std::make_unique<Server>(std::move(*listener), 7);
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

  // A client that has been welcomed, with `welcome` filled in.
  std::unique_ptr<TestClient> Greeted(const std::string& name,
                                      Welcome* welcome) {
    std::unique_ptr<TestClient> client = std::make_unique<TestClient>();
    EXPECT_TRUE(client->Open(port_));
    client->SayHello(name);
    ServerMessage message = Await(*client);
    EXPECT_EQ(message.kind_case(), ServerMessage::kWelcome);
    *welcome = message.welcome();
    return client;
  }

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
  EXPECT_EQ(Await(*client).kind_case(), ServerMessage::kPong);
}

TEST_F(ServerTest, DropsASessionThatGoesQuiet) {
  Welcome welcome;
  std::unique_ptr<TestClient> client = Greeted("Dagger", &welcome);
  ASSERT_EQ(server_->session_count(), 1);

  now_ += kSessionTimeout + seconds(1);
  EXPECT_TRUE(StepUntil([&]() { return server_->session_count() == 0; }));
}

TEST_F(ServerTest, SendsEveryoneAwayWhenDraining) {
  Welcome welcome;
  std::unique_ptr<TestClient> client = Greeted("Dagger", &welcome);

  server_->Drain();
  ServerMessage message = Await(*client);
  ASSERT_EQ(message.kind_case(), ServerMessage::kRejected);
  EXPECT_EQ(message.rejected().reason(), Rejected::REASON_MAINTENANCE);
  EXPECT_TRUE(StepUntil([&]() { return server_->drained(); }));

  // Nobody new is taken on while it is going down.
  TestClient late;
  EXPECT_FALSE(late.Open(port_));
}

}  // namespace
}  // namespace ms
