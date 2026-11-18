#include "src/sacred_dailies.h"

#include <gtest/gtest.h>

#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "src/game_state.h"
#include "src/roster.h"
#include "src/save/save.h"
#include "src/testing/data_files.h"

namespace ms {
namespace {

std::unique_ptr<GameState> RealState() {
  return std::make_unique<GameState>(
      LoadTestData<EquipPrototype>("equip"), LoadTestData<Scroll>("scrolls"),
      LoadTestData<ItemPrototype>("items"), LoadTestData<Mob>("mobs"),
      LoadTestData<MapData>("maps"), LoadTestData<Skill>("skills"),
      GameMode::kPlay, TestOptions(), /*seed=*/1u,
      LoadTestData<EquipSet>("sets"), LoadTestData<Boss>("bosses"));
}

// The three respawn intervals at 10x game speed, 21.6s, 37.8s and 75.6s, pay
// 100%, 80% and nothing, linear between.
TEST(SacredShareTest, TheRespawnIntervalsAreTheTiers) {
  EXPECT_DOUBLE_EQ(SacredShare(10.0, 10.0), 1.0);
  EXPECT_NEAR(SacredShare(21.6, 10.0), 1.0, 1e-9);
  EXPECT_NEAR(SacredShare(29.7, 10.0), 0.9, 1e-9);
  EXPECT_NEAR(SacredShare(37.8, 10.0), 0.8, 1e-9);
  EXPECT_NEAR(SacredShare(56.7, 10.0), 0.4, 1e-9);
  EXPECT_NEAR(SacredShare(75.6, 10.0), 0.0, 1e-9);
  EXPECT_EQ(SacredShare(80.0, 10.0), 0.0);
  EXPECT_NEAR(SacredShare(3.78, 1.0), 0.8, 1e-9) << "scales with game speed";
}

// Each area is farmed where its symbol drops, on the map asking the least
// Sacred Power: Geardock drops in two places.
TEST(SacredSymbolMapTest, TheEasiestMapDroppingTheSymbol) {
  std::unique_ptr<GameState> state = RealState();
  EXPECT_EQ(SacredSymbolMap(*state, "symbol_cernium"),
            "cernium_eastern_city_ramparts_1");
  EXPECT_EQ(SacredSymbolMap(*state, "symbol_geardock"), "b2_zone_1");
  EXPECT_EQ(SacredSymbolMap(*state, "symbol_arcana"), "")
      << "an Arcane Symbol drops off Grandis";
}

// A character who can't farm Cernium is measured there and nowhere harder.
TEST(MeasureSacredAreasTest, TheFirstAreaThatPaysNothingEndsTheRun) {
  std::unique_ptr<GameState> state = RealState();
  std::vector<SacredArea> areas = MeasureSacredAreas(*state);
  ASSERT_EQ(areas.size(), 1u);
  EXPECT_EQ(areas[0].map, "cernium_eastern_city_ramparts_1");
  EXPECT_EQ(areas[0].share, 0.0);
  EXPECT_TRUE(SacredShares(areas).empty());
}

// The copy holds whoever is played, not the offline character the load opens.
TEST(RestoreCopyTest, TheCopyPlaysTheSameCharacter) {
  std::unique_ptr<GameState> state = RealState();
  CreateCharacter(*state);
  state->character.LevelUp();
  state->character.LevelUp();
  ASSERT_EQ(state->played_slot, 1);

  std::unique_ptr<GameState> copy =
      RestoreCopy(*state, SaveGameToBytes(*state));
  ASSERT_NE(copy, nullptr);
  EXPECT_EQ(copy->played_slot, 1);
  EXPECT_EQ(copy->character.proto().level(), 3);
  EXPECT_EQ(copy->current_map, state->current_map);
}

// The worker's answer is the measurement's, taken on a copy on another thread,
// and only from the 6th advancement.
TEST(SacredDailiesWorkerTest, TheBackgroundRunMatchesAMeasurementHere) {
  TestOptions test;
  test.job = kTestAdvancement;
  test.level = 260;
  test.skills = TestSkills::kMax;
  test.equips.scrolled = true;
  test.equips.stars = 17;
  GameState state(
      LoadTestData<EquipPrototype>("equip"), LoadTestData<Scroll>("scrolls"),
      LoadTestData<ItemPrototype>("items"), LoadTestData<Mob>("mobs"),
      LoadTestData<MapData>("maps"), LoadTestData<Skill>("skills"),
      GameMode::kTest, test, /*seed=*/1u, LoadTestData<EquipSet>("sets"),
      LoadTestData<Boss>("bosses"));
  const auto now = std::chrono::steady_clock::now();
  SacredDailiesWorker worker;
  worker.Start(state, now);
  std::map<std::string, double> shares = worker.Shares(state, now);
  if (state.character.proto().job_stage() < 6) {
    EXPECT_TRUE(shares.empty());
    return;
  }
  EXPECT_FALSE(shares.empty()) << "a workbench character farms Cernium";
  EXPECT_EQ(shares, SacredShares(MeasureSacredAreas(state)));
}

}  // namespace
}  // namespace ms
