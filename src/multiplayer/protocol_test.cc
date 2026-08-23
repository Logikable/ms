#include "src/multiplayer/protocol.h"

#include <gtest/gtest.h>

#include <string>

#include "src/protos/multiplayer.pb.h"

namespace ms {
namespace {

TEST(ProtocolTest, RoundTripsAMessage) {
  ClientMessage sent;
  sent.mutable_hello()->set_protocol_version(kMultiplayerVersion);
  sent.mutable_hello()->mutable_player()->set_name("Dagger");

  std::string wire;
  ASSERT_TRUE(Encode(sent, wire));

  ClientMessage got;
  ASSERT_EQ(Decode(wire, got), DecodeStatus::kOk);
  EXPECT_EQ(got.hello().protocol_version(), kMultiplayerVersion);
  EXPECT_EQ(got.hello().player().name(), "Dagger");
  EXPECT_TRUE(wire.empty());
}

TEST(ProtocolTest, WaitsForTheRestOfAMessage) {
  ServerMessage sent;
  sent.mutable_welcome()->set_account_id("abc");
  std::string whole;
  ASSERT_TRUE(Encode(sent, whole));

  std::string wire = whole.substr(0, whole.size() - 1);
  ServerMessage got;
  EXPECT_EQ(Decode(wire, got), DecodeStatus::kIncomplete);

  wire = whole;
  EXPECT_EQ(Decode(wire, got), DecodeStatus::kOk);
  EXPECT_EQ(got.welcome().account_id(), "abc");
}

TEST(ProtocolTest, RefusesBytesThatAreNotAMessage) {
  // A frame whose length is past the cap: nothing can be resynchronised.
  std::string oversized("\xff\xff\xff\xff", 4);
  ClientMessage got;
  EXPECT_EQ(Decode(oversized, got), DecodeStatus::kBroken);
}

}  // namespace
}  // namespace ms
