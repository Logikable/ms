// Checks the shipped scroll catalog. A scroll with no trace cost is a scroll
// the player gets for nothing, and a tier that prices the same as the one
// below it is a tier that does not exist -- neither shows up as a crash, so
// neither shows up at all without this.
#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <string>
#include <utility>

#include "src/proto_loader.h"
#include "src/protos/scroll.pb.h"
#include "tools/cpp/runfiles/runfiles.h"

namespace ms {
namespace {

using bazel::tools::cpp::runfiles::Runfiles;

class ScrollDataTest : public ::testing::Test {
 protected:
  void SetUp() override {
    std::string err;
    std::unique_ptr<Runfiles> runfiles(Runfiles::CreateForTest(&err));
    ASSERT_NE(runfiles, nullptr) << err;
    scrolls_ = LoadTextProtoDir<Scroll>(runfiles->Rlocation("ms/data/scrolls"));
    ASSERT_FALSE(scrolls_.empty());
  }

  std::map<std::string, Scroll> scrolls_;
};

TEST_F(ScrollDataTest, EveryScrollIsTieredAndPriced) {
  for (const std::pair<const std::string, Scroll>& entry : scrolls_) {
    const Scroll& scroll = entry.second;
    EXPECT_NE(scroll.tier(), SCROLL_TIER_UNSPECIFIED)
        << entry.first << " belongs to no tier, so it is offered for every "
        << "item at once";
    EXPECT_GT(scroll.trace_cost(), 0) << entry.first << " costs nothing to use";
  }
}

// Within one tier a scroll that lands less often has to cost more, or the
// player would take the safe one every time and the risky ones are decoration.
TEST_F(ScrollDataTest, LongerOddsCostMoreWithinATier) {
  for (const std::pair<const std::string, Scroll>& a : scrolls_) {
    for (const std::pair<const std::string, Scroll>& b : scrolls_) {
      if (a.second.tier() != b.second.tier() ||
          a.second.scroll_category() == SCROLL_CATEGORY_CLEAN_SLATE ||
          b.second.scroll_category() == SCROLL_CATEGORY_CLEAN_SLATE) {
        continue;
      }
      if (a.second.success_rate() < b.second.success_rate()) {
        EXPECT_GT(a.second.trace_cost(), b.second.trace_cost())
            << a.first << " lands less often than " << b.first
            << " and costs no more";
      }
    }
  }
}

// One clean slate a tier, each dearer than the last. Three of them sharing a
// tier would offer the player the same slot back at three prices.
TEST_F(ScrollDataTest, OneCleanSlatePerTierAndTheyClimb) {
  std::map<int, int> by_tier;
  for (const std::pair<const std::string, Scroll>& entry : scrolls_) {
    if (entry.second.scroll_category() != SCROLL_CATEGORY_CLEAN_SLATE) {
      continue;
    }
    EXPECT_EQ(by_tier.count(entry.second.tier()), 0u)
        << "two clean slates in tier " << entry.second.tier();
    by_tier[entry.second.tier()] = entry.second.trace_cost();
  }
  ASSERT_EQ(by_tier.size(), 3u);
  EXPECT_LT(by_tier[SCROLL_TIER_1], by_tier[SCROLL_TIER_2]);
  EXPECT_LT(by_tier[SCROLL_TIER_2], by_tier[SCROLL_TIER_3]);
}

}  // namespace
}  // namespace ms
