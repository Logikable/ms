#include "src/game_state.h"

#include <gtest/gtest.h>

#include <map>
#include <string>
#include <vector>

#include "src/character/exp_table.h"
#include "src/character/progression.h"
#include "src/item/item.h"
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

// A catalog holding the equip both modes hand out, plus one more that only
// the workbench asks for -- so the seeding has something to find, and test
// mode has something left in the bag once it is wearing the first.
std::map<std::string, EquipPrototype> SwordCatalog() {
  EquipPrototype sword;
  sword.set_name("Sword");
  sword.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  EquipPrototype long_sword;
  long_sword.set_name("Long Sword");
  long_sword.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  return {{"sword", sword}, {"long_sword", long_sword}};
}

GameState MakeTestModeState() {
  return GameState(SwordCatalog(), {}, {}, {}, {}, {}, GameMode::kTest);
}

GameState MakePlayModeState() {
  return GameState(SwordCatalog(), {}, {}, {}, {}, {}, GameMode::kPlay);
}

// One skill of each advancement the workbench's character passes through, so
// the seeding has a book to spend its SP on. The levels are the real ones, so
// what a stage's pool buys is the real question too.
std::map<std::string, Skill> TwoStageBook() {
  Skill first;
  first.set_name("Slash Blast");
  first.set_kind(SKILL_KIND_ATTACK);
  first.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  first.set_max_level(60);
  Skill second;
  second.set_name("Spear Sweep");
  second.set_kind(SKILL_KIND_ATTACK);
  second.set_job_advancement(JOB_ADVANCEMENT_SPEARMAN);
  second.set_max_level(90);
  return {{"slash_blast", first}, {"spear_sweep", second}};
}

GameState MakeTestModeStateWithSkills() {
  return GameState(SwordCatalog(), {}, {}, {}, {}, TwoStageBook(),
                   GameMode::kTest);
}

// The item catalog under the key test mode's seeding asks for.
std::map<std::string, ItemPrototype> LevelUpCatalog() {
  ItemPrototype item;
  item.set_name("Level-Up");
  item.set_category(ITEM_CATEGORY_USE);
  item.set_effect(ITEM_EFFECT_LEVEL_UP);
  return {{"level_up", item}};
}

GameState MakeTestModeStateWithItems() {
  return GameState(SwordCatalog(), {}, LevelUpCatalog(), {}, {}, {},
                   GameMode::kTest);
}

GameState MakePlayModeStateWithItems() {
  return GameState(SwordCatalog(), {}, LevelUpCatalog(), {}, {}, {},
                   GameMode::kPlay);
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

// The workbench opens on a finished character, because everything past level
// thirty is otherwise thirty hours away and there is no other way to look at
// it. Play mode is where the climb is worth watching a level at a time.
TEST(GameStateTest, TestModeStartsAtTheEndOfSecondJob) {
  GameState test = MakeTestModeState();
  EXPECT_EQ(test.character.proto().level(), kTrialLevelCap);
  EXPECT_EQ(test.character.proto().job(), JOB_SPEARMAN);
  EXPECT_EQ(test.character.proto().job_stage(), 2);
  // Nothing left standing between the tester and the screens: no advancement
  // waiting to be taken, and no pool waiting to be spent.
  EXPECT_FALSE(test.character.CanAdvanceJob());
  EXPECT_EQ(test.character.proto().ap(), 0);
}

TEST(GameStateTest, TestModeStartsWithBothBooksBought) {
  GameState state = MakeTestModeStateWithSkills();
  EXPECT_EQ(state.character.sp(1), 0);
  EXPECT_EQ(state.character.sp(2), 0);
  for (const std::pair<const std::string, Skill>& entry : state.skills) {
    EXPECT_EQ(state.character.skill_level(entry.second),
              entry.second.max_level())
        << entry.first << " is left part-bought";
  }
}

// Levels past the workbench's starting sixty, spent on demand. LevelUp is not
// bounded by the trial cap, so this is also where more AP and SP come from
// once the seeding has spent what the climb to sixty earned.
TEST(GameStateTest, TestModeStartsWithLevelUpItems) {
  GameState state = MakeTestModeStateWithItems();
  const std::vector<StackableItem>& use =
      state.character.stackables(ITEM_CATEGORY_USE);
  ASSERT_EQ(use.size(), 1u);
  EXPECT_EQ(use[0].name(), "Level-Up");
  // Enough of them to climb past every gate in the unlock table -- including
  // the ones above kTrialLevelCap, which combat can no longer reach.
  EXPECT_GT(use[0].count(), UnlockLevel(Feature::kRecovery));
  EXPECT_EQ(use[0].prototype().effect(), ITEM_EFFECT_LEVEL_UP);
}

TEST(GameStateTest, PlayModeGetsNoLevelUpItems) {
  GameState state = MakePlayModeStateWithItems();
  EXPECT_TRUE(state.character.stackables(ITEM_CATEGORY_USE).empty());
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

// Both modes start wearing a weapon. A level 1 character has no equipped
// panel and no bag, so one that only carried a weapon could never swing it,
// and without a swing there is no EXP and no way off level 1.
TEST(GameStateTest, BothModesStartWearingAWeapon) {
  EXPECT_FALSE(MakeTestModeState().character.equipped().empty());
  EXPECT_FALSE(MakePlayModeState().character.equipped().empty());
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
