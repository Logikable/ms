#include "analysis/run_stats.h"

#include <gtest/gtest.h>

#include <cmath>
#include <string>
#include <vector>

namespace ms {
namespace {

RunStats OneStat(const std::string& branch, unsigned int seed, double cubes) {
  RunStats run{branch, seed, {}};
  run.Add("cubes", cubes);
  return run;
}

TEST(RunStatsTest, RowsRoundTrip) {
  RunStats run{"Fire/Poison", 7, {}};
  run.Add("map_days/Kerning\tTower", 1.5);
  run.Add("cubes", 300);
  std::string text;
  AppendStatsRows(run, &text);

  std::vector<RunStats> back = ParseStatsRows(text + "garbage line\n");
  ASSERT_EQ(back.size(), 1u);
  EXPECT_EQ(back[0].branch, "Fire/Poison");
  EXPECT_EQ(back[0].seed, 7u);
  ASSERT_EQ(back[0].values.size(), 2u);
  EXPECT_EQ(back[0].values[0].first, "map_days/Kerning Tower");
  EXPECT_DOUBLE_EQ(back[0].values[0].second, 1.5);
  EXPECT_DOUBLE_EQ(back[0].values[1].second, 300);
}

TEST(RunStatsTest, MissingStatReadsAsZero) {
  RunStats a = OneStat("Hero", 1, 10);
  RunStats b{"Hero", 2, {}};
  b.Add("booms", 3);
  std::vector<StatSummary> stats = Summarize({a, b});
  ASSERT_EQ(stats.size(), 2u);
  EXPECT_EQ(stats[1].stat, "cubes");
  EXPECT_EQ(stats[1].by_branch.at("Hero"), (std::vector<double>{10, 0}));
  EXPECT_DOUBLE_EQ(stats[1].branch_mean.at("Hero"), 5);
}

TEST(RunStatsTest, SeedSpreadIsTheMeanOfEachBranchsVariation) {
  // Hero 10 and 30: mean 20, sd 14.14. Bishop never varies.
  std::vector<StatSummary> stats =
      Summarize({OneStat("Hero", 1, 10), OneStat("Hero", 2, 30),
                 OneStat("Bishop", 1, 5), OneStat("Bishop", 2, 5)});
  ASSERT_EQ(stats.size(), 1u);
  EXPECT_NEAR(stats[0].seed_spread, (std::sqrt(200.0) / 20.0) / 2.0, 1e-9);
}

TEST(RunStatsTest, FindsTheBranchFarFromTheRest) {
  std::vector<RunStats> runs = {OneStat("A", 1, 100), OneStat("B", 1, 104),
                                OneStat("C", 1, 96), OneStat("D", 1, 102),
                                OneStat("E", 1, 400)};
  std::vector<Outlier> found = FindOutliers(Summarize(runs), 3.0, 0.25);
  ASSERT_EQ(found.size(), 1u);
  EXPECT_EQ(found[0].branch, "E");
  EXPECT_DOUBLE_EQ(found[0].median, 102);
}

TEST(RunStatsTest, ASmallGapIsNoOutlierHoweverTightTheRest) {
  // Every other branch agrees exactly, so the z is infinite, but 10% off the
  // median isn't worth reading.
  std::vector<RunStats> runs = {OneStat("A", 1, 100), OneStat("B", 1, 100),
                                OneStat("C", 1, 100), OneStat("D", 1, 110)};
  EXPECT_TRUE(FindOutliers(Summarize(runs), 3.0, 0.25).empty());
  EXPECT_EQ(FindOutliers(Summarize(runs), 3.0, 0.05).size(), 1u);
}

TEST(RunStatsTest, AGapWithinSeedNoiseIsNoOutlier) {
  // B's seeds swing 50 to 150 around 100, so A's 130 against the median of 100
  // is within what a reseed does.
  std::vector<RunStats> runs = {OneStat("A", 1, 130), OneStat("A", 2, 130),
                                OneStat("B", 1, 50),  OneStat("B", 2, 150),
                                OneStat("C", 1, 100), OneStat("C", 2, 100),
                                OneStat("D", 1, 100), OneStat("D", 2, 100)};
  EXPECT_TRUE(FindOutliers(Summarize(runs), 0.0, 0.25).empty());
}

TEST(RunStatsTest, AZeroMedianHasNoRatio) {
  std::vector<RunStats> runs = {OneStat("A", 1, 0), OneStat("B", 1, 0),
                                OneStat("C", 1, 5)};
  EXPECT_TRUE(FindOutliers(Summarize(runs), 0.0, 0.0).empty());
}

TEST(RunStatsTest, TwoBranchesHaveNoRest) {
  std::vector<RunStats> runs = {OneStat("A", 1, 1), OneStat("B", 1, 1000)};
  EXPECT_TRUE(FindOutliers(Summarize(runs), 0.0, 0.0).empty());
}

}  // namespace
}  // namespace ms
