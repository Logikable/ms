#include "src/combat/damage_breakdown.h"

#include <gtest/gtest.h>

#include <vector>

namespace ms {
namespace {

// A row sums its lines, counts each cast once however many monsters and lines
// it hit, and rows sort heaviest first.
TEST(DamageBreakdownTest, RowsAddUpAndCountEachCastOnce) {
  DamageBreakdown breakdown;
  // One attack on two monsters, two lines each, plus a Final Attack on both.
  breakdown.AddLine("Brandish", 100.0, 1);
  breakdown.AddLine("Brandish", 100.0, 1);
  breakdown.AddLine("Brandish", 100.0, 1);
  breakdown.AddLine("Brandish", 100.0, 1);
  breakdown.AddLine("Advanced Final Attack", 50.0, 1);
  breakdown.AddLine("Advanced Final Attack", 50.0, 1);
  // A three-pulse hold recorded one monster at a time: casts 2-4, then 2-4
  // again.
  for (int mob = 0; mob < 2; ++mob) {
    for (int cast = 2; cast <= 4; ++cast) {
      breakdown.AddLine("Hurricane", 10.0, cast);
    }
  }

  std::vector<BreakdownRow> rows = breakdown.Rows();
  ASSERT_EQ(rows.size(), 3u);
  EXPECT_EQ(rows[0].skill, "Brandish");
  EXPECT_DOUBLE_EQ(rows[0].damage, 400.0);
  EXPECT_EQ(rows[0].casts, 1);
  EXPECT_EQ(rows[0].lines, 4);
  EXPECT_DOUBLE_EQ(rows[0].per_line(), 100.0);
  EXPECT_EQ(rows[1].skill, "Advanced Final Attack");
  EXPECT_EQ(rows[1].casts, 1);
  EXPECT_EQ(rows[2].skill, "Hurricane");
  EXPECT_EQ(rows[2].casts, 3);
  EXPECT_EQ(rows[2].lines, 6);

  EXPECT_DOUBLE_EQ(breakdown.total(), 560.0);
  EXPECT_DOUBLE_EQ(TotalDamage(rows), 560.0);
  EXPECT_DOUBLE_EQ(DamageShare(rows[1], breakdown.total()), 100.0 / 560.0);
}

TEST(DamageBreakdownTest, NothingLandedIsNoRowsAndNoShare) {
  DamageBreakdown breakdown;
  breakdown.AddSeconds(1.5);
  breakdown.AddSeconds(2.0);
  EXPECT_DOUBLE_EQ(breakdown.seconds(), 3.5);
  EXPECT_TRUE(breakdown.Rows().empty());
  EXPECT_DOUBLE_EQ(DamageShare(BreakdownRow{}, 0.0), 0.0);
  EXPECT_DOUBLE_EQ(BreakdownRow{}.per_line(), 0.0);
}

// Merging two players' rows with the same name gives one party row. Ties sort
// by name.
TEST(DamageBreakdownTest, MergeAddsUpEachSkillAcrossPlayers) {
  std::vector<PlayerBreakdown> players(2);
  players[0].rows = {{"Raging Blow", 300.0, 3, 9}, {"Puncture", 50.0, 5, 5}};
  players[1].rows = {{"Raging Blow", 200.0, 2, 4}, {"Blast", 50.0, 1, 1}};

  std::vector<BreakdownRow> rows = MergeBreakdowns(players);
  ASSERT_EQ(rows.size(), 3u);
  EXPECT_EQ(rows[0].skill, "Raging Blow");
  EXPECT_DOUBLE_EQ(rows[0].damage, 500.0);
  EXPECT_EQ(rows[0].casts, 5);
  EXPECT_EQ(rows[0].lines, 13);
  EXPECT_EQ(rows[1].skill, "Blast");
  EXPECT_EQ(rows[2].skill, "Puncture");
  EXPECT_TRUE(MergeBreakdowns({}).empty());
}

}  // namespace
}  // namespace ms
