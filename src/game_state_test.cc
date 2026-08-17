#include "src/game_state.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "src/character/character.h"
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

// Which branch the workbench's character takes -- a knob in game_state.cc,
// meant to be flipped to look at another job's screens. Read rather than
// named, so flipping it does not fail the tests below.
Job WorkbenchJob() {
  return MakeTestModeState().character.proto().job();
}

// One skill of each advancement the workbench's character passes through, so
// the seeding has a book to spend its SP on. The levels are the real ones, so
// what a stage's pool buys is the real question too.
std::map<std::string, Skill> EveryStageBook() {
  const int kSpByStage[] = {0, 60, 90, 120, 200};
  std::map<std::string, Skill> book;
  for (int stage = 1; stage <= 4; ++stage) {
    JobAdvancement advancement = AdvancementForJobStage(WorkbenchJob(), stage);
    if (advancement == JOB_ADVANCEMENT_UNSPECIFIED) {
      continue;
    }
    Skill skill;
    skill.set_name("Stage " + std::to_string(stage) + " Swing");
    skill.set_kind(SKILL_KIND_ATTACK);
    skill.set_job_advancement(advancement);
    skill.set_max_level(kSpByStage[stage]);
    book.insert({"stage_" + std::to_string(stage), skill});
  }
  return book;
}

GameState MakeTestModeStateWithSkills() {
  return GameState(SwordCatalog(), {}, {}, {}, {}, EveryStageBook(),
                   GameMode::kTest);
}

// The item catalog under the key test mode's seeding asks for, plus a currency
// and an ordinary Etc item to tell it from.
std::map<std::string, ItemPrototype> LevelUpCatalog() {
  ItemPrototype item;
  item.set_name("Level-Up");
  item.set_category(ITEM_CATEGORY_USE);
  item.set_effect(ITEM_EFFECT_LEVEL_UP);
  ItemPrototype token;
  token.set_name("Weapon Token");
  token.set_category(ITEM_CATEGORY_ETC);
  token.set_currency_mark("●");
  ItemPrototype horn;
  horn.set_name("Beetle's Horn");
  horn.set_category(ITEM_CATEGORY_ETC);
  horn.set_sell_price(230);
  return {{"level_up", item}, {"weapon_token", token}, {"horn", horn}};
}

GameState MakeTestModeStateWithItems() {
  return GameState(SwordCatalog(), {}, LevelUpCatalog(), {}, {}, {},
                   GameMode::kTest);
}

GameState MakePlayModeStateWithItems() {
  return GameState(SwordCatalog(), {}, LevelUpCatalog(), {}, {}, {},
                   GameMode::kPlay);
}

// One claim, five catalogs: the constructor hands each one through to the
// field named after it.
TEST(GameStateTest, ConstructorStoresEveryCatalog) {
  EquipPrototype equip;
  equip.set_name("Sword");
  Scroll scroll;
  scroll.set_name("60% ATT");
  ItemPrototype item;
  item.set_name("Green Snail Shell");
  Mob mob;
  mob.set_name("Snail");
  MapData map;
  map.set_name("Right Around Lith Harbor");

  GameState state({{"sword", equip}}, {{"att_60", scroll}},
                  {{"green_snail_shell", item}}, {{"snail", mob}},
                  {{"lith", map}});

  ASSERT_TRUE(state.equips.count("sword"));
  EXPECT_EQ(state.equips.at("sword").name(), "Sword");
  ASSERT_TRUE(state.scrolls.count("att_60"));
  EXPECT_EQ(state.scrolls.at("att_60").name(), "60% ATT");
  ASSERT_TRUE(state.items.count("green_snail_shell"));
  EXPECT_EQ(state.items.at("green_snail_shell").name(), "Green Snail Shell");
  ASSERT_TRUE(state.mobs.count("snail"));
  EXPECT_EQ(state.mobs.at("snail").name(), "Snail");
  ASSERT_TRUE(state.maps.count("lith"));
  EXPECT_EQ(state.maps.at("lith").name(), "Right Around Lith Harbor");
}

// The workbench opens on a finished character, because everything past level
// thirty is otherwise thirty hours away and there is no other way to look at
// it. Play mode is where the climb is worth watching a level at a time.
TEST(GameStateTest, TestModeStartsAtTheTopOfTheWrittenLine) {
  GameState test = MakeTestModeState();
  // The top of 4th job, held to the level cap: the 5th advancement's level is
  // above the cap, so the workbench stops where the EXP table does.
  int stage = test.character.proto().job_stage();
  EXPECT_EQ(stage, 4);
  EXPECT_EQ(test.character.proto().level(),
            std::min(NextAdvancementLevel(stage), kTrialLevelCap));
  // Some warrior branch, not a particular one -- see WorkbenchJob. Walked down
  // the tree from the 1st job, so a workbench standing somewhere the choices
  // cannot reach fails here.
  std::vector<Job> reached = {JOB_SWORDMAN};
  for (int i = 2; i <= stage; ++i) {
    std::vector<Job> next;
    for (Job job : reached) {
      for (Job choice : JobChoicesForStage(job, i)) {
        next.push_back(choice);
      }
    }
    reached = next;
  }
  EXPECT_NE(
      std::find(reached.begin(), reached.end(), test.character.proto().job()),
      reached.end());
  // Nothing left standing between the tester and the screens: no advancement
  // waiting to be taken, and no pool waiting to be spent.
  EXPECT_FALSE(test.character.CanAdvanceJob());
  EXPECT_EQ(test.character.proto().ap(), 0);
}

TEST(GameStateTest, TestModeStartsWithEveryBookBought) {
  GameState state = MakeTestModeStateWithSkills();
  EXPECT_EQ(state.character.sp(1), 0);
  EXPECT_EQ(state.character.sp(2), 0);
  EXPECT_EQ(state.character.sp(3), 0);
  EXPECT_EQ(state.character.sp(4), 0);
  for (const std::pair<const std::string, Skill>& entry : state.skills) {
    EXPECT_EQ(state.character.skill_level(entry.second),
              entry.second.max_level())
        << entry.first << " is left part-bought";
  }
}

// --- the --job workbench ---

// A bow, so a chosen bowman job has its own weapon to be handed. Named the way
// StarterEquipsFor and the workbench's own table name it.
std::map<std::string, EquipPrototype> BowCatalog() {
  std::map<std::string, EquipPrototype> equips = SwordCatalog();
  EquipPrototype war_bow;
  war_bow.set_name("War Bow");
  war_bow.set_equip_type(EQUIP_TYPE_BOW);
  war_bow.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  EquipPrototype ryden;
  ryden.set_name("Ryden");
  ryden.set_equip_type(EQUIP_TYPE_BOW);
  ryden.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  EquipPrototype asianic;
  asianic.set_name("Asianic Bow");
  asianic.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  equips["war_bow"] = war_bow;
  equips["ryden"] = ryden;
  equips["asianic_bow"] = asianic;
  return equips;
}

// A book the chosen job can actually buy from, so "the SP is unspent" means
// unspent rather than unspendable. One skill per advancement that job reaches.
std::map<std::string, Skill> BookFor(Job job) {
  std::map<std::string, Skill> book;
  const char* kNames[] = {"", "First Swing", "Second Swing"};
  const int kMaxLevels[] = {0, 60, 90};
  for (int stage = 1; stage <= 2; ++stage) {
    JobAdvancement advancement = AdvancementForJobStage(job, stage);
    if (advancement == JOB_ADVANCEMENT_UNSPECIFIED) {
      continue;
    }
    Skill skill;
    skill.set_name(kNames[stage]);
    skill.set_kind(SKILL_KIND_ATTACK);
    skill.set_job_advancement(advancement);
    skill.set_max_level(kMaxLevels[stage]);
    book[kNames[stage]] = skill;
  }
  return book;
}

GameState MakeChosenJobState(JobAdvancement advancement) {
  return GameState(BowCatalog(), {}, {}, {}, {},
                   BookFor(JobForAdvancement(advancement)), GameMode::kTest,
                   advancement);
}

// --job stops at the top of the advancement it names, not at the workbench's
// own: an archer is the last level before the 2nd job, a hunter the last
// before the 3rd.
TEST(GameStateTest, ChosenJobStartsAtTheTopOfThatAdvancement) {
  GameState archer = MakeChosenJobState(JOB_ADVANCEMENT_ARCHER);
  EXPECT_EQ(archer.character.proto().job(), JOB_ARCHER);
  EXPECT_EQ(archer.character.proto().level(), 30);

  GameState hunter = MakeChosenJobState(JOB_ADVANCEMENT_HUNTER);
  EXPECT_EQ(hunter.character.proto().job(), JOB_HUNTER);
  EXPECT_EQ(hunter.character.proto().level(), NextAdvancementLevel(2));
  EXPECT_EQ(hunter.character.proto().job_stage(), 2);
}

// The AP is spent: which stats to raise is never the question a tester is
// asking. Only the chosen job's own book is left, since the books behind it
// are not what was asked for either.
TEST(GameStateTest, ChosenJobSpendsTheApAndEveryBookBelowItsOwn) {
  GameState state = MakeChosenJobState(JOB_ADVANCEMENT_HUNTER);
  EXPECT_EQ(state.character.proto().ap(), 0);
  EXPECT_GT(state.character.proto().allocated_stats().dex(), 200);
  EXPECT_EQ(state.character.sp(1), 0);
  EXPECT_GT(state.character.sp(2), 0);
  for (const std::pair<const std::string, Skill>& entry : state.skills) {
    int stage = StageForAdvancement(entry.second.job_advancement());
    int expected = stage < 2 ? entry.second.max_level() : 0;
    EXPECT_EQ(state.character.skill_level(entry.second), expected)
        << entry.first << " at stage " << stage;
  }
}

// A 1st job is the highest book its character has, so nothing is bought.
TEST(GameStateTest, AChosenFirstJobKeepsItsWholeBook) {
  GameState state = MakeChosenJobState(JOB_ADVANCEMENT_ARCHER);
  EXPECT_GT(state.character.sp(1), 0);
  for (const std::pair<const std::string, Skill>& entry : state.skills) {
    EXPECT_EQ(state.character.skill_level(entry.second), 0) << entry.first;
  }
}

// Worn, not carried. A 2nd job advances empty-handed now, so without this the
// chosen job would arrive with nothing in hand and half its book asleep.
// An Archer is left at 30 holding the Ryden that a level 30 can wear; a Hunter
// is left at 60 holding the Asianic Bow. Both are the top of the bow ladder
// their level reaches -- see WorkbenchWeaponsFor.
TEST(GameStateTest, ChosenJobWearsTheWeaponItsLevelTopsOutAt) {
  GameState archer = MakeChosenJobState(JOB_ADVANCEMENT_ARCHER);
  ASSERT_TRUE(archer.character.equipped().count(EQUIP_SLOT_PRIMARY_WEAPON));
  EXPECT_EQ(archer.character.equipped().at(EQUIP_SLOT_PRIMARY_WEAPON).name(),
            "Ryden");

  GameState hunter = MakeChosenJobState(JOB_ADVANCEMENT_HUNTER);
  ASSERT_TRUE(hunter.character.equipped().count(EQUIP_SLOT_PRIMARY_WEAPON));
  EXPECT_EQ(hunter.character.equipped().at(EQUIP_SLOT_PRIMARY_WEAPON).name(),
            "Asianic Bow");
}

// The warrior ladder the default workbench carries is the default job's, so a
// chosen job is spared it.
TEST(GameStateTest, ChosenJobIsNotHandedTheDefaultJobsLadder) {
  GameState state = MakeChosenJobState(JOB_ADVANCEMENT_HUNTER);
  for (int i = 0; i < static_cast<int>(state.character.inventory().size());
       ++i) {
    EXPECT_NE(state.character.inventory().equip_instance(i)->prototype().name(),
              "Long Sword");
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
  // Enough of them to climb past every gate in the unlock table without
  // farming a level of it.
  EXPECT_GT(use[0].count(), UnlockLevel(Feature::kStarForce));
  EXPECT_EQ(use[0].prototype().effect(), ITEM_EFFECT_LEVEL_UP);
}

// The token shelves are unbuyable without one, and farming for one is exactly
// what a workbench is for skipping. Only a currency: an ordinary Etc drop is
// not something the shop asks a price in.
TEST(GameStateTest, TestModeStartsWithEveryToken) {
  GameState state = MakeTestModeStateWithItems();
  const std::vector<StackableItem>& etc =
      state.character.stackables(ITEM_CATEGORY_ETC);
  ASSERT_EQ(etc.size(), 1u);
  EXPECT_EQ(etc[0].name(), "Weapon Token");
  EXPECT_GT(etc[0].count(), 1);
}

TEST(GameStateTest, PlayModeGetsNoTokens) {
  GameState state = MakePlayModeStateWithItems();
  EXPECT_TRUE(state.character.stackables(ITEM_CATEGORY_ETC).empty());
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
TEST(GameStateTest, PlayModeStartsWearingASword) {
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
  EXPECT_EQ(state.character.meso(), 1000000000);
  EXPECT_FALSE(state.character.inventory().empty());
}

// Both modes start wearing a weapon. A level 1 character has no equipped
// panel and no bag, so one that only carried a weapon could never swing it,
// and without a swing there is no EXP and no way off level 1.
TEST(GameStateTest, BothModesStartWearingAWeapon) {
  EXPECT_FALSE(MakeTestModeState().character.equipped().empty());
  EXPECT_FALSE(MakePlayModeState().character.equipped().empty());
}

// The set effect cannot be read off the stats page without the whole set, and
// farming one out takes a climb to 100. The workbench wears it instead --
// worn and not carried, so nothing has to be put on to read the bonus.
TEST(GameStateTest, TestModeWearsTheWholeFrozenSet) {
  struct Piece {
    const char* key;
    const char* name;
    EquipSlot slot;
  };
  const Piece kPieces[] = {
      {"frozen_hat", "Frozen Hat", EQUIP_SLOT_HAT},
      {"frozen_top", "Frozen Top", EQUIP_SLOT_TOP},
      {"frozen_bottom", "Frozen Bottom", EQUIP_SLOT_BOTTOM},
      {"frozen_cape", "Frozen Cape", EQUIP_SLOT_CAPE}};
  std::map<std::string, EquipPrototype> catalog = SwordCatalog();
  for (const Piece& piece : kPieces) {
    EquipPrototype proto;
    proto.set_name(piece.name);
    proto.set_equip_slot(piece.slot);
    catalog[piece.key] = proto;
  }

  GameState state(catalog, {}, {}, {}, {}, {}, GameMode::kTest);
  const std::map<EquipSlot, EquipInstance>& worn = state.character.equipped();
  for (const Piece& piece : kPieces) {
    std::map<EquipSlot, EquipInstance>::const_iterator it =
        worn.find(piece.slot);
    ASSERT_NE(it, worn.end()) << "the workbench has no " << piece.name;
    EXPECT_EQ(it->second.prototype().name(), piece.name);
  }
  // And no second copy in the bag. Four pieces nobody can wear twice were
  // four rows of clutter in front of everything the workbench is for.
  const InventoryInstance& bag = state.character.inventory();
  for (int i = 0; i < bag.size(); ++i) {
    EXPECT_EQ(bag[i].prototype().name().find("Frozen"), std::string::npos)
        << bag[i].prototype().name() << " is carried as well as worn";
  }
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
  EXPECT_EQ(test.character.meso(), 1000000000);
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
