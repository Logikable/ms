#include "src/character/hexa_stat.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <random>

#include "src/character/hyper_stats.h"
#include "src/character/sol_erda.h"
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

TEST(HexaStatTest, PricesFollowTheWiki) {
  EXPECT_EQ(HexaStatOpenPrice(0).energy, 5 * kEnergyPerSolErda);
  EXPECT_EQ(HexaStatOpenPrice(0).fragments, 10);
  EXPECT_EQ(HexaStatOpenPrice(2).energy, 15 * kEnergyPerSolErda);
  EXPECT_EQ(HexaStatOpenPrice(2).fragments, 350);
  EXPECT_EQ(HexaStatOpenPrice(3).fragments, 0);

  HexaStatNode node =
      Node(HEXA_STAT_BOSS_DAMAGE, 2, HEXA_STAT_ATTACK, 0, HEXA_STAT_DAMAGE, 0);
  EXPECT_EQ(HexaStatEnhancePrice(node), 10);
  node.set_primary_level(3);
  EXPECT_EQ(HexaStatEnhancePrice(node), 20);
  node.set_primary_level(9);
  EXPECT_EQ(HexaStatEnhancePrice(node), 50);
  node.set_primary_level(10);
  EXPECT_EQ(HexaStatEnhancePrice(node), 50) << "the wiki's gap";
  node.set_first_level(10);
  EXPECT_EQ(HexaStatEnhancePrice(node), 0) << "full";

  EXPECT_DOUBLE_EQ(HexaStatPrimaryChance(0), 0.35);
  EXPECT_DOUBLE_EQ(HexaStatPrimaryChance(6), 0.20);
  EXPECT_DOUBLE_EQ(HexaStatPrimaryChance(7), 0.15);
  EXPECT_DOUBLE_EQ(HexaStatPrimaryChance(9), 0.05);
  EXPECT_DOUBLE_EQ(HexaStatPrimaryChance(10), 0.0);
}

// The community's exact figures for one node rolled to 20 with no reset:
// about 323 Fragments, and a primary at 5.24 on average.
TEST(HexaStatTest, TwentyRollsCostWhatGmsPlayersMeasured) {
  std::mt19937 rng(11);
  constexpr int kNodes = 20000;
  double primary = 0.0;
  double fragments = 0.0;
  for (int i = 0; i < kNodes; ++i) {
    HexaStatNode node = Node(HEXA_STAT_BOSS_DAMAGE, 0, HEXA_STAT_ATTACK, 0,
                             HEXA_STAT_DAMAGE, 0);
    while (HexaStatEnhancePrice(node) > 0) {
      fragments += HexaStatEnhancePrice(node);
      ASSERT_TRUE(RollHexaStatLevel(node, rng));
      ASSERT_TRUE(HexaStatNodeValid(node));
    }
    EXPECT_EQ(HexaStatNodeLevels(node), kHexaStatNodeLevels);
    EXPECT_FALSE(RollHexaStatLevel(node, rng));
    primary += node.primary_level();
  }
  EXPECT_NEAR(primary / kNodes, 5.24, 0.05);
  EXPECT_NEAR(fragments / kNodes, 323.0, 3.0);
}

// With one additional stat at 10, every level the primary misses goes to the
// other; with the primary at 10, every level goes to an additional stat.
TEST(HexaStatTest, AFullStatPassesItsLevelsOn) {
  std::mt19937 rng(3);
  HexaStatNode node =
      Node(HEXA_STAT_BOSS_DAMAGE, 0, HEXA_STAT_ATTACK, 10, HEXA_STAT_DAMAGE, 0);
  for (int i = 0; i < 10; ++i) {
    ASSERT_TRUE(RollHexaStatLevel(node, rng));
  }
  EXPECT_EQ(node.first_level(), 10);
  EXPECT_EQ(node.primary_level() + node.second_level(), 10);

  HexaStatNode maxed =
      Node(HEXA_STAT_BOSS_DAMAGE, 10, HEXA_STAT_ATTACK, 0, HEXA_STAT_DAMAGE, 0);
  for (int i = 0; i < 10; ++i) {
    ASSERT_TRUE(RollHexaStatLevel(maxed, rng));
  }
  EXPECT_EQ(maxed.primary_level(), 10);
}

TEST(HexaStatTest, EachNodeOpensAtItsLevelOnceTheOneBeforeIsFull) {
  HexaStats stats;
  EXPECT_FALSE(HexaStatNextNodeOpens(stats, 300, kFifthJobStage));
  EXPECT_FALSE(HexaStatNextNodeOpens(stats, 259, kSixthJobStage));
  EXPECT_TRUE(HexaStatNextNodeOpens(stats, 260, kSixthJobStage));
  *stats.add_nodes() =
      Node(HEXA_STAT_BOSS_DAMAGE, 5, HEXA_STAT_ATTACK, 5, HEXA_STAT_DAMAGE, 9);
  EXPECT_FALSE(HexaStatNextNodeOpens(stats, 300, kSixthJobStage))
      << "the first is one short of 20";
  stats.mutable_nodes(0)->set_second_level(10);
  EXPECT_FALSE(HexaStatNextNodeOpens(stats, 264, kSixthJobStage));
  EXPECT_TRUE(HexaStatNextNodeOpens(stats, 265, kSixthJobStage));
  *stats.add_nodes() = stats.nodes(0);
  *stats.add_nodes() = stats.nodes(0);
  EXPECT_FALSE(HexaStatNextNodeOpens(stats, 300, kSixthJobStage))
      << "all three";
}

}  // namespace
}  // namespace ms
