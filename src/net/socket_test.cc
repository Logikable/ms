#include "src/net/socket.h"

#include <gtest/gtest.h>

#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace ms {
namespace {

using ::std::chrono::milliseconds;

// How long a test waits for something that should already be on its way.
constexpr milliseconds kWait(2000);

// Waits for `socket` to have something to read.
bool WaitReadable(const Socket& socket) {
  std::vector<PollTarget> targets(1);
  targets[0].handle = socket.handle();
  targets[0].want_read = true;
  return Poll(targets, kWait) && (targets[0].readable || targets[0].closed);
}

// A connected pair on the loopback: what one end writes, the other reads.
struct Pair {
  Socket listener;
  Socket client;
  Socket server;
};

Pair Connected() {
  Pair pair;
  std::optional<Socket> listener = Listen(0);
  EXPECT_TRUE(listener.has_value());
  pair.listener = std::move(*listener);
  int port = LocalPort(pair.listener);
  EXPECT_GT(port, 0);

  std::optional<Socket> client = Connect("127.0.0.1", port, kWait);
  EXPECT_TRUE(client.has_value());
  pair.client = std::move(*client);

  EXPECT_TRUE(WaitReadable(pair.listener));
  std::optional<Socket> server = Accept(pair.listener);
  EXPECT_TRUE(server.has_value());
  pair.server = std::move(*server);
  return pair;
}

// Reads until `expected` many bytes have arrived, or the wait runs out.
std::string ReadAll(const Socket& socket, size_t expected) {
  std::string got;
  while (got.size() < expected && WaitReadable(socket)) {
    if (Read(socket, got) != IoStatus::kOk) {
      break;
    }
  }
  return got;
}

// Writes the whole of `payload`, waiting out a socket that fills up.
bool WriteAll(const Socket& socket, std::string payload) {
  while (!payload.empty()) {
    IoStatus status = Write(socket, payload);
    if (status == IoStatus::kOk) {
      continue;
    }
    if (status != IoStatus::kWouldBlock) {
      return false;
    }
    std::vector<PollTarget> targets(1);
    targets[0].handle = socket.handle();
    targets[0].want_write = true;
    if (!Poll(targets, kWait)) {
      return false;
    }
  }
  return true;
}

class SocketTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(StartSockets());
  }
};

TEST_F(SocketTest, CarriesBytesBothWays) {
  Pair pair = Connected();

  ASSERT_TRUE(WriteAll(pair.client, "to the server"));
  EXPECT_EQ(ReadAll(pair.server, 13), "to the server");

  ASSERT_TRUE(WriteAll(pair.server, "and back"));
  EXPECT_EQ(ReadAll(pair.client, 8), "and back");
}

TEST_F(SocketTest, ReportsAPeerThatWentAway) {
  Pair pair = Connected();
  ASSERT_TRUE(WriteAll(pair.client, "last words"));
  pair.client.Close();

  // Whatever was already sent still arrives; the close comes after it.
  std::string got;
  ASSERT_TRUE(WaitReadable(pair.server));
  ASSERT_EQ(Read(pair.server, got), IoStatus::kOk);
  EXPECT_EQ(got, "last words");
  ASSERT_TRUE(WaitReadable(pair.server));
  EXPECT_EQ(Read(pair.server, got), IoStatus::kClosed);
}

TEST_F(SocketTest, WaitsRatherThanBlocking) {
  Pair pair = Connected();

  // Nothing has been sent, so neither end has anything to say.
  std::string got;
  EXPECT_EQ(Read(pair.server, got), IoStatus::kWouldBlock);
  EXPECT_TRUE(got.empty());
  EXPECT_FALSE(Accept(pair.listener).has_value());
}

TEST_F(SocketTest, RefusesAPortTwice) {
  std::optional<Socket> first = Listen(0);
  ASSERT_TRUE(first.has_value());
  EXPECT_FALSE(Listen(LocalPort(*first)).has_value());
}

TEST_F(SocketTest, GivesUpOnAPortNobodyIsOn) {
  // A port that was listening and is not any more: nothing can answer there.
  std::optional<Socket> listener = Listen(0);
  ASSERT_TRUE(listener.has_value());
  int port = LocalPort(*listener);
  listener->Close();

  EXPECT_FALSE(Connect("127.0.0.1", port, kWait).has_value());
}

TEST_F(SocketTest, MovingLeavesTheSourceEmpty) {
  std::optional<Socket> listener = Listen(0);
  ASSERT_TRUE(listener.has_value());
  SocketHandle handle = listener->handle();

  Socket moved = std::move(*listener);
  EXPECT_EQ(moved.handle(), handle);
  EXPECT_TRUE(moved.valid());
  EXPECT_FALSE(listener->valid());
}

}  // namespace
}  // namespace ms
