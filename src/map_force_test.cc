#include "src/map_force.h"

#include <random>
#include <utility>

#include "gtest/gtest.h"
#include "src/character/character.h"
#include "src/character/stat_preset.h"
#include "src/protos/boss.pb.h"
#include "src/protos/character.pb.h"
#include "src/protos/map.pb.h"

namespace ms {
namespace {

// Each map is checked against the force it names, and a map naming neither
// applies nothing.
TEST(MapForceTest, ReadsTheForceTheMapNames) {
  std::mt19937 rng(1);
  Character proto;
  proto.set_level(260);
  CharacterInstance character(rng, std::move(proto));

  MapData river;
  river.set_arcane_force(30);
  MapForce arcane = MapForceFor(river, character, {});
  EXPECT_EQ(arcane.name, "Arcane Force");
  EXPECT_EQ(arcane.abbreviation, "AF");
  EXPECT_EQ(arcane.required, 30);
  EXPECT_DOUBLE_EQ(arcane.factors.damage_dealt, 0.10);
  EXPECT_TRUE(AsksForForce(river));

  MapData grandis;
  grandis.set_sacred_power(30);
  MapForce sacred = MapForceFor(grandis, character, {});
  EXPECT_EQ(sacred.name, "Sacred Power");
  EXPECT_EQ(sacred.abbreviation, "SAC");
  EXPECT_EQ(sacred.owned, 0);
  EXPECT_DOUBLE_EQ(sacred.factors.damage_dealt, 0.70);
  EXPECT_TRUE(AsksForForce(grandis));

  MapData field;
  MapForce none = MapForceFor(field, character, {});
  EXPECT_TRUE(none.name.empty());
  EXPECT_EQ(none.required, 0);
  EXPECT_DOUBLE_EQ(none.factors.damage_dealt, 1.0);
  EXPECT_DOUBLE_EQ(none.factors.damage_taken, 1.0);
  EXPECT_FALSE(AsksForForce(field));
}

TEST(MapForceTest, ABossWeighsArcaneForceAsAMapDoes) {
  std::mt19937 rng(1);
  Character proto;
  proto.set_level(230);
  CharacterInstance character(rng, std::move(proto));

  BossDifficulty lucid;
  lucid.set_arcane_force(360);
  MapForce force = BossForceFor(lucid, character, {});
  EXPECT_EQ(force.name, "Arcane Force");
  EXPECT_EQ(force.required, 360);
  EXPECT_DOUBLE_EQ(force.factors.damage_dealt, 0.10);

  MapForce none = BossForceFor(BossDifficulty(), character, {});
  EXPECT_EQ(none.required, 0);
  EXPECT_DOUBLE_EQ(none.factors.damage_dealt, 1.0);
}

TEST(MapForceTest, ABossWeighsSacredPowerAsAMapDoes) {
  std::mt19937 rng(1);
  Character proto;
  proto.set_level(260);
  CharacterInstance character(rng, std::move(proto));

  BossDifficulty seren;
  seren.set_sacred_power(200);
  MapForce force = BossForceFor(seren, character, {});
  MapData grandis;
  grandis.set_sacred_power(200);
  MapForce map = MapForceFor(grandis, character, {});
  EXPECT_EQ(force.name, "Sacred Power");
  EXPECT_EQ(force.required, 200);
  EXPECT_DOUBLE_EQ(force.factors.damage_dealt, map.factors.damage_dealt);
  EXPECT_LT(force.factors.damage_dealt, 1.0);
}

// A boss is fought under the Bossing presets, so their Hyper Stat Arcane Force
// counts there and the Farming preset's does not.
TEST(MapForceTest, ABossReadsTheBossingPresetsForce) {
  std::mt19937 rng(1);
  Character proto;
  proto.set_level(260);
  CharacterInstance character(rng, std::move(proto));
  character.set_autoswap_presets(true);
  ASSERT_TRUE(character.AllocateHyperStat(
      HYPER_STAT_FIELD_ARCANE_FORCE, AutoswapSlotFor(Activity::kBossing), 10));

  BossDifficulty lucid;
  lucid.set_arcane_force(360);
  EXPECT_EQ(BossForceFor(lucid, character, {}).owned, 50);
  MapData river;
  river.set_arcane_force(30);
  EXPECT_EQ(MapForceFor(river, character, {}).owned, 0);
}

}  // namespace
}  // namespace ms
