#include "src/item/soul.h"

#include <map>
#include <random>

#include "gtest/gtest.h"

namespace ms {
namespace {

Soul SoulOf(SoulTier tier, SoulLine line) {
  Soul soul;
  soul.set_boss("Damien");
  soul.set_tier(tier);
  soul.set_line(line);
  return soul;
}

// The shard pages' values at both ends: Zakum's C and the SS every endgame
// boss shares. Below SS, ATT and all stats are points; at SS, percents.
TEST(SoulTest, TiersGiveTheShardPagesValues) {
  EXPECT_EQ(SoulLineValue(SOUL_TIER_C, SOUL_LINE_ATTACK), 6);
  EXPECT_FALSE(SoulLineIsPercent(SOUL_TIER_C, SOUL_LINE_ATTACK));
  EXPECT_EQ(SoulLineValue(SOUL_TIER_C, SOUL_LINE_MAX_HP), 1100);
  EXPECT_EQ(SoulLineValue(SOUL_TIER_A, SOUL_LINE_ALL_STATS), 17);
  EXPECT_EQ(SoulLineValue(SOUL_TIER_S, SOUL_LINE_BOSS_DAMAGE), 5);
  EXPECT_TRUE(SoulLineIsPercent(SOUL_TIER_C, SOUL_LINE_BOSS_DAMAGE));

  EXPECT_EQ(SoulLineValue(SOUL_TIER_SS, SOUL_LINE_MAGIC_ATTACK), 3);
  EXPECT_TRUE(SoulLineIsPercent(SOUL_TIER_SS, SOUL_LINE_MAGIC_ATTACK));
  EXPECT_EQ(SoulLineValue(SOUL_TIER_SS, SOUL_LINE_ALL_STATS), 5);
  EXPECT_TRUE(SoulLineIsPercent(SOUL_TIER_SS, SOUL_LINE_ALL_STATS));
  EXPECT_EQ(SoulLineValue(SOUL_TIER_SS, SOUL_LINE_MAX_HP), 2000);
  EXPECT_FALSE(SoulLineIsPercent(SOUL_TIER_SS, SOUL_LINE_MAX_HP));
  EXPECT_EQ(SoulLineValue(SOUL_TIER_SS, SOUL_LINE_CRIT_RATE), 12);
  EXPECT_EQ(SoulLineValue(SOUL_TIER_SS, SOUL_LINE_IGNORE_DEFENSE), 7);
}

// The gauge's 20 ATT and MATT ride every soul; a point line adds to the item's
// stats and a percent line to the potential totals, never both.
TEST(SoulTest, PointsGoToStatsAndPercentsToTotals) {
  EquipStats flat = SoulStats(SoulOf(SOUL_TIER_S, SOUL_LINE_ATTACK));
  EXPECT_EQ(flat.attack(), kSoulGaugeAttack + 10);
  EXPECT_EQ(flat.magic_attack(), kSoulGaugeAttack);
  PotentialTotals none;
  AddSoul(SoulOf(SOUL_TIER_S, SOUL_LINE_ATTACK), none);
  EXPECT_DOUBLE_EQ(none.attack_pct, 0.0);

  flat = SoulStats(SoulOf(SOUL_TIER_A, SOUL_LINE_ALL_STATS));
  EXPECT_EQ(flat.str(), 17);
  EXPECT_EQ(flat.luk(), 17);
  EXPECT_EQ(SoulStats(SoulOf(SOUL_TIER_SS, SOUL_LINE_MAX_HP)).max_hp(), 2000);

  const Soul percent = SoulOf(SOUL_TIER_SS, SOUL_LINE_ATTACK);
  flat = SoulStats(percent);
  EXPECT_EQ(flat.attack(), kSoulGaugeAttack);
  PotentialTotals totals;
  AddSoul(percent, totals);
  EXPECT_DOUBLE_EQ(totals.attack_pct, 0.03);

  AddSoul(SoulOf(SOUL_TIER_SS, SOUL_LINE_ALL_STATS), totals);
  EXPECT_DOUBLE_EQ(totals.dex_pct, 0.05);
  AddSoul(SoulOf(SOUL_TIER_SS, SOUL_LINE_CRIT_RATE), totals);
  EXPECT_DOUBLE_EQ(totals.crit_rate, 0.12);
  AddSoul(SoulOf(SOUL_TIER_SS, SOUL_LINE_BOSS_DAMAGE), totals);
  EXPECT_DOUBLE_EQ(totals.boss_pct, 0.07);
  totals.ied = 0.5;
  AddSoul(SoulOf(SOUL_TIER_SS, SOUL_LINE_IGNORE_DEFENSE), totals);
  EXPECT_DOUBLE_EQ(totals.ied, 1.0 - 0.5 * 0.93);

  // A weapon with no soul gets nothing, the gauge included.
  EXPECT_EQ(SoulStats(Soul()).attack(), 0);
  PotentialTotals empty;
  AddSoul(Soul(), empty);
  EXPECT_DOUBLE_EQ(empty.attack_pct, 0.0);
}

// The roll copies the shard's boss and tier, and every line turns up at
// roughly a seventh.
TEST(SoulTest, ARollTakesTheShardsTierAndAnyLineEvenly) {
  ItemPrototype shard;
  shard.set_short_name("Lucid");
  shard.set_soul_tier(SOUL_TIER_SS);
  std::mt19937 rng(7);
  std::map<SoulLine, int> seen;
  constexpr int kRolls = 7000;
  for (int i = 0; i < kRolls; ++i) {
    const Soul soul = RollSoul(shard, rng);
    EXPECT_EQ(soul.boss(), "Lucid");
    EXPECT_EQ(soul.tier(), SOUL_TIER_SS);
    ++seen[soul.line()];
  }
  ASSERT_EQ(seen.size(), 7u);
  EXPECT_EQ(seen.count(SOUL_LINE_UNSPECIFIED), 0u);
  for (const auto& [line, count] : seen) {
    EXPECT_NEAR(count, kRolls / 7, 100) << SoulLine_Name(line);
  }
}

}  // namespace
}  // namespace ms
