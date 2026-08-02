#include "src/game_state.h"

#include <gtest/gtest.h>

#include <map>
#include <string>

#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/map.pb.h"
#include "src/protos/mob.pb.h"
#include "src/protos/scroll.pb.h"

namespace ms {
namespace {

GameState MakeState() {
  return GameState({}, {}, {}, {}, {});
}

// A catalog holding the one equip both modes hand out, so the seeding has
// something to find.
std::map<std::string, EquipPrototype> SwordCatalog() {
  EquipPrototype sword;
  sword.set_name("Sword");
  sword.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  return {{"sword", sword}};
}

GameState MakeTestModeState() {
  return GameState(SwordCatalog(), {}, {}, {}, {}, {}, GameMode::kTest);
}

GameState MakePlayModeState() {
  return GameState(SwordCatalog(), {}, {}, {}, {}, {}, GameMode::kPlay);
}

TEST(GameStateTest, ConstructorStoresEquipsMap) {
  EquipPrototype proto;
  proto.set_name("Sword");
  GameState state({{"sword", proto}}, {}, {}, {}, {});
  ASSERT_TRUE(state.equips.count("sword"));
  EXPECT_EQ(state.equips.at("sword").name(), "Sword");
}

TEST(GameStateTest, ConstructorStoresScrollsMap) {
  Scroll scroll;
  scroll.set_name("60% ATT");
  GameState state({}, {{"att_60", scroll}}, {}, {}, {});
  ASSERT_TRUE(state.scrolls.count("att_60"));
  EXPECT_EQ(state.scrolls.at("att_60").name(), "60% ATT");
}

TEST(GameStateTest, ConstructorStoresItemsMap) {
  ItemPrototype item;
  item.set_name("Green Snail Shell");
  GameState state({}, {}, {{"green_snail_shell", item}}, {}, {});
  ASSERT_TRUE(state.items.count("green_snail_shell"));
  EXPECT_EQ(state.items.at("green_snail_shell").name(), "Green Snail Shell");
}

TEST(GameStateTest, ConstructorStoresMobsMap) {
  Mob mob;
  mob.set_name("Snail");
  GameState state({}, {}, {}, {{"snail", mob}}, {});
  ASSERT_TRUE(state.mobs.count("snail"));
  EXPECT_EQ(state.mobs.at("snail").name(), "Snail");
}

TEST(GameStateTest, ConstructorStoresMapsMap) {
  MapData map;
  map.set_name("Right Around Lith Harbor");
  GameState state({}, {}, {}, {}, {{"lith", map}});
  ASSERT_TRUE(state.maps.count("lith"));
  EXPECT_EQ(state.maps.at("lith").name(), "Right Around Lith Harbor");
}

TEST(GameStateTest, TestModeCharacterIsLevel10) {
  GameState state = MakeTestModeState();
  EXPECT_EQ(state.character.proto().level(), 10);
}

TEST(GameStateTest, TestModeCharacterStandsAtItsFirstAdvancement) {
  // The workbench opens on the choice: a Beginner that has reached level 10
  // and taken nothing yet.
  GameState state = MakeTestModeState();
  EXPECT_EQ(state.character.proto().job(), JOB_BEGINNER);
  EXPECT_EQ(state.character.proto().job_stage(), 0);
  EXPECT_TRUE(state.character.CanAdvanceJob());
}

TEST(GameStateTest, TestModeCharacterHasLeveledAp) {
  // Leveling 1->10 grants 5 AP each. No SP yet: the 1st-job pool starts at
  // level 11, once there is a job to spend it on.
  GameState state = MakeTestModeState();
  EXPECT_EQ(state.character.proto().ap(), 45);
  EXPECT_EQ(state.character.sp(1), 0);
}

TEST(GameStateTest, TestModeCharacterStats) {
  GameState state = MakeTestModeState();
  const AllocatedStats& s = state.character.proto().allocated_stats();
  EXPECT_EQ(s.str(), 13);
  EXPECT_EQ(s.dex(), 4);
  EXPECT_EQ(s.int_(), 4);
  EXPECT_EQ(s.luk(), 4);
  // Nine level-ups at the Beginner's rate, all of them before any job could
  // change it.
  EXPECT_EQ(s.hp(), 50 + 9 * 36);
  EXPECT_EQ(s.mp(), 15 + 9 * 24);
}

// --- play mode ---

TEST(GameStateTest, PlayModeStartsAtLevelOne) {
  GameState state = MakePlayModeState();
  EXPECT_EQ(state.character.proto().level(), 1);
  EXPECT_EQ(state.character.proto().job(), JOB_BEGINNER);
  EXPECT_FALSE(state.character.CanAdvanceJob());
}

TEST(GameStateTest, PlayModeStartsWithTheBeginnerSpread) {
  GameState state = MakePlayModeState();
  const AllocatedStats& s = state.character.proto().allocated_stats();
  EXPECT_EQ(s.str(), 13);
  EXPECT_EQ(s.dex(), 4);
  EXPECT_EQ(s.int_(), 4);
  EXPECT_EQ(s.luk(), 4);
  EXPECT_EQ(state.character.proto().ap(), 0);
}

TEST(GameStateTest, PlayModeStartsWithNoMeso) {
  EXPECT_EQ(MakePlayModeState().character.meso(), 0);
}

// Armed but carrying nothing: the Sword is worn, so the bag really is empty.
TEST(GameStateTest, PlayModeStartsWearingASwordAndHoldingNothing) {
  GameState state = MakePlayModeState();
  EXPECT_TRUE(state.character.inventory().empty());
  ASSERT_TRUE(state.character.equipped().count(EQUIP_SLOT_PRIMARY_WEAPON));
  EXPECT_EQ(state.character.equipped().at(EQUIP_SLOT_PRIMARY_WEAPON).name(),
            "Sword");
}

TEST(GameStateTest, PlayModeStartsOnMapleIsland) {
  EXPECT_EQ(MakePlayModeState().current_map, "maple_island");
}

// --- test mode ---

TEST(GameStateTest, TestModeStartsWithMesoAndAFullBag) {
  GameState state = MakeTestModeState();
  EXPECT_EQ(state.character.meso(), 1000000);
  EXPECT_FALSE(state.character.inventory().empty());
}

TEST(GameStateTest, TestModeStartsOnAHuntingGround) {
  EXPECT_EQ(MakeTestModeState().current_map, "right_around_lith_harbor");
}

// Neither mode may insist on a catalog entry: a state built for a test carries
// no data files, and seeding must not fall over on that.
TEST(GameStateTest, SeedingSkipsEquipsTheCatalogDoesNotHave) {
  GameState play({}, {}, {}, {}, {}, {}, GameMode::kPlay);
  EXPECT_TRUE(play.character.inventory().empty());
  EXPECT_TRUE(play.character.equipped().empty());
  GameState test({}, {}, {}, {}, {}, {}, GameMode::kTest);
  EXPECT_TRUE(test.character.inventory().empty());
  // The meso does not depend on the catalog, so it still arrives.
  EXPECT_EQ(test.character.meso(), 1000000);
}

// Play is what an unadorned construction gives, so the game's default is the
// game rather than the workbench.
TEST(GameStateTest, PlayIsTheDefaultMode) {
  GameState state(SwordCatalog(), {}, {}, {}, {});
  EXPECT_EQ(state.character.proto().level(), 1);
  EXPECT_EQ(state.current_map, "maple_island");
}

// The workbench climbs the level ladder on a bonus rather than by farming the
// early levels at play speed. Play mode earns what it earns.
TEST(GameStateTest, TestModeFarmsOnAnExpBonus) {
  EXPECT_EQ(MakeTestModeState().exp_multiplier, 5);
}

TEST(GameStateTest, PlayModeEarnsPlainExp) {
  EXPECT_EQ(MakePlayModeState().exp_multiplier, 1);
}

}  // namespace
}  // namespace ms
