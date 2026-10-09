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

TEST(LegionTest, CharacterRanksAndPointsFollowGms) {
  EXPECT_EQ(CharacterRankFor(59), CharacterRank::kNone);
  EXPECT_EQ(CharacterRankFor(60), CharacterRank::kB);
  EXPECT_EQ(CharacterRankFor(99), CharacterRank::kB);
  EXPECT_EQ(CharacterRankFor(100), CharacterRank::kA);
  EXPECT_EQ(CharacterRankFor(140), CharacterRank::kS);
  EXPECT_EQ(CharacterRankFor(200), CharacterRank::kSS);
  EXPECT_EQ(CharacterRankFor(249), CharacterRank::kSS);
  EXPECT_EQ(CharacterRankFor(250), CharacterRank::kSSS);
  EXPECT_EQ(CharacterRankName(CharacterRank::kSSS), "SSS");
  EXPECT_EQ(LegionPointsFor(CharacterRank::kNone), 0);
  EXPECT_EQ(LegionPointsFor(CharacterRank::kB), 1);
  EXPECT_EQ(LegionPointsFor(CharacterRank::kSSS), 5);
}

TEST(LegionTest, RanksAreEvery500Levels) {
  EXPECT_EQ(LegionRankFor(499), 0);
  EXPECT_EQ(LegionRankName(0), "");
  EXPECT_EQ(LegionRankFor(500), 1);
  EXPECT_EQ(LegionRankName(1), "Nameless Legion Rank I");
  EXPECT_EQ(LegionRankName(LegionRankFor(2500)), "Nameless Legion Rank V");
  EXPECT_EQ(LegionRankName(LegionRankFor(3000)), "Renowned Legion Rank I");
  EXPECT_EQ(LegionRankName(LegionRankFor(5500)), "Heroic Legion Rank I");
  EXPECT_EQ(LegionRankName(LegionRankFor(8000)), "Legendary Legion Rank I");
  EXPECT_EQ(LegionRankName(LegionRankFor(10500)), "Supreme Legion Rank I");
  EXPECT_EQ(LegionRankFor(99999), kLegionRanks);
}

TEST(LegionTest, MemberSlotsFollowGmsTable) {
  EXPECT_EQ(LegionMemberSlots(0), 9);
  EXPECT_EQ(LegionMemberSlots(1), 9);
  EXPECT_EQ(LegionMemberSlots(5), 13);
  EXPECT_EQ(LegionMemberSlots(6), 18);
  EXPECT_EQ(LegionMemberSlots(11), 27);
  EXPECT_EQ(LegionMemberSlots(16), 36);
  EXPECT_EQ(LegionMemberSlots(21), 41);
  EXPECT_EQ(LegionMemberSlots(25), 45);
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

TEST(LegionTest, JobEffectsByLine) {
  EXPECT_EQ(LegionJobEffectFor(JOB_HERO, CharacterRank::kSSS).str, 100);
  EXPECT_EQ(LegionJobEffectFor(JOB_FIGHTER, CharacterRank::kB).str, 10);
  EXPECT_EQ(LegionJobEffectFor(JOB_PALADIN, CharacterRank::kSS).str, 80);
  EXPECT_DOUBLE_EQ(
      LegionJobEffectFor(JOB_DARK_KNIGHT, CharacterRank::kSSS).max_hp_pct,
      0.06);
  EXPECT_EQ(
      LegionJobEffectFor(JOB_FIRE_POISON_ARCH_MAGE, CharacterRank::kS).int_,
      40);
  EXPECT_EQ(LegionJobEffectFor(JOB_BISHOP, CharacterRank::kA).int_, 20);
  EXPECT_EQ(LegionJobEffectFor(JOB_BOW_MASTER, CharacterRank::kSSS).dex, 100);
  EXPECT_DOUBLE_EQ(
      LegionJobEffectFor(JOB_NIGHT_LORD, CharacterRank::kSSS).crit_rate, 0.05);
  EXPECT_DOUBLE_EQ(
      LegionJobEffectFor(JOB_MARKSMAN, CharacterRank::kB).crit_rate, 0.01);
  EXPECT_EQ(LegionJobEffectFor(JOB_SHADOWER, CharacterRank::kSSS).luk, 100);
  // No line yet, no effect.
  EXPECT_EQ(LegionJobEffectFor(JOB_SWORDMAN, CharacterRank::kSSS).str, 0);
  EXPECT_EQ(LegionJobEffectFor(JOB_HERO, CharacterRank::kNone).str, 0);
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
