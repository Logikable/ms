#include "analysis/legion_plan.h"

#include <vector>

#include "gtest/gtest.h"
#include "src/character/character_stats.h"
#include "src/character/legion.h"
#include "src/game_state.h"
#include "src/protos/character.pb.h"
#include "src/protos/legion.pb.h"

namespace ms {
namespace {

// Moved only by boss damage and crit damage, the second worth half as much.
double BossPower(GameState& state) {
  DerivedStats derived = DerivedStatsFor(state.character, state.skills, {}, {},
                                         Activity::kBossing);
  return derived.boss_pct + 0.5 * derived.crit_dmg;
}

// Makes the main level 200 beside `heroes` level-250 Heroes, autoswap on.
void Account(GameState& state, int heroes) {
  Character proto = state.character.ToProto();
  proto.set_level(200);
  state.character.RestoreFrom(proto, state.equips, state.items);
  state.character.set_autoswap_presets(true);
  state.character.set_legion_roster(
      std::vector<LegionMember>(heroes, LegionMember{JOB_HERO, 250}));
}

int PointsIn(const CharacterInstance& c, StatPreset slot, LegionStat stat) {
  const LegionPreset& preset = PresetOf(c.legion(), slot);
  return preset.points().contains(stat) ? preset.points().at(stat) : 0;
}

TEST(LegionPlanTest, MeasuresPerPointAndLeavesTheCharacterAsItWas) {
  GameState state({}, {}, {}, {}, {});
  Account(state, 50);
  Legion kept;
  PresetOf(kept, StatPreset::kSecond);
  (*PresetOf(kept, StatPreset::kSecond).mutable_points())[LEGION_STAT_DEX] = 2;
  state.character.set_legion(kept);
  const LegionWorth worth =
      MeasureLegionWorth(state, StatPreset::kSecond, BossPower);
  EXPECT_NEAR(worth.per_point[LEGION_STAT_BOSS_DAMAGE], 0.01, 1e-9);
  EXPECT_NEAR(worth.per_point[LEGION_STAT_CRIT_DAMAGE], 0.0025, 1e-9);
  EXPECT_DOUBLE_EQ(worth.per_point[LEGION_STAT_STR], 0.0);
  EXPECT_EQ(PointsIn(state.character, StatPreset::kSecond, LEGION_STAT_DEX), 2);
}

// Best per point first, each to its cap; what pays nothing fills in order.
TEST(LegionPlanTest, SpendsTheBestFirstAndWastesNothing) {
  GameState state({}, {}, {}, {}, {});
  Account(state, 50);
  LegionWorth worth;
  worth.per_point[LEGION_STAT_CRIT_DAMAGE] = 2.0;
  worth.per_point[LEGION_STAT_IED] = 1.0;
  EXPECT_EQ(SpendLegionByWorth(state.character, StatPreset::kFirst, worth),
            41 * 5);
  EXPECT_EQ(
      PointsIn(state.character, StatPreset::kFirst, LEGION_STAT_CRIT_DAMAGE),
      40);
  EXPECT_EQ(PointsIn(state.character, StatPreset::kFirst, LEGION_STAT_IED), 40);
  EXPECT_EQ(PointsIn(state.character, StatPreset::kFirst, LEGION_STAT_STR),
            kLegionBaseStatCap);
}

// Few points go to the best stat alone.
TEST(LegionPlanTest, AShortPurseGoesToTheBest) {
  GameState state({}, {}, {}, {}, {});
  Account(state, 1);
  LegionWorth worth;
  worth.per_point[LEGION_STAT_ATTACK] = 3.0;
  worth.per_point[LEGION_STAT_LUK] = 1.0;
  // One Hero at 250 and the main at 200: 9 points.
  EXPECT_EQ(SpendLegionByWorth(state.character, StatPreset::kFirst, worth), 9);
  EXPECT_EQ(PointsIn(state.character, StatPreset::kFirst, LEGION_STAT_ATTACK),
            9);
  EXPECT_EQ(PointsIn(state.character, StatPreset::kFirst, LEGION_STAT_LUK), 0);
}

}  // namespace
}  // namespace ms
