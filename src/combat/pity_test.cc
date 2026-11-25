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

// Pity 3 pays the third kill and only it, whatever the kills before paid.
TEST(PityTest, OnlyTheNthKillPays) {
  PityRecord record;
  const MobDrop drop = Drop(3);
  for (int kill = 1; kill <= 6; ++kill) {
    EXPECT_EQ(PityDue(record, "lotus", drop), kill == 3) << kill;
    EXPECT_EQ(ProgressOf(record, "lotus", drop).has_value(), kill <= 3) << kill;
    RecordPity(record, "lotus", drop);
  }
}

// Pity 1 over five copies pays each of the first five kills.
TEST(PityTest, SeveralCopiesPayTheFirstKills) {
  PityRecord record;
  const MobDrop drop = Drop(1, 5);
  for (int kill = 1; kill <= 5; ++kill) {
    ASSERT_TRUE(PityDue(record, "pierre", drop)) << kill;
    EXPECT_EQ(ProgressOf(record, "pierre", drop)->count, kill - 1);
    EXPECT_EQ(ProgressOf(record, "pierre", drop)->of, 5);
    RecordPity(record, "pierre", drop);
  }
  EXPECT_FALSE(PityDue(record, "pierre", drop));
  EXPECT_FALSE(ProgressOf(record, "pierre", drop).has_value());
}

// Each boss counts its own; a drop with no pity counts nothing.
TEST(PityTest, EachBossCountsItsOwn) {
  PityRecord record;
  const MobDrop drop = Drop(2);
  RecordPity(record, "lucid", drop);
  EXPECT_TRUE(PityDue(record, "lucid", drop));
  EXPECT_FALSE(PityDue(record, "will", drop));
  RecordPity(record, "lucid", Drop(0));
  EXPECT_EQ(record.size(), 1);
  EXPECT_FALSE(ProgressOf(record, "lucid", Drop(0)).has_value());
}

}  // namespace
}  // namespace ms
