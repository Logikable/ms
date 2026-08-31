// Checks the shipped scroll catalog. A scroll that prices to nothing is a
// scroll the player gets for free, and a stat that no job can scroll is a file
// nobody will ever see -- neither shows up as a crash, so neither shows up at
// all without this.
#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>

#include "src/item/spell_trace_cost.h"
#include "src/protos/equip.pb.h"
#include "src/protos/scroll.pb.h"
#include "src/testing/data_files.h"

namespace ms {
namespace {

class ScrollDataTest : public ::testing::Test {
 protected:
  void SetUp() override {
    scrolls_ = LoadTestData<Scroll>("scrolls");
    ASSERT_FALSE(scrolls_.empty());
  }

  // A level inside each tier's own span, where the game ships equipment: the
  // price comes from the item, so a scroll cannot be priced without one.
  static int LevelForTier(ScrollTier tier) {
    switch (tier) {
      case SCROLL_TIER_2:
        return 100;
      case SCROLL_TIER_3:
        return 150;
      default:
        return 70;
    }
  }

  int CostOf(const Scroll& scroll) const {
    return TraceCost(scroll, LevelForTier(scroll.tier()));
  }

  std::map<std::string, Scroll> scrolls_;
};

TEST_F(ScrollDataTest, EveryScrollIsTieredAndPriced) {
  for (const std::pair<const std::string, Scroll>& entry : scrolls_) {
    const Scroll& scroll = entry.second;
    EXPECT_NE(scroll.tier(), SCROLL_TIER_UNSPECIFIED)
        << entry.first << " belongs to no tier, so it is offered for every "
        << "item at once";
    EXPECT_GT(CostOf(scroll), 0)
        << entry.first << " costs nothing to use on an item of its own tier, "
        << "which means GMS sells no such scroll";
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

// Within one tier a scroll that lands less often must not cost less, or the
// player would take the risky one every time and the safe ones are decoration.
// Within one target too: a weapon is dearer than armour at every level, so the
// two families are only comparable against themselves.
//
// "Not less" rather than "more" because the price is GMS's, and at the bottom
// of its table two rates can round to the same figure. src/item's own test
// pins where that happens.
TEST_F(ScrollDataTest, LongerOddsNeverCostLess) {
  for (const std::pair<const std::string, Scroll>& a : scrolls_) {
    for (const std::pair<const std::string, Scroll>& b : scrolls_) {
      if (a.second.tier() != b.second.tier() ||
          a.second.target() != b.second.target() ||
          a.second.scroll_category() == SCROLL_CATEGORY_CLEAN_SLATE ||
          b.second.scroll_category() == SCROLL_CATEGORY_CLEAN_SLATE) {
        continue;
      }
      if (a.second.success_rate() < b.second.success_rate()) {
        EXPECT_GE(CostOf(a.second), CostOf(b.second))
            << a.first << " lands less often than " << b.first
            << " and costs less";
      }
    }
  }
}

// Only a clean slate carries a price of its own. A figure written on any other
// scroll is ignored, so leaving one there is a second price that drifts out of
// step with the one the player is actually charged.
TEST_F(ScrollDataTest, OnlyACleanSlateWritesItsOwnPrice) {
  for (const std::pair<const std::string, Scroll>& entry : scrolls_) {
    if (entry.second.scroll_category() == SCROLL_CATEGORY_CLEAN_SLATE) {
      EXPECT_GT(entry.second.trace_cost(), 0) << entry.first << " is free";
      continue;
    }
    EXPECT_EQ(entry.second.trace_cost(), 0)
        << entry.first << " names a price, which nothing reads: the item's "
        << "level sets it";
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

// An accessory scroll pays the stat it was chosen for and nothing else: no HP
// rider, no DEF, no attack. The armour shelf beside it bundles all three, so a
// file copied from there arrives paying more than GMS sells.
TEST_F(ScrollDataTest, EveryAccessoryScrollPaysOneThing) {
  int seen = 0;
  for (const std::pair<const std::string, Scroll>& entry : scrolls_) {
    const Scroll& scroll = entry.second;
    if (scroll.target() != SCROLL_TARGET_ACCESSORY) {
      continue;
    }
    ++seen;
    const EquipStats& stats = scroll.stats();
    EXPECT_EQ(stats.def(), 0) << entry.first << " pays defense";
    EXPECT_EQ(stats.attack(), 0) << entry.first << " pays attack";
    EXPECT_EQ(stats.magic_attack(), 0) << entry.first;
    // The HP shelf is the one that pays HP, and it pays nothing else.
    bool hp_scroll = scroll.scroll_type() == SCROLL_TYPE_HP;
    EXPECT_EQ(stats.max_hp() > 0, hp_scroll) << entry.first;
    EXPECT_EQ(stats.str() > 0 || stats.dex() > 0 || stats.int_() > 0 ||
                  stats.luk() > 0,
              !hp_scroll)
        << entry.first;
  }
  EXPECT_GT(seen, 0);
}

// The HP shelf trades at fifty times the stat shelf beside it, rate for rate
// and tier for tier. One figure to check rather than nine.
TEST_F(ScrollDataTest, AnAccessoryHpScrollIsFiftyTimesItsStatScroll) {
  int checked = 0;
  for (const std::pair<const std::string, Scroll>& hp : scrolls_) {
    if (hp.second.target() != SCROLL_TARGET_ACCESSORY ||
        hp.second.scroll_type() != SCROLL_TYPE_HP) {
      continue;
    }
    for (const std::pair<const std::string, Scroll>& stat : scrolls_) {
      if (stat.second.target() != SCROLL_TARGET_ACCESSORY ||
          stat.second.scroll_type() != SCROLL_TYPE_STR ||
          stat.second.tier() != hp.second.tier() ||
          stat.second.success_rate() != hp.second.success_rate()) {
        continue;
      }
      ++checked;
      EXPECT_EQ(hp.second.stats().max_hp(), stat.second.stats().str() * 50)
          << hp.first;
    }
  }
  EXPECT_EQ(checked, 9);
}

// Which stat a job can put on its gear, from the wiki's own table. A missing
// file shows up as a job with nothing to scroll at that tier. Asked of the
// armour and the accessory shelves alike: the two carry the same stats, and
// the accessory one was written by copying this table.
TEST_F(ScrollDataTest, EveryJobHasAScrollAtEveryTierAndRate) {
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
  const ScrollTarget kShelves[] = {SCROLL_TARGET_ARMOUR,
                                   SCROLL_TARGET_ACCESSORY};

  for (ScrollTarget shelf : kShelves) {
    for (const JobStats& expected : kExpected) {
      for (ScrollTier tier : kTiers) {
        for (int rate : kRates) {
          std::set<ScrollType> found;
          for (const std::pair<const std::string, Scroll>& entry : scrolls_) {
            const Scroll& s = entry.second;
            if (s.target() != shelf || s.tier() != tier ||
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
                << "job " << expected.job << " has no " << stat << " " << shelf
                << " scroll at tier " << tier << ", " << rate << "%";
          }
          EXPECT_EQ(found.count(SCROLL_TYPE_ALL_STATS), rate == 30 ? 1u : 0u)
              << "job " << expected.job << " on shelf " << shelf << " at tier "
              << tier << ", " << rate << "%";
        }
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
      // Against the single-stat scroll on the same shelf: an accessory scroll
      // and a piece of armour are never offered for the same item.
      if (one.second.target() != all.second.target() ||
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

// The glove shelf pays attack, not the stat the armour shelf pays, and the
// one rung that pays defense instead is GMS's tier 1 100%. A file that paid a
// stat here would be an armour scroll that wandered onto the wrong shelf.
TEST_F(ScrollDataTest, EveryGloveScrollPaysAttackOrDefense) {
  int seen = 0;
  for (const std::pair<const std::string, Scroll>& entry : scrolls_) {
    const Scroll& scroll = entry.second;
    if (scroll.target() != SCROLL_TARGET_GLOVES) {
      continue;
    }
    ++seen;
    const EquipStats& stats = scroll.stats();
    EXPECT_EQ(stats.str() + stats.dex() + stats.int_() + stats.luk(), 0)
        << entry.first << " pays a stat, which a glove scroll never does";
    EXPECT_EQ(stats.max_hp(), 0) << entry.first;
    if (scroll.scroll_type() == SCROLL_TYPE_DEF) {
      EXPECT_EQ(scroll.tier(), SCROLL_TIER_1) << entry.first;
      EXPECT_EQ(scroll.success_rate(), 100) << entry.first;
      EXPECT_GT(stats.def(), 0) << entry.first;
      continue;
    }
    EXPECT_EQ(stats.def(), 0) << entry.first;
    EXPECT_GT(stats.attack() + stats.magic_attack(), 0) << entry.first;
  }
  EXPECT_GT(seen, 0);
}

// A heart pays the attack its wearer's job swings with and nothing else.
TEST_F(ScrollDataTest, EveryHeartScrollPaysOnlyAttack) {
  int seen = 0;
  for (const std::pair<const std::string, Scroll>& entry : scrolls_) {
    const Scroll& scroll = entry.second;
    if (scroll.target() != SCROLL_TARGET_HEART) {
      continue;
    }
    ++seen;
    const EquipStats& stats = scroll.stats();
    EXPECT_EQ(stats.str() + stats.dex() + stats.int_() + stats.luk(), 0)
        << entry.first;
    EXPECT_EQ(stats.max_hp(), 0) << entry.first;
    EXPECT_EQ(stats.def(), 0) << entry.first;
    EXPECT_GT(stats.attack() + stats.magic_attack(), 0) << entry.first;
  }
  EXPECT_GT(seen, 0);
}

// Both shelves come in the two kinds of attack, and a magician has to find
// one wherever a warrior does. GMS sells no 15% on either.
TEST_F(ScrollDataTest, TheAttackShelvesCoverBothKindsAtEveryRate) {
  const ScrollTarget kShelves[] = {SCROLL_TARGET_GLOVES, SCROLL_TARGET_HEART};
  const ScrollTier kTiers[] = {SCROLL_TIER_1, SCROLL_TIER_2, SCROLL_TIER_3};
  for (ScrollTarget shelf : kShelves) {
    for (ScrollTier tier : kTiers) {
      for (int rate : {100, 70, 30}) {
        std::set<ScrollType> found;
        for (const std::pair<const std::string, Scroll>& entry : scrolls_) {
          const Scroll& s = entry.second;
          if (s.target() == shelf && s.tier() == tier &&
              s.success_rate() == rate) {
            found.insert(s.scroll_type());
          }
        }
        // The one gap: GMS pays a glove defense at tier 1, 100%, where every
        // other rung on both shelves pays attack.
        if (shelf == SCROLL_TARGET_GLOVES && tier == SCROLL_TIER_1 &&
            rate == 100) {
          EXPECT_EQ(found, std::set<ScrollType>{SCROLL_TYPE_DEF});
          continue;
        }
        EXPECT_EQ(found,
                  (std::set<ScrollType>{SCROLL_TYPE_ATT, SCROLL_TYPE_MATT}))
            << "shelf " << shelf << " tier " << tier << " at " << rate << "%";
      }
    }
  }
}

}  // namespace
}  // namespace ms
