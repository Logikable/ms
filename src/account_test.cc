#include "src/account.h"

#include <gtest/gtest.h>

#include "src/protos/account.pb.h"

namespace ms {
namespace {

TEST(AccountTest, SeenKeysLatch) {
  AccountInstance account;
  EXPECT_FALSE(account.Seen("skills"));

  account.MarkSeen("skills");
  account.MarkSeen("skills");
  EXPECT_TRUE(account.Seen("skills"));
  EXPECT_FALSE(account.Seen("shop"));
  EXPECT_EQ(account.proto().seen_keys_size(), 1);
}

TEST(AccountTest, ProgressOnlyClimbs) {
  AccountInstance account;
  account.RecordProgress(40, 2);
  EXPECT_EQ(account.max_level(), 40);
  EXPECT_EQ(account.max_job_stage(), 2);

  // A second character starting over does not take the account back down.
  account.RecordProgress(1, 0);
  EXPECT_EQ(account.max_level(), 40);
  EXPECT_EQ(account.max_job_stage(), 2);

  account.RecordProgress(60, 3);
  EXPECT_EQ(account.max_level(), 60);
  EXPECT_EQ(account.max_job_stage(), 3);
}

TEST(AccountTest, WrapsTheProtoItWasGiven) {
  Account proto;
  proto.set_max_level(30);
  proto.add_seen_keys("bag");
  proto.mutable_keybinds();

  AccountInstance account(proto);
  EXPECT_EQ(account.max_level(), 30);
  EXPECT_TRUE(account.Seen("bag"));
}

}  // namespace
}  // namespace ms
