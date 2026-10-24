#include "src/character/hexa_stat.h"

#include <gtest/gtest.h>

#include "src/protos/character.pb.h"

namespace ms {
namespace {

HexaStatNode Node(HexaStat primary, int primary_level, HexaStat first,
                  int first_level, HexaStat second, int second_level) {
  HexaStatNode node;
  node.set_primary(primary);
  node.set_primary_level(primary_level);
  node.set_first(first);
  node.set_first_level(first_level);
  node.set_second(second);
  node.set_second_level(second_level);
  return node;
}

// The wiki's table at its ends and at the level 5 break where the columns
// part.
TEST(HexaStatTest, ValuesFollowTheWikisTable) {
  EXPECT_DOUBLE_EQ(HexaStatValue(HEXA_STAT_CRIT_DAMAGE, 1, true), 0.0035);
  EXPECT_DOUBLE_EQ(HexaStatValue(HEXA_STAT_CRIT_DAMAGE, 10, true), 0.07);
  EXPECT_DOUBLE_EQ(HexaStatValue(HEXA_STAT_CRIT_DAMAGE, 10, false), 0.035);
  EXPECT_DOUBLE_EQ(HexaStatValue(HEXA_STAT_BOSS_DAMAGE, 5, true), 0.06);
  EXPECT_DOUBLE_EQ(HexaStatValue(HEXA_STAT_BOSS_DAMAGE, 5, false), 0.05);
  EXPECT_DOUBLE_EQ(HexaStatValue(HEXA_STAT_DAMAGE, 10, true), 0.15);
  EXPECT_DOUBLE_EQ(HexaStatValue(HEXA_STAT_ATTACK, 10, true), 100);
  EXPECT_DOUBLE_EQ(HexaStatValue(HEXA_STAT_ATTACK, 10, false), 50);
  EXPECT_DOUBLE_EQ(HexaStatValue(HEXA_STAT_MAIN_STAT, 10, true), 2000);
  EXPECT_DOUBLE_EQ(HexaStatValue(HEXA_STAT_MAIN_STAT, 0, true), 0);
}

TEST(HexaStatTest, ANodeHasThreeStatsAndTwentyLevels) {
  EXPECT_TRUE(HexaStatNodeValid(Node(
      HEXA_STAT_BOSS_DAMAGE, 10, HEXA_STAT_ATTACK, 5, HEXA_STAT_DAMAGE, 5)));
  EXPECT_FALSE(HexaStatNodeValid(Node(
      HEXA_STAT_BOSS_DAMAGE, 10, HEXA_STAT_ATTACK, 10, HEXA_STAT_DAMAGE, 1)));
  EXPECT_FALSE(HexaStatNodeValid(Node(
      HEXA_STAT_BOSS_DAMAGE, 11, HEXA_STAT_ATTACK, 0, HEXA_STAT_DAMAGE, 0)));
  EXPECT_FALSE(
      HexaStatNodeValid(Node(HEXA_STAT_BOSS_DAMAGE, 5, HEXA_STAT_BOSS_DAMAGE, 5,
                             HEXA_STAT_DAMAGE, 5)));
}

TEST(HexaStatTest, NodesKeepTheirRulesAcrossEachOther) {
  HexaStats stats;
  *stats.add_nodes() =
      Node(HEXA_STAT_BOSS_DAMAGE, 10, HEXA_STAT_ATTACK, 5, HEXA_STAT_DAMAGE, 5);
  *stats.add_nodes() =
      Node(HEXA_STAT_CRIT_DAMAGE, 10, HEXA_STAT_ATTACK, 5, HEXA_STAT_DAMAGE, 5);
  EXPECT_TRUE(HexaStatsAllowed(stats));
  *stats.add_nodes() = Node(HEXA_STAT_IGNORE_DEFENSE, 10, HEXA_STAT_ATTACK, 5,
                            HEXA_STAT_MAIN_STAT, 5);
  EXPECT_FALSE(HexaStatsAllowed(stats)) << "attack a third time";
  stats.mutable_nodes(2)->set_first(HEXA_STAT_BOSS_DAMAGE);
  EXPECT_TRUE(HexaStatsAllowed(stats));
  stats.mutable_nodes(2)->set_primary(HEXA_STAT_CRIT_DAMAGE);
  EXPECT_FALSE(HexaStatsAllowed(stats)) << "a primary twice";
}

// A node counts only once its level opens it.
TEST(HexaStatTest, ANodeCountsFromItsLevel) {
  HexaStats stats;
  *stats.add_nodes() = Node(HEXA_STAT_BOSS_DAMAGE, 10, HEXA_STAT_ATTACK, 10,
                            HEXA_STAT_DAMAGE, 0);
  *stats.add_nodes() = Node(HEXA_STAT_MAIN_STAT, 10, HEXA_STAT_IGNORE_DEFENSE,
                            5, HEXA_STAT_CRIT_DAMAGE, 5);
  EXPECT_DOUBLE_EQ(HexaStatTotalsFor(stats, 259).boss_pct, 0.0);
  const HexaStatTotals first = HexaStatTotalsFor(stats, 264);
  EXPECT_DOUBLE_EQ(first.boss_pct, 0.20);
  EXPECT_EQ(first.attack, 50);
  EXPECT_EQ(first.main_stat, 0);
  const HexaStatTotals both = HexaStatTotalsFor(stats, 300);
  EXPECT_EQ(both.main_stat, 2000);
  EXPECT_NEAR(both.ied, 0.05, 1e-12);
  EXPECT_DOUBLE_EQ(both.crit_dmg, 0.0175);
}

}  // namespace
}  // namespace ms
