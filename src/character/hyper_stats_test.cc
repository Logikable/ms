#include "src/character/hyper_stats.h"

#include <vector>

#include "gtest/gtest.h"
#include "src/protos/character.pb.h"

namespace ms {
namespace {

TEST(HyperStatsTest, PointsPerLevelClimbEveryTenLevels) {
  EXPECT_EQ(HyperStatPointsAtLevel(139), 0);
  EXPECT_EQ(HyperStatPointsAtLevel(140), 3);
  EXPECT_EQ(HyperStatPointsAtLevel(149), 3);
  EXPECT_EQ(HyperStatPointsAtLevel(150), 4);
  EXPECT_EQ(HyperStatPointsAtLevel(190), 8);
  EXPECT_EQ(HyperStatPointsAtLevel(200), 9);
  EXPECT_EQ(HyperStatPointsAtLevel(300), 19);
}

// GMS's own totals: 3 at the unlock level, 34 at 150, 339 at the level cap
// and 1,699 at 300.
TEST(HyperStatsTest, TotalPointsMatchTheTable) {
  EXPECT_EQ(TotalHyperStatPoints(139), 0);
  EXPECT_EQ(TotalHyperStatPoints(140), 3);
  EXPECT_EQ(TotalHyperStatPoints(150), 34);
  EXPECT_EQ(TotalHyperStatPoints(199), 330);
  EXPECT_EQ(TotalHyperStatPoints(200), 339);
  EXPECT_EQ(TotalHyperStatPoints(300), 1699);
}

TEST(HyperStatsTest, LevelCostsAndTheirRunningTotal) {
  const std::vector<int> costs = {1,  2,  4,  8,  10, 15, 20, 25,
                                  30, 35, 50, 65, 80, 95, 110};
  const std::vector<int> totals = {1,   3,   7,   15,  25,  40,  60, 85,
                                   115, 150, 200, 265, 345, 440, 550};
  for (int level = 1; level <= kMaxHyperStatLevel; ++level) {
    EXPECT_EQ(HyperStatLevelCost(level), costs[level - 1]) << "level " << level;
    EXPECT_EQ(HyperStatTotalCost(level), totals[level - 1])
        << "level " << level;
  }
  EXPECT_EQ(HyperStatLevelCost(0), 0);
  EXPECT_EQ(HyperStatLevelCost(16), 0);
  EXPECT_EQ(HyperStatTotalCost(0), 0);
}

// Ten until a character takes a 5th job, which none of them does.
TEST(HyperStatsTest, MaxLevelWaitsOnTheFifthJob) {
  EXPECT_EQ(MaxHyperStatLevel(4), 10);
  EXPECT_EQ(MaxHyperStatLevel(kFifthJobStage), 15);
}

TEST(HyperStatsTest, ArcaneForceIsTheOneStatHeldBack) {
  EXPECT_FALSE(HyperStatUnlocked(HYPER_STAT_FIELD_DAMAGE, 139));
  EXPECT_TRUE(HyperStatUnlocked(HYPER_STAT_FIELD_DAMAGE, 140));
  EXPECT_FALSE(HyperStatUnlocked(HYPER_STAT_FIELD_ARCANE_FORCE, 199));
  EXPECT_TRUE(HyperStatUnlocked(HYPER_STAT_FIELD_ARCANE_FORCE, 200));
  EXPECT_FALSE(HyperStatUnlocked(HYPER_STAT_FIELD_UNSPECIFIED, 200));
}

// Every ladder, level by level, against the wiki's tables.
TEST(HyperStatsTest, BonusLaddersMatchTheTables) {
  const std::vector<std::pair<HyperStatField, std::vector<double>>> ladders = {
      {HYPER_STAT_FIELD_STR,
       {30, 60, 90, 120, 150, 180, 210, 240, 270, 300, 330, 360, 390, 420,
        450}},
      {HYPER_STAT_FIELD_MAX_HP,
       {2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30}},
      {HYPER_STAT_FIELD_CRIT_RATE,
       {1, 2, 3, 4, 5, 7, 9, 11, 13, 15, 17, 19, 21, 23, 25}},
      {HYPER_STAT_FIELD_CRIT_DAMAGE,
       {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}},
      {HYPER_STAT_FIELD_IED,
       {3, 6, 9, 12, 15, 18, 21, 24, 27, 30, 33, 36, 39, 42, 45}},
      {HYPER_STAT_FIELD_BOSS_DAMAGE,
       {3, 6, 9, 12, 15, 19, 23, 27, 31, 35, 39, 43, 47, 51, 55}},
      {HYPER_STAT_FIELD_EXP,
       {0.5, 1, 1.5, 2, 2.5, 3, 3.5, 4, 4.5, 5, 6, 7, 8, 9, 10}},
      {HYPER_STAT_FIELD_ARCANE_FORCE,
       {5, 10, 15, 20, 25, 30, 35, 40, 45, 50, 60, 70, 80, 90, 100}},
  };
  for (const std::pair<HyperStatField, std::vector<double>>& ladder : ladders) {
    EXPECT_EQ(HyperStatBonus(ladder.first, 0), 0.0);
    for (int level = 1; level <= kMaxHyperStatLevel; ++level) {
      EXPECT_DOUBLE_EQ(HyperStatBonus(ladder.first, level),
                       ladder.second[level - 1])
          << ladder.first << " level " << level;
    }
  }
  // The four stats share one ladder, as do the two damage ladders.
  EXPECT_DOUBLE_EQ(HyperStatBonus(HYPER_STAT_FIELD_LUK, 10), 300.0);
  EXPECT_DOUBLE_EQ(HyperStatBonus(HYPER_STAT_FIELD_NORMAL_DAMAGE, 10), 35.0);
  EXPECT_DOUBLE_EQ(HyperStatBonus(HYPER_STAT_FIELD_DAMAGE, 10), 30.0);
  EXPECT_DOUBLE_EQ(HyperStatBonus(HYPER_STAT_FIELD_ATTACK, 10), 30.0);
  EXPECT_DOUBLE_EQ(HyperStatBonus(HYPER_STAT_FIELD_UNSPECIFIED, 10), 0.0);
}

// Past the top of the table a stat stops climbing rather than running off it.
TEST(HyperStatsTest, BonusHoldsAtTheTopRung) {
  EXPECT_DOUBLE_EQ(HyperStatBonus(HYPER_STAT_FIELD_CRIT_RATE, 99),
                   HyperStatBonus(HYPER_STAT_FIELD_CRIT_RATE, 15));
}

TEST(HyperStatsTest, SpentPointsAreTheCostOfEveryLevel) {
  HyperStatPreset preset;
  EXPECT_EQ(HyperStatPointsSpent(preset), 0);
  EXPECT_EQ(HyperStatLevel(preset, HYPER_STAT_FIELD_STR), 0);

  (*preset.mutable_levels())[HYPER_STAT_FIELD_STR] = 10;
  (*preset.mutable_levels())[HYPER_STAT_FIELD_BOSS_DAMAGE] = 5;
  EXPECT_EQ(HyperStatLevel(preset, HYPER_STAT_FIELD_STR), 10);
  EXPECT_EQ(HyperStatPointsSpent(preset), 175);
}

TEST(HyperStatsTest, PresetsAreToldApart) {
  HyperStats stats;
  (*PresetOf(stats, StatPreset::kFarming)
        .mutable_levels())[HYPER_STAT_FIELD_EXP] = 3;
  (*PresetOf(stats, StatPreset::kBossing)
        .mutable_levels())[HYPER_STAT_FIELD_BOSS_DAMAGE] = 4;
  const HyperStats& read = stats;
  EXPECT_EQ(HyperStatLevel(PresetOf(read, StatPreset::kFarming),
                           HYPER_STAT_FIELD_EXP),
            3);
  EXPECT_EQ(HyperStatLevel(PresetOf(read, StatPreset::kFarming),
                           HYPER_STAT_FIELD_BOSS_DAMAGE),
            0);
  EXPECT_EQ(HyperStatLevel(PresetOf(read, StatPreset::kBossing),
                           HYPER_STAT_FIELD_BOSS_DAMAGE),
            4);
}

}  // namespace
}  // namespace ms
