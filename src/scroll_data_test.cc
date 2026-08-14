// Checks the shipped scroll catalog. A scroll with no trace cost is a scroll
// the player gets for nothing, and a tier that prices the same as the one
// below it is a tier that does not exist -- neither shows up as a crash, so
// neither shows up at all without this.
#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>

#include "src/proto_loader.h"
#include "src/protos/equip.pb.h"
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

// A scroll that names no kind of equipment is offered for none of it, which
// is a scroll nobody can ever buy. Only a clean slate goes on anything.
TEST_F(ScrollDataTest, EveryScrollButACleanSlateNamesWhatItGoesOn) {
  for (const std::pair<const std::string, Scroll>& entry : scrolls_) {
    if (entry.second.scroll_category() == SCROLL_CATEGORY_CLEAN_SLATE) {
      EXPECT_EQ(entry.second.target(), SCROLL_TARGET_UNSPECIFIED)
          << entry.first << " is a clean slate held to one kind of item";
      continue;
    }
    EXPECT_NE(entry.second.target(), SCROLL_TARGET_UNSPECIFIED)
        << entry.first << " goes on nothing";
  }
}

// Within one tier a scroll that lands less often has to cost more, or the
// player would take the safe one every time and the risky ones are decoration.
// Within one target, too: armour is priced well under a weapon, so a safe
// weapon scroll costs more than a risky armour one and the two families are
// only comparable against themselves.
TEST_F(ScrollDataTest, LongerOddsCostMoreWithinATier) {
  for (const std::pair<const std::string, Scroll>& a : scrolls_) {
    for (const std::pair<const std::string, Scroll>& b : scrolls_) {
      if (a.second.tier() != b.second.tier() ||
          a.second.target() != b.second.target() ||
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

// Every armour scroll carries a little HP and DEF on top of whatever stat it
// was chosen for. A file written without them looks like a weapon scroll that
// wandered into the wrong list.
TEST_F(ScrollDataTest, EveryArmourScrollPaysHpAndDef) {
  int seen = 0;
  for (const std::pair<const std::string, Scroll>& entry : scrolls_) {
    if (entry.second.target() != SCROLL_TARGET_ARMOUR) {
      continue;
    }
    ++seen;
    EXPECT_GT(entry.second.stats().max_hp(), 0) << entry.first;
    EXPECT_GT(entry.second.stats().def(), 0) << entry.first;
    EXPECT_EQ(entry.second.stats().attack(), 0)
        << entry.first << " pays weapon attack, which armour never does";
  }
  EXPECT_GT(seen, 0);
}

// Armour is never dearer to scroll than a weapon of the same level and rate.
// It is why the two families cannot be priced against each other. The only
// place the two meet is the cheapest scroll in the game, at tier 1's 100%.
TEST_F(ScrollDataTest, ArmourCostsNoMoreThanAWeaponOfTheSameTierAndRate) {
  for (const std::pair<const std::string, Scroll>& a : scrolls_) {
    for (const std::pair<const std::string, Scroll>& w : scrolls_) {
      if (a.second.target() != SCROLL_TARGET_ARMOUR ||
          w.second.target() != SCROLL_TARGET_WEAPON ||
          a.second.tier() != w.second.tier() ||
          a.second.success_rate() != w.second.success_rate()) {
        continue;
      }
      EXPECT_LE(a.second.trace_cost(), w.second.trace_cost())
          << a.first << " costs more than " << w.first;
    }
  }
}

// Which stat a job can put on its armour, from the wiki's own table. A missing
// file shows up as a job with nothing to scroll at that tier.
TEST_F(ScrollDataTest, EveryJobHasAnArmourScrollAtEveryTierAndRate) {
  struct JobStats {
    EquipJobCategory job;
    std::set<ScrollType> stats;
  };
  const JobStats kExpected[] = {
      {EQUIP_JOB_CATEGORY_WARRIOR, {SCROLL_TYPE_STR, SCROLL_TYPE_HP}},
      {EQUIP_JOB_CATEGORY_BOWMAN, {SCROLL_TYPE_DEX}},
      {EQUIP_JOB_CATEGORY_MAGICIAN, {SCROLL_TYPE_INT}},
      {EQUIP_JOB_CATEGORY_THIEF,
       {SCROLL_TYPE_STR, SCROLL_TYPE_DEX, SCROLL_TYPE_LUK}},
      {EQUIP_JOB_CATEGORY_PIRATE,
       {SCROLL_TYPE_STR, SCROLL_TYPE_DEX, SCROLL_TYPE_LUK}},
      {EQUIP_JOB_CATEGORY_UNIVERSAL,
       {SCROLL_TYPE_STR, SCROLL_TYPE_DEX, SCROLL_TYPE_INT, SCROLL_TYPE_LUK,
        SCROLL_TYPE_HP}},
  };
  const ScrollTier kTiers[] = {SCROLL_TIER_1, SCROLL_TIER_2, SCROLL_TIER_3};
  const int kRates[] = {100, 70, 30};

  for (const JobStats& expected : kExpected) {
    for (ScrollTier tier : kTiers) {
      for (int rate : kRates) {
        std::set<ScrollType> found;
        for (const std::pair<const std::string, Scroll>& entry : scrolls_) {
          const Scroll& s = entry.second;
          if (s.target() != SCROLL_TARGET_ARMOUR || s.tier() != tier ||
              s.success_rate() != rate) {
            continue;
          }
          for (int job : s.applicable_job_categories()) {
            if (job == expected.job) {
              found.insert(s.scroll_type());
            }
          }
        }
        // All Stats rides along at 30% for every job, which is why the
        // expectation is a subset check rather than an equality.
        for (ScrollType stat : expected.stats) {
          EXPECT_EQ(found.count(stat), 1u)
              << "job " << expected.job << " has no " << stat
              << " armour scroll at tier " << tier << ", " << rate << "%";
        }
        EXPECT_EQ(found.count(SCROLL_TYPE_ALL_STATS), rate == 30 ? 1u : 0u)
            << "job " << expected.job << " at tier " << tier << ", " << rate
            << "%";
      }
    }
  }
}

// All Stats raises four stats by less than the scroll beside it raises one.
// A file that let it match the single-stat scroll would make every other
// armour scroll in its tier pointless.
TEST_F(ScrollDataTest, AllStatsPaysLessPerStatThanASingleStatScroll) {
  for (const std::pair<const std::string, Scroll>& all : scrolls_) {
    if (all.second.scroll_type() != SCROLL_TYPE_ALL_STATS) {
      continue;
    }
    EXPECT_EQ(all.second.success_rate(), 30)
        << all.first << " is offered outside the long odds";
    const EquipStats& s = all.second.stats();
    EXPECT_EQ(s.str(), s.dex());
    EXPECT_EQ(s.str(), s.int_());
    EXPECT_EQ(s.str(), s.luk());
    for (const std::pair<const std::string, Scroll>& one : scrolls_) {
      if (one.second.target() != SCROLL_TARGET_ARMOUR ||
          one.second.tier() != all.second.tier() ||
          one.second.scroll_type() != SCROLL_TYPE_STR) {
        continue;
      }
      if (one.second.success_rate() == 30) {
        EXPECT_LT(s.str(), one.second.stats().str()) << all.first;
      }
    }
  }
}

}  // namespace
}  // namespace ms
