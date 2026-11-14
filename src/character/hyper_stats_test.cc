#include "src/character/hyper_stats.h"

#include <vector>

#include "gtest/gtest.h"
#include "src/protos/character.pb.h"

namespace ms {
namespace {

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
  (*PresetOf(stats, StatPreset::kFirst)
        .mutable_levels())[HYPER_STAT_FIELD_EXP] = 3;
  (*PresetOf(stats, StatPreset::kSecond)
        .mutable_levels())[HYPER_STAT_FIELD_BOSS_DAMAGE] = 4;
  const HyperStats& read = stats;
  EXPECT_EQ(
      HyperStatLevel(PresetOf(read, StatPreset::kFirst), HYPER_STAT_FIELD_EXP),
      3);
  EXPECT_EQ(HyperStatLevel(PresetOf(read, StatPreset::kFirst),
                           HYPER_STAT_FIELD_BOSS_DAMAGE),
            0);
  EXPECT_EQ(HyperStatLevel(PresetOf(read, StatPreset::kSecond),
                           HYPER_STAT_FIELD_BOSS_DAMAGE),
            4);
}

}  // namespace
}  // namespace ms
