#include "src/combat/pity.h"

#include <gtest/gtest.h>

namespace ms {
namespace {

MobDrop Drop(int pity, int copies = 0) {
  MobDrop drop;
  drop.set_equip("belt");
  drop.set_per_kill(0.03);
  drop.set_pity(pity);
  drop.set_pity_copies(copies);
  return drop;
}

// Pity 3 makes the third kill without a copy certain, and is spent by the copy.
TEST(PityTest, TheNthKillWithoutACopyPays) {
  PityRecord record;
  const MobDrop drop = Drop(3);
  EXPECT_FALSE(PityDue(record, "lotus", drop));
  RecordPity(record, "lotus", drop, false);
  EXPECT_FALSE(PityDue(record, "lotus", drop));
  RecordPity(record, "lotus", drop, false);
  EXPECT_TRUE(PityDue(record, "lotus", drop));
  ASSERT_TRUE(ProgressOf(record, "lotus", drop).has_value());
  EXPECT_EQ(ProgressOf(record, "lotus", drop)->count, 2);
  EXPECT_EQ(ProgressOf(record, "lotus", drop)->of, 3);
  RecordPity(record, "lotus", drop, true);
  EXPECT_FALSE(PityDue(record, "lotus", drop));
  EXPECT_FALSE(ProgressOf(record, "lotus", drop).has_value());
  for (int i = 0; i < 5; ++i) {
    RecordPity(record, "lotus", drop, false);
  }
  EXPECT_FALSE(PityDue(record, "lotus", drop));
}

// Pity 1 over five copies pays each of the first five kills.
TEST(PityTest, SeveralCopiesPayTheFirstKills) {
  PityRecord record;
  const MobDrop drop = Drop(1, 5);
  for (int i = 0; i < 5; ++i) {
    ASSERT_TRUE(PityDue(record, "pierre", drop)) << i;
    EXPECT_EQ(ProgressOf(record, "pierre", drop)->count, i);
    EXPECT_EQ(ProgressOf(record, "pierre", drop)->of, 5);
    RecordPity(record, "pierre", drop, true);
  }
  EXPECT_FALSE(PityDue(record, "pierre", drop));
}

// Each boss counts its own; a drop with no pity counts nothing.
TEST(PityTest, EachBossCountsItsOwn) {
  PityRecord record;
  const MobDrop drop = Drop(2);
  RecordPity(record, "lucid", drop, false);
  EXPECT_TRUE(PityDue(record, "lucid", drop));
  EXPECT_FALSE(PityDue(record, "will", drop));
  RecordPity(record, "lucid", Drop(0), false);
  EXPECT_EQ(record.size(), 1);
  EXPECT_FALSE(ProgressOf(record, "lucid", Drop(0)).has_value());
}

}  // namespace
}  // namespace ms
