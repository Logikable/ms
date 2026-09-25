// Checks the shipped scroll catalog. A scroll that costs nothing is free for
// the player, and a stat that no job can scroll is a file nobody will ever see.
// Neither causes a crash, so neither would be noticed without this.
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

  // A level inside each tier's range where the game has equipment. The price
  // comes from the item, so a scroll can't be priced without one.
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

// A scroll that names no kind of equipment is offered for none, so nobody can
// ever buy it. Only a clean slate works on anything.
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

// Within a tier and target, lower odds must not cost less, or the safe scrolls
// are pointless. "Not less" because GMS's prices can round two rates together
// at the bottom of its table.
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

// Only a clean slate has its own price. A price written on any other scroll is
// ignored, so leaving one there creates a second price that drifts from the one
// the player actually pays.
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

// One clean slate per tier, each more expensive than the last. Three in the
// same tier would offer the player the same slot back at three prices.
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

// Every armour scroll adds a little HP and DEF on top of its main stat. A file
// without them looks like a weapon scroll in the wrong list.
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

// An accessory scroll adds only its chosen stat: no HP, DEF or attack. The
// armour scrolls bundle all three, so a file copied from those would give more
// than GMS does.
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
    // The HP scroll is the one that adds HP, and it adds nothing else.
    bool hp_scroll = scroll.scroll_type() == SCROLL_TYPE_HP;
    EXPECT_EQ(stats.max_hp() > 0, hp_scroll) << entry.first;
    EXPECT_EQ(stats.str() > 0 || stats.dex() > 0 || stats.int_() > 0 ||
                  stats.luk() > 0,
              !hp_scroll)
        << entry.first;
  }
  EXPECT_GT(seen, 0);
}

// The accessory HP scroll costs fifty times the stat scroll of the same rate
// and tier. One number to check instead of nine.
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

// Which stats each job can scroll onto its gear, from the wiki's table. The
// accessory files were copied from the armour ones, so both are checked.
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
          // All Stats is included at 30% for every job, which is why this is a
          // subset check instead of equality.
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

// All Stats raises four stats, each by less than the matching single-stat
// scroll raises one. If it matched, every other armour scroll in its tier would
// be pointless.
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
      // Compared with the single-stat scroll for the same target, since
      // accessory and armour scrolls never apply to the same item.
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

// Glove scrolls add attack, not the stat armour scrolls add. The one exception,
// which adds defense instead, is GMS's tier 1 100%. A glove scroll that added a
// stat would be an armour scroll in the wrong list.
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

// Heart scrolls add only the attack type the wearer's job uses.
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

// Both lists come in both attack types, so a magician finds one wherever a
// warrior does. GMS sells no 15% on either.
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
        // The one exception: GMS's tier 1 100% glove scroll adds defense, while
        // every other one on both lists adds attack.
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
