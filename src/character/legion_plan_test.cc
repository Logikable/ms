#include "src/character/legion_plan.h"

#include <gtest/gtest.h>

#include <random>
#include <vector>

#include "src/character/character.h"
#include "src/character/character_stats.h"
#include "src/character/legion.h"
#include "src/protos/character.pb.h"
#include "src/protos/legion.pb.h"

namespace ms {
namespace {

int PointsIn(const CharacterInstance& c, LegionStat stat) {
  const LegionPreset& preset = PresetOf(c.legion(), StatPreset::kFirst);
  return preset.points().contains(stat) ? preset.points().at(stat) : 0;
}

// Points go where the rating pays, to the cap; what pays nothing still lands,
// in stat order, so none is wasted.
TEST(LegionPlanTest, SpendsWhereTheRatingPaysThenInOrder) {
  std::mt19937 rng(1);
  Character proto;
  proto.set_level(200);
  proto.set_job(JOB_SWORDMAN);
  CharacterInstance c(rng, std::move(proto));
  c.set_legion_roster(
      std::vector<LegionMember>(50, LegionMember{JOB_HERO, 250}));
  Legion stale;
  (*PresetOf(stale, StatPreset::kFirst).mutable_points())[LEGION_STAT_LUK] = 3;
  c.set_legion(stale);

  const int spent =
      SpendLegion(c, StatPreset::kFirst, [](CharacterInstance& character) {
        const DerivedStats stats = DerivedStatsFor(character, {});
        return stats.boss_pct + 0.5 * stats.crit_dmg;
      });
  // Supreme I: 41 members.
  EXPECT_EQ(spent, 41 * 5);
  EXPECT_EQ(PointsIn(c, LEGION_STAT_BOSS_DAMAGE), 40);
  EXPECT_EQ(PointsIn(c, LEGION_STAT_CRIT_DAMAGE), 40);
  EXPECT_EQ(PointsIn(c, LEGION_STAT_STR), kLegionBaseStatCap);
  EXPECT_EQ(PointsIn(c, LEGION_STAT_LUK), kLegionBaseStatCap);
  EXPECT_EQ(LegionPointsSpent(PresetOf(c.legion(), StatPreset::kSecond)), 0);
}

TEST(LegionPlanTest, NothingWhileLocked) {
  std::mt19937 rng(1);
  Character proto;
  proto.set_level(149);
  proto.set_job(JOB_HERO);
  CharacterInstance c(rng, std::move(proto));
  EXPECT_EQ(SpendLegion(c, StatPreset::kFirst,
                        [](CharacterInstance&) { return 0.0; }),
            0);
}

}  // namespace
}  // namespace ms
