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
  // A frame with a length over the cap: there's no way to resynchronise.
  std::string oversized("\xff\xff\xff\xff", 4);
  ClientMessage got;
  EXPECT_EQ(Decode(oversized, got), DecodeStatus::kBroken);
}

TEST(ProtocolTest, SplitsAnIpv4Address) {
  auto split = SplitServerAddress("1.2.3.4:21711");
  ASSERT_TRUE(split.has_value());
  EXPECT_EQ(split->first, "1.2.3.4");
  EXPECT_EQ(split->second, 21711);
}

TEST(ProtocolTest, SplitsAnIpv6AddressInBrackets) {
  auto split = SplitServerAddress("[2606:8ac0::23c3]:21711");
  ASSERT_TRUE(split.has_value());
  EXPECT_EQ(split->first, "2606:8ac0::23c3");
  EXPECT_EQ(split->second, 21711);
}

TEST(ProtocolTest, RefusesAnAddressWithoutAHostOrPort) {
  EXPECT_FALSE(SplitServerAddress("").has_value());
  EXPECT_FALSE(SplitServerAddress("host").has_value());
  EXPECT_FALSE(SplitServerAddress(":21711").has_value());
  EXPECT_FALSE(SplitServerAddress("[]:21711").has_value());
  EXPECT_FALSE(SplitServerAddress("host:0").has_value());
  EXPECT_FALSE(SplitServerAddress("host:port").has_value());
}

TEST(ProtocolTest, JoinedAddressesSplitBack) {
  for (const char* host : {"1.2.3.4", "example.com", "::1", kServerHost}) {
    if (*host == '\0') {
      continue;
    }
    auto split = SplitServerAddress(JoinServerAddress(host, 21711));
    ASSERT_TRUE(split.has_value()) << host;
    EXPECT_EQ(split->first, host);
    EXPECT_EQ(split->second, 21711);
  }
}

}  // namespace
}  // namespace ms
