#include "src/character/hyper_plan.h"

#include <random>

#include "gtest/gtest.h"
#include "src/character/character.h"
#include "src/character/stat_preset.h"
#include "src/protos/character.pb.h"

namespace ms {
namespace {

// Each measurement plays a fight in the sims, so a stat the rate never reads
// must cost two measurements, not one per level, and still price at nothing.
TEST(HyperPlanTest, AStatWorthNothingIsNotMeasuredLevelByLevel) {
  std::mt19937 rng(1);
  CharacterInstance character(rng, Character());
  while (character.proto().level() < 200) {
    character.LevelUp();
  }
  int calls = 0;
  const HyperWorth worth = MeasureHyperWorth(
      character, StatPreset::kFirst, [&calls](CharacterInstance& measured) {
        ++calls;
        return 100.0 + measured.hyper_stat_level(HYPER_STAT_FIELD_DAMAGE);
      });

  const int ceiling = character.max_hyper_stat_level();
  int reached = 0;
  for (int level = 1; level <= ceiling; ++level) {
    if (worth.rate[HYPER_STAT_FIELD_DAMAGE][level] > 0.0) {
      EXPECT_EQ(worth.rate[HYPER_STAT_FIELD_DAMAGE][level], level);
      reached = level;
    }
    EXPECT_EQ(worth.rate[HYPER_STAT_FIELD_BOSS_DAMAGE][level], 0.0);
  }
  ASSERT_GT(reached, 1) << "a Lv200 pool reaches past level 1";
  // The bare character, the damage stat's levels, and two for each of the
  // other thirteen, where measuring every level took ten each.
  EXPECT_LE(calls, 1 + ceiling + 2 * 13);
  EXPECT_EQ(character.hyper_stat_level(HYPER_STAT_FIELD_DAMAGE), 0)
      << "the preset is left empty";
}

}  // namespace
}  // namespace ms
