#include "src/net/frame.h"

#include <gtest/gtest.h>

#include <string>

namespace ms {
namespace {

TEST(FrameTest, RoundTripsInOrder) {
  std::string wire;
  ASSERT_TRUE(AppendFrame("first", wire));
  ASSERT_TRUE(AppendFrame("", wire));
  ASSERT_TRUE(AppendFrame("third", wire));

  std::string payload;
  EXPECT_EQ(TakeFrame(wire, payload), FrameStatus::kOk);
  EXPECT_EQ(payload, "first");
  EXPECT_EQ(TakeFrame(wire, payload), FrameStatus::kOk);
  EXPECT_EQ(payload, "");
  EXPECT_EQ(TakeFrame(wire, payload), FrameStatus::kOk);
  EXPECT_EQ(payload, "third");
  EXPECT_EQ(TakeFrame(wire, payload), FrameStatus::kIncomplete);
  EXPECT_TRUE(wire.empty());
}

TEST(FrameTest, HoldsAPartialFrame) {
  std::string whole;
  ASSERT_TRUE(AppendFrame("payload", whole));

  // Every prefix short of the last byte is incomplete, and none of them is
  // consumed.
  std::string payload;
  for (size_t i = 0; i < whole.size(); ++i) {
    std::string wire = whole.substr(0, i);
    EXPECT_EQ(TakeFrame(wire, payload), FrameStatus::kIncomplete) << i;
    EXPECT_EQ(wire.size(), i);
  }
  EXPECT_EQ(TakeFrame(whole, payload), FrameStatus::kOk);
  EXPECT_EQ(payload, "payload");
}

TEST(FrameTest, CarriesBytesThatAreNotText) {
  std::string binary("\x00\xff\x0a\x00", 4);
  std::string wire;
  ASSERT_TRUE(AppendFrame(binary, wire));

  std::string payload;
  ASSERT_EQ(TakeFrame(wire, payload), FrameStatus::kOk);
  EXPECT_EQ(payload, binary);
}

TEST(FrameTest, RefusesAnOversizedFrame) {
  std::string wire;
  EXPECT_FALSE(AppendFrame(std::string(kMaxFrameBytes + 1, 'x'), wire));
  EXPECT_TRUE(wire.empty());

  // A length past the cap is refused before anything is waited for.
  std::string header("\xff\xff\xff\xff", 4);
  std::string payload;
  EXPECT_EQ(TakeFrame(header, payload), FrameStatus::kTooLarge);
}

TEST(FrameTest, TakesTheLargestFrameAllowed) {
  std::string wire;
  ASSERT_TRUE(AppendFrame(std::string(kMaxFrameBytes, 'x'), wire));

  std::string payload;
  ASSERT_EQ(TakeFrame(wire, payload), FrameStatus::kOk);
  EXPECT_EQ(payload.size(), kMaxFrameBytes);
}

}  // namespace
}  // namespace ms
