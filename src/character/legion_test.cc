#include "src/character/legion.h"

#include <gtest/gtest.h>

#include <vector>

#include "src/protos/character.pb.h"
#include "src/protos/legion.pb.h"

namespace ms {
namespace {

std::vector<LegionMember> Many(int count, Job job, int level) {
  return std::vector<LegionMember>(count, LegionMember{job, level});
}

TEST(LegionTest, ExpandedCapsOpenWithRank) {
  EXPECT_EQ(LegionStatCap(LEGION_STAT_STR, 0), kLegionBaseStatCap);
  EXPECT_EQ(LegionStatCap(LEGION_STAT_MAX_MP, 25), kLegionBaseStatCap);
  EXPECT_EQ(LegionStatCap(LEGION_STAT_IED, 3), 0);
  EXPECT_EQ(LegionStatCap(LEGION_STAT_IED, 4), 6);
  EXPECT_EQ(LegionStatCap(LEGION_STAT_IED, 6), 13);
  EXPECT_EQ(LegionStatCap(LEGION_STAT_IED, 8), 21);
  EXPECT_EQ(LegionStatCap(LEGION_STAT_IED, 10), 30);
  EXPECT_EQ(LegionStatCap(LEGION_STAT_IED, 11), 30);
  EXPECT_EQ(LegionStatCap(LEGION_STAT_IED, 12), 40);
  EXPECT_EQ(LegionStatCap(LEGION_STAT_CRIT_DAMAGE, 25), 40);
}

// Every stat must have a value; one left at 0 would sell points for nothing.
TEST(LegionTest, EveryStatIsWorthSomething) {
  for (int i = LegionStat_MIN + 1; i <= LegionStat_MAX; ++i) {
    EXPECT_GT(LegionPerPoint(static_cast<LegionStat>(i)), 0.0) << i;
  }
}

TEST(LegionTest, OnlyRankedCharactersCount) {
  const LegionSummary summary =
      SummarizeLegion({{JOB_HERO, 150}, {JOB_PALADIN, 59}, {JOB_BEGINNER, 10}});
  EXPECT_EQ(summary.legion_level, 150);
  EXPECT_EQ(summary.members, 1);
  EXPECT_EQ(summary.points, 3);
  EXPECT_EQ(summary.job_effects.str, 40);
}

// The two halves of the 42-character question. The Legion level sums the top
// 42 and points come from the member count, which reaches 45 at Supreme V; the
// job effects come from everyone.
TEST(LegionTest, PastFortyTwoOnlyJobEffectsAndTheLastSlotsPay) {
  std::vector<LegionMember> roster = Many(50, JOB_HERO, 300);
  LegionSummary summary = SummarizeLegion(roster);
  EXPECT_EQ(summary.legion_level, 42 * 300);
  EXPECT_EQ(summary.rank, kLegionRanks);
  EXPECT_EQ(summary.members, 45);
  EXPECT_EQ(summary.points, 45 * 5);
  EXPECT_EQ(summary.job_effects.str, 50 * 100);

  // A 51st character changes nothing but their own job effect.
  roster.push_back({JOB_BISHOP, 60});
  summary = SummarizeLegion(roster);
  EXPECT_EQ(summary.legion_level, 42 * 300);
  EXPECT_EQ(summary.points, 45 * 5);
  EXPECT_EQ(summary.job_effects.int_, 10);
}

// The highest levels take the member slots, so a low character past the count
// gives no points.
TEST(LegionTest, MemberSlotsTakeTheHighestLevels) {
  std::vector<LegionMember> roster = Many(9, JOB_HERO, 60);
  roster.push_back({JOB_BISHOP, 100});
  const LegionSummary summary = SummarizeLegion(roster);
  EXPECT_EQ(summary.rank, 1);
  EXPECT_EQ(summary.members, 9);
  EXPECT_EQ(summary.points, 2 + 8 * 1);

  const LegionSummary capped = SummarizeLegion(Many(10, JOB_HERO, 60));
  EXPECT_EQ(capped.rank, 1);
  EXPECT_EQ(capped.members, 9);
  EXPECT_EQ(capped.points, 9);
  EXPECT_EQ(capped.job_effects.str, 100);
}

TEST(LegionTest, SpendingRespectsCapsAndPoints) {
  Legion legion;
  LegionSummary summary;
  summary.points = 20;
  summary.rank = 4;
  EXPECT_EQ(SpendLegionPoints(legion, StatPreset::kFirst, LEGION_STAT_STR, 99,
                              summary),
            kLegionBaseStatCap);
  EXPECT_EQ(SpendLegionPoints(legion, StatPreset::kFirst, LEGION_STAT_IED, 99,
                              summary),
            5);
  EXPECT_EQ(LegionPointsSpent(PresetOf(legion, StatPreset::kFirst)), 20);
  EXPECT_EQ(SpendLegionPoints(legion, StatPreset::kFirst, LEGION_STAT_DEX, 1,
                              summary),
            0);
  EXPECT_EQ(SpendLegionPoints(legion, StatPreset::kFirst, LEGION_STAT_STR, -99,
                              summary),
            -kLegionBaseStatCap);
  EXPECT_FALSE(
      PresetOf(legion, StatPreset::kFirst).points().contains(LEGION_STAT_STR));
  // The other presets are their own.
  EXPECT_EQ(SpendLegionPoints(legion, StatPreset::kSecond, LEGION_STAT_DEX, 3,
                              summary),
            3);
  ResetLegionPreset(legion, StatPreset::kFirst);
  EXPECT_EQ(LegionPointsSpent(PresetOf(legion, StatPreset::kFirst)), 0);
  EXPECT_EQ(LegionPointsSpent(PresetOf(legion, StatPreset::kSecond)), 3);
}

// A Legion that shrinks keeps the allocation but reads less of it.
TEST(LegionTest, EffectivePointsCutToCapsThenPointsInStatOrder) {
  LegionPreset preset;
  (*preset.mutable_points())[LEGION_STAT_IED] = 40;
  (*preset.mutable_points())[LEGION_STAT_LUK] = 15;
  (*preset.mutable_points())[LEGION_STAT_ATTACK] = 15;

  std::map<LegionStat, int> effective = EffectiveLegionPoints(preset, 12, 100);
  EXPECT_EQ(effective[LEGION_STAT_IED], 40);

  effective = EffectiveLegionPoints(preset, 6, 100);
  EXPECT_EQ(effective[LEGION_STAT_IED], 13);

  effective = EffectiveLegionPoints(preset, 6, 20);
  EXPECT_EQ(effective[LEGION_STAT_LUK], 15);
  EXPECT_EQ(effective[LEGION_STAT_ATTACK], 5);
  EXPECT_EQ(effective.count(LEGION_STAT_IED), 0);
  EXPECT_EQ(LegionPointsSpent(preset), 70);
}

TEST(LegionTest, SwappingCarriesTheInUseMarker) {
  Legion legion;
  LegionSummary summary;
  summary.points = 10;
  SpendLegionPoints(legion, StatPreset::kThird, LEGION_STAT_INT, 4, summary);
  legion.set_slot_in_use(IndexOf(StatPreset::kThird));
  SwapLegionPresets(legion, StatPreset::kFirst, StatPreset::kThird);
  EXPECT_EQ(LegionPointsSpent(PresetOf(legion, StatPreset::kFirst)), 4);
  EXPECT_EQ(LegionPointsSpent(PresetOf(legion, StatPreset::kThird)), 0);
  EXPECT_EQ(legion.slot_in_use(), IndexOf(StatPreset::kFirst));
}

}  // namespace
}  // namespace ms
