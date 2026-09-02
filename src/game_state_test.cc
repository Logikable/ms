#include "src/game_state.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "src/character/arcane_force.h"
#include "src/character/character.h"
#include "src/character/consumables.h"
#include "src/character/exp_table.h"
#include "src/character/hyper_stats.h"
#include "src/character/max_character.h"
#include "src/character/progression.h"
#include "src/character/stat_preset.h"
#include "src/item/equip_instance.h"
#include "src/item/item.h"
#include "src/item/potential.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/map.pb.h"
#include "src/protos/mob.pb.h"
#include "src/protos/scroll.pb.h"

namespace ms {
namespace {

// A catalog holding the equip both modes hand out, so the seeding has
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

GameState MakeTestModeStateWithSkills(TestSkills skills = TestSkills::kZero) {
  TestOptions test;
  test.skills = skills;
  return GameState(SwordCatalog(), {}, {}, {}, {}, EveryStageBook(),
                   GameMode::kTest, test);
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
  // The real one's max_stack, because the count the workbench is handed is
  // that number: a smaller cap here would spill it across a hundred rows.
  ItemPrototype trace;
  trace.set_name("Spell Trace");
  trace.set_category(ITEM_CATEGORY_ETC);
  trace.set_max_stack(30000);
  return {{"level_up", item},
          {"weapon_token", token},
          {"horn", horn},
          {"spell_trace", trace}};
}

// The stack of `name` on `category`'s tab, or nullptr when there is none.
const StackableItem* FindStack(const GameState& state, ItemCategory category,
                               const std::string& name) {
  for (const StackableItem& stack : state.character.stackables(category)) {
    if (stack.name() == name) {
      return &stack;
    }
  }
  return nullptr;
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

// --skills decides what becomes of the book the character is standing in. The
// books behind it are bought either way: they are not what the tester picked
// the job for.
TEST(GameStateTest, SkillsZeroLeavesTheJobsOwnBookUnbought) {
  GameState state = MakeTestModeStateWithSkills();
  int top = state.character.proto().job_stage();
  for (int stage = 1; stage < top; ++stage) {
    EXPECT_EQ(state.character.sp(stage), 0) << "stage " << stage;
  }
  EXPECT_GT(state.character.sp(top), 0);
  for (const std::pair<const std::string, Skill>& entry : state.skills) {
    int stage = StageForAdvancement(entry.second.job_advancement());
    int expected = stage < top ? entry.second.max_level() : 0;
    EXPECT_EQ(state.character.skill_level(entry.second), expected)
        << entry.first << " at stage " << stage;
  }
}

TEST(GameStateTest, SkillsMaxBuysEveryBookOutright) {
  GameState state = MakeTestModeStateWithSkills(TestSkills::kMax);
  for (int stage = 1; stage <= 4; ++stage) {
    EXPECT_EQ(state.character.sp(stage), 0) << "stage " << stage;
  }
  for (const std::pair<const std::string, Skill>& entry : state.skills) {
    EXPECT_EQ(state.character.skill_level(entry.second),
              entry.second.max_level())
        << entry.first << " is left part-bought";
  }
}

// --- --hammered, --scrolled and --sf ---

// The workbench's own level 30 warrior weapon, with slots to scroll and stars
// to add. Keyed the way WorkbenchGearFor names it, or nothing is worn at all.
std::map<std::string, EquipPrototype> GladiusCatalog() {
  EquipPrototype gladius;
  gladius.set_name("Gladius");
  gladius.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  gladius.set_equip_type(EQUIP_TYPE_ONE_HANDED_SWORD);
  gladius.set_required_level(30);
  gladius.add_equip_job_categories(EQUIP_JOB_CATEGORY_WARRIOR);
  gladius.set_upgrade_slots(7);
  return {{"gladius", gladius}};
}

// Two traces a warrior's weapon takes, the long odds paying more. The
// workbench passes every slot, so it should take the one that pays.
std::map<std::string, Scroll> WarriorWeaponTraces() {
  Scroll sure;
  sure.set_name("100% STR");
  sure.set_scroll_type(SCROLL_TYPE_STR);
  sure.set_target(SCROLL_TARGET_WEAPON);
  sure.set_tier(SCROLL_TIER_1);
  sure.set_success_rate(100);
  sure.mutable_stats()->set_attack(1);
  sure.add_applicable_job_categories(EQUIP_JOB_CATEGORY_WARRIOR);
  Scroll risky = sure;
  risky.set_name("30% STR");
  risky.set_success_rate(30);
  risky.mutable_stats()->set_attack(5);
  risky.mutable_stats()->set_str(3);
  return {{"str_100", sure}, {"str_30", risky}};
}

GameState MakeEquipsState(GearSetup equips) {
  TestOptions test;
  test.job = JOB_ADVANCEMENT_SWORDMAN;
  test.equips = equips;
  return GameState(GladiusCatalog(), WarriorWeaponTraces(), {}, {}, {}, {},
                   GameMode::kTest, test);
}

const Equip& WornWeapon(const GameState& state) {
  return state.character.equipped().at(EQUIP_SLOT_PRIMARY_WEAPON).equip_state();
}

// No flag at all: gear arrives as it drops, slots to spend and no stars.
TEST(GameStateTest, NoUpgradeFlagLeavesTheGearAsItDrops) {
  GameState state = MakeEquipsState(GearSetup());
  EXPECT_EQ(WornWeapon(state).remaining_upgrade_slots(), 7);
  EXPECT_EQ(WornWeapon(state).hammers(), 0);
  EXPECT_EQ(WornWeapon(state).scroll_successes(), 0);
  EXPECT_EQ(WornWeapon(state).stars(), 0);
}

// Each flag does its own job and nothing else: the hammers widen the shelf,
// leaving every slot on it unspent.
TEST(GameStateTest, HammeredWidensTheShelfWithoutFillingIt) {
  GearSetup equips;
  equips.hammered = true;
  const Equip& worn = WornWeapon(MakeEquipsState(equips));
  EXPECT_EQ(worn.hammers(), kMaxHammers);
  EXPECT_EQ(worn.remaining_upgrade_slots(), 9);
  EXPECT_EQ(worn.scroll_successes(), 0);
}

// And scrolling alone passes the shelf the item shipped with, with the trace
// that pays the most.
TEST(GameStateTest, ScrolledPassesTheSlotsTheItemHas) {
  GearSetup equips;
  equips.scrolled = true;
  const Equip& worn = WornWeapon(MakeEquipsState(equips));
  EXPECT_EQ(worn.hammers(), 0);
  EXPECT_EQ(worn.remaining_upgrade_slots(), 0);
  EXPECT_EQ(worn.scroll_successes(), 7);
  EXPECT_EQ(worn.scroll_stats().attack(), 35);
  EXPECT_EQ(worn.scroll_stats().str(), 21);
  EXPECT_EQ(worn.stars(), 0);
}

// Together the wider shelf is the one that gets filled.
TEST(GameStateTest, HammeredAndScrolledFillTheWiderShelf) {
  GearSetup equips;
  equips.hammered = true;
  equips.scrolled = true;
  const Equip& worn = WornWeapon(MakeEquipsState(equips));
  EXPECT_EQ(worn.hammers(), kMaxHammers);
  EXPECT_EQ(worn.remaining_upgrade_slots(), 0);
  EXPECT_EQ(worn.scroll_successes(), 9);
  EXPECT_EQ(worn.scroll_stats().attack(), 45);
  EXPECT_EQ(worn.scroll_stats().str(), 27);
}

// --sf sets exactly the stars it names, and the item's own cap is the ceiling
// -- a level 30 weapon takes five of them however many are asked for.
TEST(GameStateTest, SfSetsTheStarsItNamesUpToTheItemsCap) {
  GearSetup equips;
  equips.scrolled = true;
  equips.stars = 3;
  EXPECT_EQ(WornWeapon(MakeEquipsState(equips)).stars(), 3);

  equips.stars = 22;
  EXPECT_EQ(WornWeapon(MakeEquipsState(equips)).stars(),
            EquipTabItem::MaxStarsForLevel(30));
}

// Stars need nothing left to scroll, which is the upgrade screen's own rule --
// so --sf on an item with slots unspent leaves it unstarred.
TEST(GameStateTest, SfWaitsForAShelfWithNothingLeftOnIt) {
  GearSetup equips;
  equips.stars = 3;
  EXPECT_EQ(WornWeapon(MakeEquipsState(equips)).stars(), 0);

  equips.hammered = true;
  EXPECT_EQ(WornWeapon(MakeEquipsState(equips)).stars(), 0);
}

// An item that refuses an upgrade path is left alone on it, however the flags
// are set: the workbench does not get to overrule the data.
TEST(GameStateTest, TheFlagsLeaveAnItemThatRefusesThePathAlone) {
  std::map<std::string, EquipPrototype> catalog = GladiusCatalog();
  catalog["gladius"].add_unsupported_upgrades(UPGRADE_STAR_FORCE);
  TestOptions test;
  test.job = JOB_ADVANCEMENT_SWORDMAN;
  test.equips = {/*hammered=*/true, /*scrolled=*/true, /*stars=*/30};
  GameState state(catalog, WarriorWeaponTraces(), {}, {}, {}, {},
                  GameMode::kTest, test);
  EXPECT_EQ(WornWeapon(state).scroll_successes(), 9);
  EXPECT_EQ(WornWeapon(state).stars(), 0);
}

// Gloves take no stat trace at all, so the workbench falls back to the attack
// one -- and a scrolled shelf is what lets the stars go on.
TEST(GameStateTest, TheFlagsScrollGlovesWithTheAttackTrace) {
  // Hung on the key WorkbenchGearFor names, which is what decides what a
  // swordman is handed -- the prototype behind it is the catalog's to choose.
  EquipPrototype gloves;
  gloves.set_name("Gauntlets");
  gloves.set_equip_slot(EQUIP_SLOT_GLOVES);
  gloves.set_required_level(30);
  gloves.add_equip_job_categories(EQUIP_JOB_CATEGORY_WARRIOR);
  gloves.set_upgrade_slots(5);
  std::map<std::string, EquipPrototype> catalog{{"gladius", gloves}};
  std::map<std::string, Scroll> scrolls = WarriorWeaponTraces();
  Scroll att;
  att.set_name("30% ATT");
  att.set_scroll_type(SCROLL_TYPE_ATT);
  att.set_target(SCROLL_TARGET_GLOVES);
  att.set_tier(SCROLL_TIER_1);
  att.set_success_rate(30);
  att.mutable_stats()->set_attack(3);
  att.add_applicable_job_categories(EQUIP_JOB_CATEGORY_WARRIOR);
  scrolls["gloves_att_30"] = att;
  TestOptions test;
  test.job = JOB_ADVANCEMENT_SWORDMAN;
  test.equips = {/*hammered=*/true, /*scrolled=*/true, /*stars=*/30};
  GameState state(catalog, scrolls, {}, {}, {}, {}, GameMode::kTest, test);
  const Equip& worn =
      state.character.equipped().at(EQUIP_SLOT_GLOVES).equip_state();
  EXPECT_EQ(worn.hammers(), kMaxHammers);
  EXPECT_EQ(worn.scroll_successes(), 7);
  EXPECT_EQ(worn.scroll_stats().attack(), 21);
  EXPECT_EQ(worn.stars(), EquipTabItem::MaxStarsForLevel(30));
}

// The stat trace wins where both are written: a weapon takes STR, not the ATT
// the fallback would reach for.
TEST(GameStateTest, TheFlagsPreferTheStatTraceOverTheAttackOne) {
  std::map<std::string, Scroll> scrolls = WarriorWeaponTraces();
  Scroll att;
  att.set_name("30% ATT");
  att.set_scroll_type(SCROLL_TYPE_ATT);
  att.set_target(SCROLL_TARGET_WEAPON);
  att.set_tier(SCROLL_TIER_1);
  att.set_success_rate(30);
  att.mutable_stats()->set_attack(9);
  att.add_applicable_job_categories(EQUIP_JOB_CATEGORY_WARRIOR);
  scrolls["weapon_att_30"] = att;
  TestOptions test;
  test.job = JOB_ADVANCEMENT_SWORDMAN;
  test.equips = {/*hammered=*/true, /*scrolled=*/true, /*stars=*/0};
  GameState state(GladiusCatalog(), scrolls, {}, {}, {}, {}, GameMode::kTest,
                  test);
  EXPECT_EQ(WornWeapon(state).scroll_stats().str(), 27);
}

// A piece with no scroll shelf takes no hammer either: the hammer widens a
// shelf, and there is none to widen.
TEST(GameStateTest, TheFlagsLeaveAPieceWithNoShelfUnhammered) {
  std::map<std::string, EquipPrototype> catalog = GladiusCatalog();
  catalog["gladius"].set_upgrade_slots(0);
  TestOptions test;
  test.job = JOB_ADVANCEMENT_SWORDMAN;
  test.equips = {/*hammered=*/true, /*scrolled=*/true, /*stars=*/30};
  GameState state(catalog, WarriorWeaponTraces(), {}, {}, {}, {},
                  GameMode::kTest, test);
  EXPECT_EQ(WornWeapon(state).hammers(), 0);
  EXPECT_EQ(WornWeapon(state).scroll_successes(), 0);
  EXPECT_EQ(WornWeapon(state).stars(), EquipTabItem::MaxStarsForLevel(30));
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
                   TestOptions{advancement});
}

// A workbench character is named after its job, so several of them in a party
// are told apart. The slash of "I/L Arch Mage" is not a name character.
TEST(GameStateTest, TheWorkbenchNamesACharacterAfterItsJob) {
  EXPECT_EQ(MakeChosenJobState(JOB_ADVANCEMENT_HUNTER).character.username(),
            "Hunter");
  GameState mage = GameState(
      BowCatalog(), {}, {}, {}, {}, BookFor(JOB_ICE_LIGHTNING_ARCH_MAGE),
      GameMode::kTest, TestOptions{JOB_ADVANCEMENT_ICE_LIGHTNING_ARCH_MAGE});
  EXPECT_EQ(mage.character.username(), "IL Arch Mage");
  EXPECT_LE(static_cast<int>(mage.character.username().size()),
            kMaxUsernameLength);
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

// --level stops the climb where the tester asked rather than at the top of the
// band, which is how a workbench is put in front of a screen that only opens
// part-way up an advancement.
TEST(GameStateTest, ChosenLevelStopsTheClimbWhereItWasAsked) {
  TestOptions test;
  test.job = JOB_ADVANCEMENT_HUNTER;
  test.level = 45;
  GameState state(BowCatalog(), {}, {}, {}, {}, BookFor(JOB_HUNTER),
                  GameMode::kTest, test);
  EXPECT_EQ(state.character.proto().level(), 45);
  EXPECT_EQ(state.character.proto().job(), JOB_HUNTER)
      << "the advancements below it are still taken on the way";
  EXPECT_EQ(state.character.proto().ap(), 0) << "and the AP is still spent";
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
  const StackableItem* token =
      FindStack(state, ITEM_CATEGORY_ETC, "Weapon Token");
  ASSERT_NE(token, nullptr);
  EXPECT_GT(token->count(), 1);
  // The ordinary Etc drop beside it in the catalog stays where it was: the
  // workbench is handed currencies, not somebody else's loot.
  EXPECT_EQ(FindStack(state, ITEM_CATEGORY_ETC, "Beetle's Horn"), nullptr);
}

// Scrolling is priced in traces and the shop counts them out 5,000 meso at a
// time, which is a long walk to reach a screen a tester wants to be on. A full
// stack is handed over instead -- 30,000 is the item's own max_stack, so it
// arrives as ONE row rather than a hundred and fifty.
TEST(GameStateTest, TestModeCarriesAFullStackOfSpellTraces) {
  GameState state = MakeTestModeStateWithItems();
  const StackableItem* traces =
      FindStack(state, ITEM_CATEGORY_ETC, "Spell Trace");
  ASSERT_NE(traces, nullptr);
  EXPECT_EQ(traces->count(), traces->max_stack());
}

// Test mode's stocked bag is test mode's alone: play mode is handed neither
// the tokens nor the level-up items.
TEST(GameStateTest, PlayModeGetsNoTokensOrLevelUpItems) {
  GameState state = MakePlayModeStateWithItems();
  EXPECT_TRUE(state.character.stackables(ITEM_CATEGORY_ETC).empty());
  EXPECT_TRUE(state.character.stackables(ITEM_CATEGORY_USE).empty());
}

// --- play mode ---

// The whole of what a new character is handed: a level-1 Beginner on Maple
// Island with the shipped stat spread, no meso, and armed but carrying nothing
// -- the Sword is worn, so the bag really is empty.
TEST(GameStateTest, PlayModeStartsANewCharacter) {
  GameState state = MakePlayModeState();
  EXPECT_EQ(state.character.proto().level(), 1);
  EXPECT_EQ(state.character.proto().job(), JOB_BEGINNER);
  EXPECT_FALSE(state.character.CanAdvanceJob());
  EXPECT_EQ(state.current_map, "maple_island");
  EXPECT_EQ(state.character.meso(), 0);

  const AllocatedStats& s = state.character.proto().allocated_stats();
  EXPECT_EQ(s.str(), 13);
  EXPECT_EQ(s.dex(), 4);
  EXPECT_EQ(s.int_(), 4);
  EXPECT_EQ(s.luk(), 4);
  EXPECT_EQ(state.character.proto().ap(), 0);

  EXPECT_TRUE(state.character.inventory().empty());
  ASSERT_TRUE(state.character.equipped().count(EQUIP_SLOT_PRIMARY_WEAPON));
  EXPECT_EQ(state.character.equipped().at(EQUIP_SLOT_PRIMARY_WEAPON).name(),
            "Sword");
}

// --- test mode ---

// Meso to shop with, and an empty equip tab: the workbench wears what it is
// given, so a tester opens the bag on what they put there rather than on the
// seeding's leftovers. The spare symbols are the one thing carried, and this
// catalog has none.
TEST(GameStateTest, TestModeStartsWithMesoAndAnEmptyBag) {
  GameState state = MakeTestModeState();
  EXPECT_EQ(state.character.meso(), 100000000000);
  EXPECT_TRUE(state.character.inventory().empty());
}

// The sweep is what makes that true of a job whose gear does not all fit: a
// Rogue is handed a dagger and a claw for one slot, and the one the other
// displaces would otherwise sit in the bag.
TEST(GameStateTest, TheWorkbenchCarriesNothingItCouldNotWear) {
  std::map<std::string, EquipPrototype> equips;
  for (const char* key : {"reef_claw", "steel_guards"}) {
    EquipPrototype weapon;
    weapon.set_name(key);
    weapon.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
    equips[key] = weapon;
  }
  TestOptions test;
  test.job = JOB_ADVANCEMENT_ROGUE;
  GameState state(equips, {}, {}, {}, {}, {}, GameMode::kTest, test);

  EXPECT_EQ(state.character.equipped().count(EQUIP_SLOT_PRIMARY_WEAPON), 1u);
  EXPECT_TRUE(state.character.inventory().empty());
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

// Potential is worth looking at at every rank, so the workbench deals the
// four over the gear it wears rather than letting each piece roll its own: a
// run where nothing came out Legendary would leave the display half-tested.
TEST(GameStateTest, TestModeCubesEveryPieceItCanAndSpreadsTheRanks) {
  // The armour the workbench wears, which with the sword is five slots the
  // potential reaches -- enough for every rank to be dealt.
  struct Piece {
    const char* key;
    EquipSlot slot;
  };
  const Piece kPieces[] = {{"frozen_hat", EQUIP_SLOT_HAT},
                           {"frozen_top", EQUIP_SLOT_TOP},
                           {"frozen_bottom", EQUIP_SLOT_BOTTOM},
                           {"frozen_cape", EQUIP_SLOT_CAPE}};
  std::map<std::string, EquipPrototype> catalog = SwordCatalog();
  for (const Piece& piece : kPieces) {
    EquipPrototype proto;
    proto.set_name(piece.key);
    proto.set_equip_slot(piece.slot);
    proto.set_required_level(100);
    catalog[piece.key] = proto;
  }

  GameState state(catalog, {}, {}, {}, {}, {}, GameMode::kTest);
  std::set<PotentialRank> ranks;
  int cubed = 0;
  for (const std::pair<const EquipSlot, EquipInstance>& kv :
       state.character.equipped()) {
    ASSERT_TRUE(kv.second.CanCube()) << kv.second.prototype().name();
    const Potential& potential = kv.second.potential();
    EXPECT_EQ(potential.lines_size(), kPotentialLines)
        << kv.second.prototype().name() << " was never cubed";
    ranks.insert(potential.rank());
    ++cubed;
  }
  ASSERT_EQ(cubed, 5) << "the four ranks need four pieces to be dealt over";
  EXPECT_EQ(ranks, (std::set<PotentialRank>{
                       POTENTIAL_RANK_RARE, POTENTIAL_RANK_EPIC,
                       POTENTIAL_RANK_UNIQUE, POTENTIAL_RANK_LEGENDARY}));
}

// Nothing is cubed for a player: potential is a thing they buy.
TEST(GameStateTest, PlayModeStartsWithNoPotential) {
  GameState state = MakePlayModeState();
  for (const std::pair<const EquipSlot, EquipInstance>& kv :
       state.character.equipped()) {
    EXPECT_EQ(kv.second.potential().lines_size(), 0);
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
  EXPECT_EQ(test.character.meso(), 100000000000);
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

// A catalog holding the symbol level 200 hands over, under the key
// GrantLevelRewards looks it up by.
std::map<std::string, EquipPrototype> SymbolCatalog() {
  EquipPrototype symbol;
  symbol.set_name("Arcane Symbol: Vanishing Journey");
  symbol.set_equip_slot(EQUIP_SLOT_SYMBOL_VANISHING_JOURNEY);
  symbol.mutable_arcane_symbol()->set_meso_cost_base(8);
  return {{"symbol_vanishing_journey", symbol}};
}

TEST(GrantLevelRewardsTest, ReachingTwoHundredHandsOverTheFirstSymbol) {
  GameState state(SymbolCatalog(), {}, {}, {}, {});
  GrantLevelRewards(state, 199, 200);
  ASSERT_EQ(state.character.inventory().size(), 1);
  EXPECT_EQ(state.character.inventory()[0].prototype().name(),
            "Arcane Symbol: Vanishing Journey");
}

// One idle stretch can carry a character clean past the level, and the symbol
// still has to land.
TEST(GrantLevelRewardsTest, ASpanThatSkipsTheLevelStillGrantsIt) {
  GameState state(SymbolCatalog(), {}, {}, {}, {});
  GrantLevelRewards(state, 195, 210);
  EXPECT_EQ(state.character.inventory().size(), 1);
}

TEST(GrantLevelRewardsTest, NoSecondCopyForClimbingPastItAgain) {
  GameState state(SymbolCatalog(), {}, {}, {}, {});
  GrantLevelRewards(state, 200, 205);
  GrantLevelRewards(state, 205, 210);
  EXPECT_EQ(state.character.inventory().size(), 0);
}

// Honor is paid for every level in the span, long before there is anything to
// spend it on: Inner Ability opens at 160 onto a pool the climb has filled.
TEST(GrantLevelRewardsTest, EveryLevelInTheSpanPaysHonor) {
  GameState state(SymbolCatalog(), {}, {}, {}, {});
  GrantLevelRewards(state, 58, 61);
  EXPECT_EQ(state.character.honor(), 700 + 800 + 800);
  GrantLevelRewards(state, 61, 61);
  EXPECT_EQ(state.character.honor(), 700 + 800 + 800);
}

TEST(GrantLevelRewardsTest, NothingBelowTwoHundred) {
  GameState state(SymbolCatalog(), {}, {}, {}, {});
  GrantLevelRewards(state, 1, 199);
  EXPECT_EQ(state.character.inventory().size(), 0);
}

// The workbench puts a symbol on rather than only carrying it: a symbol in the
// bag is worth no Arcane Force, and the cap is where the maps start asking.
// The spares behind it are what the Symbols tab levels the worn one with.
TEST(GameStateTest, TheWorkbenchAtTheCapWearsItsSymbolAndCarriesSpares) {
  std::map<std::string, EquipPrototype> equips = BowCatalog();
  for (const std::pair<const std::string, EquipPrototype>& entry :
       SymbolCatalog()) {
    equips[entry.first] = entry.second;
  }
  TestOptions test;
  test.job = JOB_ADVANCEMENT_BOW_MASTER;
  test.level = kTrialLevelCap;
  GameState state(equips, {}, {}, {}, {}, BookFor(JOB_BOW_MASTER),
                  GameMode::kTest, test);

  EXPECT_EQ(
      state.character.equipped().count(EQUIP_SLOT_SYMBOL_VANISHING_JOURNEY),
      1u);
  // Twelve duplicates carry a fresh symbol to level 2, so fifteen is one move
  // on the Symbols tab and change.
  const InventoryInstance& bag = state.character.inventory();
  ASSERT_EQ(bag.size(), 15);
  for (int i = 0; i < bag.size(); ++i) {
    EXPECT_TRUE(IsArcaneSymbol(bag[i].prototype()))
        << "row " << i << " is not a symbol";
  }
}

// --- max mode ---

// A catalog holding a piece of each kind --mode=max dresses a Hero in, at the
// levels the real ones are: what a star run reaches is the item's own cap, so
// a level 30 stand-in would hide every band above five.
std::map<std::string, EquipPrototype> MaxCatalog() {
  EquipPrototype axe;
  axe.set_name("Frozen Two-handed Axe");
  axe.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  axe.set_required_level(120);
  axe.set_upgrade_slots(7);
  axe.add_equip_job_categories(EQUIP_JOB_CATEGORY_WARRIOR);
  EquipPrototype hat = axe;
  hat.set_name("Frozen Hat");
  hat.set_equip_slot(EQUIP_SLOT_HAT);
  hat.set_required_level(140);
  EquipPrototype shoulder = hat;
  shoulder.set_name("Royal Black Metal Shoulder");
  shoulder.set_equip_slot(EQUIP_SLOT_SHOULDER);
  EquipPrototype cygnus = shoulder;
  cygnus.set_name("Lionheart Battle Shoulder");
  return {{"frozen_two_handed_axe", axe},
          {"frozen_hat", hat},
          {"royal_black_metal_shoulder", shoulder},
          {"lionheart_battle_shoulder", cygnus}};
}

// Traces for both slots, so every shelf the seeding opens gets filled.
std::map<std::string, Scroll> MaxTraces() {
  std::map<std::string, Scroll> traces = WarriorWeaponTraces();
  Scroll hat = traces.at("str_100");
  hat.set_target(SCROLL_TARGET_ARMOUR);
  hat.set_tier(TierForLevel(140));
  traces["hat_str_100"] = hat;
  Scroll weapon = traces.at("str_100");
  weapon.set_tier(TierForLevel(120));
  traces["weapon_str_100"] = weapon;
  return traces;
}

GameState MakeMaxState(int level) {
  TestOptions options;
  options.job = JOB_ADVANCEMENT_HERO;
  options.level = level;
  return GameState(MaxCatalog(), MaxTraces(), {}, {}, {}, EveryStageBook(),
                   GameMode::kMax, options);
}

const EquipInstance& Worn(const GameState& state, EquipSlot slot) {
  return state.character.equipped().at(slot);
}

// The ceiling at the cap: hammers driven in, every slot of the wider shelf
// passed, and the stars the level's own band pays for -- the weapon three
// past the rest of it.
TEST(GameStateTest, MaxModeAtTheCapWearsTheWholeBand) {
  GameState state = MakeMaxState(kTrialLevelCap);
  const Equip& weapon = Worn(state, EQUIP_SLOT_PRIMARY_WEAPON).equip_state();
  EXPECT_EQ(weapon.hammers(), kMaxHammers);
  EXPECT_EQ(weapon.remaining_upgrade_slots(), 0);
  EXPECT_EQ(weapon.scroll_successes(), 9);
  EXPECT_EQ(weapon.stars(), MaxGearForLevel(kTrialLevelCap).weapon_stars);
  EXPECT_EQ(Worn(state, EQUIP_SLOT_HAT).stars(),
            MaxGearForLevel(kTrialLevelCap).stars);
}

// Every piece carries the same lines, written rather than rolled: the weapon
// the one it is cubed for, the armour three lots of the stat the job fights
// with.
TEST(GameStateTest, MaxModeAtTheCapCarriesItsPotentials) {
  GameState state = MakeMaxState(kTrialLevelCap);
  const Potential& weapon = Worn(state, EQUIP_SLOT_PRIMARY_WEAPON).potential();
  EXPECT_EQ(weapon.rank(), POTENTIAL_RANK_UNIQUE);
  ASSERT_EQ(weapon.lines_size(), kPotentialLines);
  EXPECT_EQ(weapon.lines(0).type(), POTENTIAL_LINE_TYPE_IGNORE_DEFENSE_30);

  const Potential& hat = Worn(state, EQUIP_SLOT_HAT).potential();
  EXPECT_EQ(hat.rank(), POTENTIAL_RANK_EPIC);
  ASSERT_EQ(hat.lines_size(), kPotentialLines);
  EXPECT_EQ(hat.lines(0).type(), POTENTIAL_LINE_TYPE_STR_PCT);
}

// Both potions bought and switched on, with the change of the climb left in
// the purse -- not the workbench's hundred billion.
TEST(GameStateTest, MaxModeAtTheCapHasBoughtBothPotions) {
  GameState state = MakeMaxState(kTrialLevelCap);
  for (const ConsumableInfo& potion : AllConsumables()) {
    EXPECT_TRUE(state.character.ConsumableOwned(potion.type)) << potion.name;
    EXPECT_TRUE(state.character.ConsumableActive(potion.type)) << potion.name;
  }
  EXPECT_EQ(state.character.meso(), 50000000);
  EXPECT_EQ(state.exp_multiplier, 1);
  EXPECT_TRUE(state.character.stackables(ITEM_CATEGORY_USE).empty());
}

// The pools are all spent: the AP into the stat the job swings on, the SP
// into its book, and both Hyper Stat allocations down to the change.
TEST(GameStateTest, MaxModeSpendsEveryPool) {
  GameState state = MakeMaxState(kTrialLevelCap);
  EXPECT_EQ(state.character.proto().ap(), 0);
  for (int stage = 1; stage <= 4; ++stage) {
    EXPECT_EQ(state.character.sp(stage), 0) << "stage " << stage;
  }
  for (StatPreset preset : {StatPreset::kFarming, StatPreset::kBossing}) {
    EXPECT_LT(state.character.hyper_stat_points_left(preset), 20);
  }
  EXPECT_EQ(state.character.ability(StatPreset::kBossing).rank(),
            ABILITY_RANK_LEGENDARY);
}

// A level 140 character is a long way short of the cap's band: no hammers,
// which are 340M across an outfit, and nothing cubed -- cubing opens at 180.
TEST(GameStateTest, MaxModeAtOneFortyIsShortOfTheCapsBand) {
  GameState state = MakeMaxState(kHyperStatUnlockLevel);
  const Equip& weapon = Worn(state, EQUIP_SLOT_PRIMARY_WEAPON).equip_state();
  EXPECT_EQ(weapon.hammers(), 0);
  EXPECT_EQ(weapon.scroll_successes(), 7);
  EXPECT_EQ(weapon.stars(), 14);
  EXPECT_EQ(Worn(state, EQUIP_SLOT_HAT).stars(), 10);
  EXPECT_EQ(Worn(state, EQUIP_SLOT_HAT).potential().lines_size(), 0);
  for (const ConsumableInfo& potion : AllConsumables()) {
    EXPECT_FALSE(state.character.ConsumableOwned(potion.type)) << potion.name;
  }
}

// The Cygnus shoulder is bought with a token off the fight nobody has won, so
// a character measured against the boss roster does not wear one -- and the
// three the workbench carries are not in the bag either.
TEST(GameStateTest, MaxModeWearsNoCygnusShoulder) {
  GameState state = MakeMaxState(kTrialLevelCap);
  EXPECT_EQ(Worn(state, EQUIP_SLOT_SHOULDER).name(),
            "Royal Black Metal Shoulder");
  EXPECT_TRUE(state.character.inventory().empty());
}

}  // namespace
}  // namespace ms
