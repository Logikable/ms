#include "src/game_state.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "src/character/arcane_force.h"
#include "src/character/character.h"
#include "src/character/consumables.h"
#include "src/character/exp_table.h"
#include "src/character/honor.h"
#include "src/character/hyper_stats.h"
#include "src/character/max_character.h"
#include "src/character/progression.h"
#include "src/character/skill_placement.h"
#include "src/character/stat_preset.h"
#include "src/character/v_matrix.h"
#include "src/item/equip_instance.h"
#include "src/item/item.h"
#include "src/item/potential.h"
#include "src/protos/boss.pb.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/map.pb.h"
#include "src/protos/mob.pb.h"
#include "src/protos/save.pb.h"
#include "src/protos/scroll.pb.h"

namespace ms {
namespace {

// A catalog with the equip both modes give out, so seeding finds something.
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

// Which branch the workbench character takes: a setting in game_state.cc, meant
// to be changed to view another job's screens. Read instead of hardcoded, so
// changing it doesn't break the tests below.
Job WorkbenchJob() {
  return MakeTestModeState().character.proto().job();
}

// One skill for each advancement the workbench character passes through, so
// seeding has a book to spend SP on. The levels are real, so what a stage's
// points buy is realistic too.
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
    PlaceIn(skill, advancement);
    skill.set_max_level(kSpByStage[stage]);
    book.insert({"stage_" + std::to_string(stage), skill});
  }
  // The 5th job's book is the matrix: a node of its own, and a common node
  // every matrix has whatever the job.
  Skill node;
  node.set_name("Fifth Surge");
  node.set_kind(SKILL_KIND_ATTACK);
  PlaceIn(node, AdvancementForJobStage(WorkbenchJob(), kFifthJobStage));
  node.set_v_node(V_NODE_KIND_JOB);
  node.set_max_level(MaxVNodeLevel(V_NODE_KIND_JOB));
  book.insert({"fifth_surge", node});
  Skill common = node;
  common.set_name("Common Lift");
  PlaceIn(common, JOB_ADVANCEMENT_COMMON);
  common.set_v_node(V_NODE_KIND_COMMON);
  common.set_max_level(MaxVNodeLevel(V_NODE_KIND_COMMON));
  book.insert({"common_lift", common});
  return book;
}

GameState MakeTestModeStateWithSkills(TestSkills skills = TestSkills::kZero) {
  TestOptions test;
  test.skills = skills;
  return GameState(SwordCatalog(), {}, {}, {}, {}, EveryStageBook(),
                   GameMode::kTest, test);
}

// The item catalog test mode's seeding needs: the two currencies, an ordinary
// Etc item to tell them apart from, and the traces the workbench is given.
std::map<std::string, ItemPrototype> SeededItemCatalog() {
  ItemPrototype token;
  token.set_name("Weapon Token");
  token.set_kind(ITEM_KIND_TOKEN);
  token.set_currency_mark("●");
  ItemPrototype shard;
  shard.set_name("Zakum's Soul Shard");
  shard.set_kind(ITEM_KIND_SOUL_SHARD);
  shard.set_short_name("Zakum's");
  ItemPrototype horn;
  horn.set_name("Beetle's Horn");
  horn.set_sell_price(230);
  ItemPrototype trace;
  trace.set_name("Spell Trace");
  trace.set_kind(ITEM_KIND_SPELL_TRACE);
  return {{"weapon_token", token},
          {"zakums_soul_shard", shard},
          {"horn", horn},
          {"spell_trace", trace}};
}

// The stack of `name` in the bag, or nullptr if there's none.
const StackableItem* FindStack(const GameState& state,
                               const std::string& name) {
  for (const StackableItem& stack : state.character.stackables()) {
    if (stack.name() == name) {
      return &stack;
    }
  }
  return nullptr;
}

GameState MakeTestModeStateWithItems() {
  return GameState(SwordCatalog(), {}, SeededItemCatalog(), {}, {}, {},
                   GameMode::kTest);
}

GameState MakePlayModeStateWithItems() {
  return GameState(SwordCatalog(), {}, SeededItemCatalog(), {}, {}, {},
                   GameMode::kPlay);
}

// The constructor stores each of the five catalogs in the field named after it.
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

// The workbench starts with a finished character, because everything past level
// thirty would otherwise take thirty hours to reach. Play mode is where
// levelling one at a time is worth watching.
TEST(GameStateTest, TestModeStartsAtTheTopOfTheWrittenLine) {
  GameState test = MakeTestModeState();
  // The cap: the 4th job is the last one written, so there's no band above it
  // and the workbench stops where the EXP table ends.
  int stage = test.character.proto().job_stage();
  EXPECT_EQ(stage, kLastJobStage);
  EXPECT_EQ(test.character.proto().level(), kTrialLevelCap);
  // Some warrior branch, not a specific one; see WorkbenchJob. Walked down the
  // job tree from the 1st job, so a workbench somewhere the choices can't lead
  // fails here.
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
  // Nothing between the tester and the screens: no pending advancement and no
  // unspent points.
  EXPECT_FALSE(test.character.CanAdvanceJob());
  EXPECT_EQ(test.character.proto().ap(), 0);
}

// Both settings are on for the workbench: it has two stat allocations at once,
// and a tester hears the music. Play mode leaves both to the player.
TEST(GameStateTest, TestModeThrowsTheWorkbenchSwitches) {
  GameState state = MakeTestModeState();
  EXPECT_TRUE(state.account.autoswap_presets());
  EXPECT_TRUE(state.character.autoswap_presets());
  EXPECT_EQ(state.account.jukebox_mode(), JUKEBOX_MODE_SHUFFLE);

  GameState played = MakePlayModeState();
  EXPECT_FALSE(played.account.autoswap_presets());
  EXPECT_EQ(played.account.jukebox_mode(), JUKEBOX_MODE_FOLLOW_MAP);
}

TEST(GameStateTest, SkillsZeroLeavesTheJobsOwnBookUnbought) {
  GameState state = MakeTestModeStateWithSkills();
  int top = state.character.proto().job_stage();
  for (int stage = 1; stage < top; ++stage) {
    EXPECT_EQ(state.character.sp(stage), 0) << "stage " << stage;
  }
  // The 5th job's book is the matrix, so every node (including common ones) is
  // left unbought and the points stay in the pool.
  bool fifth = top >= kFifthJobStage;
  if (fifth) {
    EXPECT_GT(state.character.v_points(), 0);
  } else {
    EXPECT_GT(state.character.sp(top), 0);
  }
  for (const std::pair<const std::string, Skill>& entry : state.skills) {
    const Skill& skill = entry.second;
    int stage = StageForAdvancement(BookOf(skill));
    bool held = fifth ? skill.v_node() == V_NODE_KIND_UNSPECIFIED : stage < top;
    int expected = held ? skill.max_level() : 0;
    EXPECT_EQ(state.character.skill_level(skill), expected)
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

// The workbench's level 30 warrior weapon, with slots to scroll and room for
// stars. Keyed as WorkbenchGearFor names it, or nothing is worn at all.
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

// Two traces for a warrior's weapon, the riskier one paying more. The workbench
// passes every slot, so it should take the one that pays more.
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
  return state.character.equipped()
      .at(EQUIP_SLOT_PRIMARY_WEAPON)
      ->equip_state();
}

// No flags: gear arrives as it drops, with slots to spend and no stars.
TEST(GameStateTest, NoUpgradeFlagLeavesTheGearAsItDrops) {
  GameState state = MakeEquipsState(GearSetup());
  EXPECT_EQ(WornWeapon(state).remaining_upgrade_slots(), 7);
  EXPECT_EQ(WornWeapon(state).hammers(), 0);
  EXPECT_EQ(WornWeapon(state).scroll_successes(), 0);
  EXPECT_EQ(WornWeapon(state).stars(), 0);
}

// Each flag does only its own job: hammers add slots and leave them all
// unspent.
TEST(GameStateTest, HammeredWidensTheShelfWithoutFillingIt) {
  GearSetup equips;
  equips.hammered = true;
  GameState state = MakeEquipsState(equips);
  const Equip& worn = WornWeapon(state);
  EXPECT_EQ(worn.hammers(), kMaxHammers);
  EXPECT_EQ(worn.remaining_upgrade_slots(), 9);
  EXPECT_EQ(worn.scroll_successes(), 0);
}

// Scrolling alone passes the item's original slots, with the best-paying trace.
TEST(GameStateTest, ScrolledPassesTheSlotsTheItemHas) {
  GearSetup equips;
  equips.scrolled = true;
  GameState state = MakeEquipsState(equips);
  const Equip& worn = WornWeapon(state);
  EXPECT_EQ(worn.hammers(), 0);
  EXPECT_EQ(worn.remaining_upgrade_slots(), 0);
  EXPECT_EQ(worn.scroll_successes(), 7);
  EXPECT_EQ(worn.scroll_stats().attack(), 35);
  EXPECT_EQ(worn.scroll_stats().str(), 21);
  EXPECT_EQ(worn.stars(), 0);
}

// Together, the widened set of slots is what gets filled.
TEST(GameStateTest, HammeredAndScrolledFillTheWiderShelf) {
  GearSetup equips;
  equips.hammered = true;
  equips.scrolled = true;
  GameState state = MakeEquipsState(equips);
  const Equip& worn = WornWeapon(state);
  EXPECT_EQ(worn.hammers(), kMaxHammers);
  EXPECT_EQ(worn.remaining_upgrade_slots(), 0);
  EXPECT_EQ(worn.scroll_successes(), 9);
  EXPECT_EQ(worn.scroll_stats().attack(), 45);
  EXPECT_EQ(worn.scroll_stats().str(), 27);
}

// --sf sets exactly the stars given, capped by the item: a level 30 weapon
// takes five however many are requested.
TEST(GameStateTest, SfSetsTheStarsItNamesUpToTheItemsCap) {
  GearSetup equips;
  equips.scrolled = true;
  equips.stars = 3;
  EXPECT_EQ(WornWeapon(MakeEquipsState(equips)).stars(), 3);

  equips.stars = 22;
  EXPECT_EQ(WornWeapon(MakeEquipsState(equips)).stars(),
            EquipTabItem::MaxStarsForLevel(30));
}

// Stars need no slots left to scroll, the upgrade screen's own rule, so --sf on
// an item with unspent slots leaves it unstarred.
TEST(GameStateTest, SfWaitsForAShelfWithNothingLeftOnIt) {
  GearSetup equips;
  equips.stars = 3;
  EXPECT_EQ(WornWeapon(MakeEquipsState(equips)).stars(), 0);

  equips.hammered = true;
  EXPECT_EQ(WornWeapon(MakeEquipsState(equips)).stars(), 0);
}

// An item that can't take an upgrade type is left alone for it, whatever the
// flags: the workbench can't override the data.
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

// Gloves take no stat trace, so the workbench uses the attack one, and the
// scrolled slots let the stars go on.
TEST(GameStateTest, TheFlagsScrollGlovesWithTheAttackTrace) {
  // Stored under the key WorkbenchGearFor uses, which decides what a Swordman
  // gets; the prototype behind it is up to the catalog.
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
      state.character.equipped().at(EQUIP_SLOT_GLOVES)->equip_state();
  EXPECT_EQ(worn.hammers(), kMaxHammers);
  EXPECT_EQ(worn.scroll_successes(), 7);
  EXPECT_EQ(worn.scroll_stats().attack(), 21);
  EXPECT_EQ(worn.stars(), EquipTabItem::MaxStarsForLevel(30));
}

// The stat trace wins where both exist: a weapon gets STR, not the ATT the
// fallback would use.
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

// A piece with no scroll slots doesn't get hammered either, since there are no
// slots to add to.
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

// A bow, so a chosen bowman job has its own weapon to be given. Named as
// StarterEquipsFor and the workbench's table name it.
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
// unspent, not unspendable. One skill per advancement that job reaches.
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
    PlaceIn(skill, advancement);
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

// A workbench character is named after its job, so several in a party can be
// told apart. The slash in "I/L Arch Mage" isn't a valid name character.
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

// --job stops at the top of the named advancement, not the workbench's default:
// an Archer at the last level before the 2nd job, a Hunter at the last before
// the 3rd.
TEST(GameStateTest, ChosenJobStartsAtTheTopOfThatAdvancement) {
  GameState archer = MakeChosenJobState(JOB_ADVANCEMENT_ARCHER);
  EXPECT_EQ(archer.character.proto().job(), JOB_ARCHER);
  EXPECT_EQ(archer.character.proto().level(), 30);

  GameState hunter = MakeChosenJobState(JOB_ADVANCEMENT_HUNTER);
  EXPECT_EQ(hunter.character.proto().job(), JOB_HUNTER);
  EXPECT_EQ(hunter.character.proto().level(), NextAdvancementLevel(2));
  EXPECT_EQ(hunter.character.proto().job_stage(), 2);
}

// --level stops levelling where the tester asked instead of the top of the
// band, which is how a workbench reaches a screen that opens partway through an
// advancement.
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

// AP is spent, since which stats to raise is never what a tester is checking.
// Only the chosen job's own book is left, since earlier books weren't requested
// either.
TEST(GameStateTest, ChosenJobSpendsTheApAndEveryBookBelowItsOwn) {
  GameState state = MakeChosenJobState(JOB_ADVANCEMENT_HUNTER);
  EXPECT_EQ(state.character.proto().ap(), 0);
  EXPECT_GT(state.character.proto().allocated_stats().dex(), 200);
  EXPECT_EQ(state.character.sp(1), 0);
  EXPECT_GT(state.character.sp(2), 0);
  for (const std::pair<const std::string, Skill>& entry : state.skills) {
    int stage = StageForAdvancement(BookOf(entry.second));
    int expected = stage < 2 ? entry.second.max_level() : 0;
    EXPECT_EQ(state.character.skill_level(entry.second), expected)
        << entry.first << " at stage " << stage;
  }
}

// A 1st job's book is its character's highest, so nothing is bought.
TEST(GameStateTest, AChosenFirstJobKeepsItsWholeBook) {
  GameState state = MakeChosenJobState(JOB_ADVANCEMENT_ARCHER);
  EXPECT_GT(state.character.sp(1), 0);
  for (const std::pair<const std::string, Skill>& entry : state.skills) {
    EXPECT_EQ(state.character.skill_level(entry.second), 0) << entry.first;
  }
}

// Worn, not carried: a 2nd job advances without a new weapon, so the chosen job
// would arrive empty-handed. An Archer at 30 wears the Ryden, a Hunter at 60
// the Asianic Bow.
TEST(GameStateTest, ChosenJobWearsTheWeaponItsLevelTopsOutAt) {
  GameState archer = MakeChosenJobState(JOB_ADVANCEMENT_ARCHER);
  ASSERT_TRUE(archer.character.equipped().count(EQUIP_SLOT_PRIMARY_WEAPON));
  EXPECT_EQ(archer.character.equipped().at(EQUIP_SLOT_PRIMARY_WEAPON)->name(),
            "Ryden");

  GameState hunter = MakeChosenJobState(JOB_ADVANCEMENT_HUNTER);
  ASSERT_TRUE(hunter.character.equipped().count(EQUIP_SLOT_PRIMARY_WEAPON));
  EXPECT_EQ(hunter.character.equipped().at(EQUIP_SLOT_PRIMARY_WEAPON)->name(),
            "Asianic Bow");
}

// The token shelves need currency, and a workbench exists to skip bosses; with
// no shard the Token tab shows half its columns. Only currencies, not Etc
// drops.
TEST(GameStateTest, TestModeStartsWithEveryCurrency) {
  GameState state = MakeTestModeStateWithItems();
  const CurrencyPurse& purse = state.character.currencies();
  EXPECT_GT(purse.Count("Weapon Token"), 1);
  EXPECT_GT(purse.Count("Zakum's Soul Shard"), 1);
  EXPECT_EQ(purse.Count("Beetle's Horn"), 0);
  EXPECT_EQ(FindStack(state, "Beetle's Horn"), nullptr);
}

// Scrolling costs traces and the shop sells them 5,000 meso at a time, a long
// way to get to the screen a tester wants, so a balance is given instead.
TEST(GameStateTest, TestModeCarriesSpellTraces) {
  GameState state = MakeTestModeStateWithItems();
  EXPECT_GE(state.character.currencies().Count("Spell Trace"), 30000);
}

// Only test mode gets currencies: play mode gets none, and no Etc drops either.
TEST(GameStateTest, PlayModeGetsNoCurrencies) {
  GameState state = MakePlayModeStateWithItems();
  EXPECT_TRUE(state.character.currencies().entries().empty());
  EXPECT_TRUE(state.character.stackables().empty());
}

// --- play mode ---

// Everything a new character gets: a level-1 Beginner on Maple Island with the
// default stats, no meso, and a weapon but nothing carried, since the Sword is
// worn and the bag is empty.
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
  EXPECT_EQ(state.character.equipped().at(EQUIP_SLOT_PRIMARY_WEAPON)->name(),
            "Sword");
}

// --- test mode ---

// Meso to spend, and an empty equip tab: the workbench wears what it's given,
// so a tester opens the bag to see only what they put there. The spare symbols
// are the only thing carried, and this catalog has none.
TEST(GameStateTest, TestModeStartsWithMesoAndAnEmptyBag) {
  GameState state = MakeTestModeState();
  EXPECT_EQ(state.character.meso(), 100000000000);
  EXPECT_TRUE(state.character.inventory().empty());
}

// Clearing the bag is what makes this true for a job whose gear doesn't all
// fit: a Rogue gets a dagger and a claw for one slot, and the displaced one
// would otherwise stay in the bag.
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

// Both modes start with a weapon equipped. A level 1 character has no equipped
// panel or bag, so a weapon only carried could never be used, and without
// attacking there's no EXP and no way past level 1.
TEST(GameStateTest, BothModesStartWearingAWeapon) {
  EXPECT_FALSE(MakeTestModeState().character.equipped().empty());
  EXPECT_FALSE(MakePlayModeState().character.equipped().empty());
}

// A set bonus can't be seen on the stats page without the whole set, and
// farming one takes levelling to 100. So the workbench wears it, so nothing
// needs equipping to see the bonus.
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
  const WornGear& worn = state.character.equipped();
  for (const Piece& piece : kPieces) {
    WornGear::const_iterator it = worn.find(piece.slot);
    ASSERT_NE(it, worn.end()) << "the workbench has no " << piece.name;
    EXPECT_EQ(it->second->prototype().name(), piece.name);
  }
  // And no second copy in the bag: four pieces nobody can wear twice were four
  // rows of clutter in the way.
  const InventoryInstance& bag = state.character.inventory();
  for (int i = 0; i < bag.size(); ++i) {
    EXPECT_EQ(bag[i].prototype().name().find("Frozen"), std::string::npos)
        << bag[i].prototype().name() << " is carried as well as worn";
  }
}

// Potential should be visible at every rank, so the workbench assigns the four
// ranks across its worn gear instead of letting each piece roll. A run where
// nothing reached Legendary would leave the display half-tested.
TEST(GameStateTest, TestModeCubesEveryPieceItCanAndSpreadsTheRanks) {
  // The armour the workbench wears which, with the sword, gives five slots with
  // potential: enough to assign every rank.
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
  for (const std::pair<const EquipSlot, const EquipInstance*>& kv :
       state.character.equipped()) {
    ASSERT_TRUE(kv.second->CanCube()) << kv.second->prototype().name();
    const Potential& potential = kv.second->potential();
    EXPECT_EQ(potential.lines_size(), kPotentialLines)
        << kv.second->prototype().name() << " was never cubed";
    ranks.insert(potential.rank());
    ++cubed;
  }
  ASSERT_EQ(cubed, 5) << "the four ranks need four pieces to be dealt over";
  EXPECT_EQ(ranks, (std::set<PotentialRank>{
                       POTENTIAL_RANK_RARE, POTENTIAL_RANK_EPIC,
                       POTENTIAL_RANK_UNIQUE, POTENTIAL_RANK_LEGENDARY}));
}

// Nothing is cubed for a player: potential is something they buy.
TEST(GameStateTest, PlayModeStartsWithNoPotential) {
  GameState state = MakePlayModeState();
  for (const std::pair<const EquipSlot, const EquipInstance*>& kv :
       state.character.equipped()) {
    EXPECT_EQ(kv.second->potential().lines_size(), 0);
  }
}

TEST(GameStateTest, TestModeStartsOnAHuntingGround) {
  EXPECT_EQ(MakeTestModeState().current_map, "right_around_lith_harbor");
}

// Neither mode may require a catalog entry: a test's state has no data files,
// and seeding must not fail because of that.
TEST(GameStateTest, SeedingSkipsEquipsTheCatalogDoesNotHave) {
  GameState play({}, {}, {}, {}, {}, {}, GameMode::kPlay);
  EXPECT_TRUE(play.character.inventory().empty());
  EXPECT_TRUE(play.character.equipped().empty());
  GameState test({}, {}, {}, {}, {}, {}, GameMode::kTest);
  EXPECT_TRUE(test.character.inventory().empty());
  // Meso doesn't depend on the catalog, so it still arrives.
  EXPECT_EQ(test.character.meso(), 100000000000);
}

// Play is what a plain construction gives, so the default is the game, not the
// workbench.
TEST(GameStateTest, PlayIsTheDefaultMode) {
  GameState state(SwordCatalog(), {}, {}, {}, {});
  EXPECT_EQ(state.character.proto().level(), 1);
  EXPECT_EQ(state.current_map, "maple_island");
}

// The workbench levels with an EXP bonus instead of farming the early levels at
// normal speed. Play mode earns normally.
TEST(GameStateTest, TestModeFarmsOnAnExpBonus) {
  EXPECT_EQ(MakeTestModeState().exp_multiplier, 5);
}

TEST(GameStateTest, PlayModeEarnsPlainExp) {
  EXPECT_EQ(MakePlayModeState().exp_multiplier, 1);
}

// A catalog with the symbol given at level 200, under the key GrantLevelRewards
// looks up.
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

// One idle period can take a character straight past the level, and the symbol
// must still arrive.
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

// Honor is paid for every level in the range, long before there's anything to
// spend it on: Inner Ability opens at 160 with a pool levelling has filled.
TEST(GrantLevelRewardsTest, EveryLevelInTheSpanPaysHonor) {
  GameState state(SymbolCatalog(), {}, {}, {}, {});
  GrantLevelRewards(state, 58, 61);
  EXPECT_EQ(state.character.honor(), 700 + 800 + 800);
  GrantLevelRewards(state, 61, 61);
  EXPECT_EQ(state.character.honor(), 700 + 800 + 800);
}

// A character alone on the account gains one level at a time; once someone is
// ahead of them, the same EXP gives two. Rewards follow the whole range,
// including burned levels.
TEST(AwardExpTest, BurnsAgainstTheRestOfTheRoster) {
  GameState state(SymbolCatalog(), {}, {}, {}, {});
  AwardExp(state, 15);
  EXPECT_EQ(state.character.proto().level(), 2);

  CharacterSave ahead;
  ahead.mutable_character()->set_level(100);
  state.inactive_characters.push_back(ahead);
  AwardExp(state, 34);
  EXPECT_EQ(state.character.proto().level(), 4);
  EXPECT_EQ(state.character.honor(), HonorForLevels(1, 4));
}

TEST(GrantLevelRewardsTest, NothingBelowTwoHundred) {
  GameState state(SymbolCatalog(), {}, {}, {}, {});
  GrantLevelRewards(state, 1, 199);
  EXPECT_EQ(state.character.inventory().size(), 0);
}

// The workbench wears a symbol instead of only carrying it: a symbol in the bag
// gives no Arcane Force, and maps start requiring it at the cap. The spares are
// for levelling the worn one on the Symbols tab.
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
  // Twelve duplicates take a new symbol to level 2, so fifteen is one level on
  // the Symbols tab with some left over.
  const InventoryInstance& bag = state.character.inventory();
  ASSERT_EQ(bag.size(), 15);
  for (int i = 0; i < bag.size(); ++i) {
    EXPECT_TRUE(IsArcaneSymbol(bag[i].prototype()))
        << "row " << i << " is not a symbol";
  }
}

// --- max mode ---

// A catalog with one piece of each kind --mode=max gives a Hero, at their real
// levels: star force is capped by the item's level, so a level 30 stand-in
// would hide every band above five stars.
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
  std::map<std::string, EquipPrototype> catalog = {
      {"frozen_two_handed_axe", axe},
      {"frozen_hat", hat},
      {"royal_black_metal_shoulder", shoulder},
      {"lionheart_battle_shoulder", cygnus}};
  // The six river areas at the levels they open, since that decides which ones
  // a max character owns.
  const std::pair<EquipSlot, int> kSymbols[] = {
      {EQUIP_SLOT_SYMBOL_VANISHING_JOURNEY, 200},
      {EQUIP_SLOT_SYMBOL_CHU_CHU_ISLAND, 210},
      {EQUIP_SLOT_SYMBOL_LACHELEIN, 220},
      {EQUIP_SLOT_SYMBOL_ARCANA, 225},
      {EQUIP_SLOT_SYMBOL_MORASS, 230},
      {EQUIP_SLOT_SYMBOL_ESFERA, 235}};
  for (const std::pair<EquipSlot, int>& entry : kSymbols) {
    EquipPrototype symbol;
    symbol.set_name("Symbol " + std::to_string(entry.second));
    symbol.set_equip_slot(entry.first);
    symbol.set_required_level(200);
    symbol.add_equip_job_categories(EQUIP_JOB_CATEGORY_UNIVERSAL);
    symbol.mutable_arcane_symbol()->set_area_level(entry.second);
    catalog[symbol.name()] = symbol;
  }
  return catalog;
}

// Traces for both slot types, so every slot seeding opens gets filled.
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

// One defended boss and one ordinary monster, which is all the Hyper Stat
// allocation reads the roster for: how much of an attack each preset's fight
// cancels.
std::map<std::string, Mob> MaxMobs(int boss_pdr = 100) {
  std::map<std::string, Mob> mobs;
  Mob& wall = mobs["wall"];
  wall.set_boss(true);
  wall.set_level(200);
  wall.set_pdr(boss_pdr);
  Mob& snail = mobs["snail"];
  snail.set_level(200);
  snail.set_pdr(10);
  return mobs;
}

std::map<std::string, Boss> MaxBosses(int unlock_level = 200) {
  std::map<std::string, Boss> bosses;
  BossDifficulty& difficulty = *bosses["wall"].add_difficulties();
  difficulty.set_unlock_level(unlock_level);
  difficulty.add_phases()->add_spawns()->set_mob("wall");
  return bosses;
}

GameState MakeMaxState(int level, JobAdvancement job = JOB_ADVANCEMENT_HERO,
                       std::map<std::string, Boss> bosses = MaxBosses(),
                       std::map<std::string, Mob> mobs = MaxMobs()) {
  TestOptions options;
  options.job = job;
  options.level = level;
  return GameState(MaxCatalog(), MaxTraces(), {}, std::move(mobs), {},
                   EveryStageBook(), GameMode::kMax, options, std::nullopt, {},
                   std::move(bosses));
}

// A max character has both allocations at once, and plays music, like the
// workbench.
TEST(GameStateTest, MaxModeThrowsTheSameSwitches) {
  GameState state = MakeMaxState(230);
  EXPECT_TRUE(state.account.autoswap_presets());
  EXPECT_TRUE(state.character.autoswap_presets());
  EXPECT_EQ(state.account.jukebox_mode(), JUKEBOX_MODE_SHUFFLE);
}

const EquipInstance& Worn(const GameState& state, EquipSlot slot) {
  return *state.character.equipped().at(slot);
}

// A max account has one character at the top of every other job line, so link
// skills stand where a fully played account has them: 9 for three-line
// branches, 6 for two-line ones.
TEST(GameStateTest, MaxModeFillsTheRosterSoTheLinkSkillsStand) {
  GameState state = MakeMaxState(kTrialLevelCap);
  EXPECT_EQ(state.inactive_characters.size(), 9u);

  LinkTally tally = state.character.link_tally();
  EXPECT_EQ(tally.LevelFor(JOB_SWORDMAN), 6)
      << "the Hero being played is not in the mirrored half";
  const CharacterInstance& hero = state.character;
  EXPECT_EQ(hero.link_tally()
                .With(hero.proto().job(), hero.proto().level())
                .LevelFor(JOB_SWORDMAN),
            9);
  EXPECT_EQ(tally.LevelFor(JOB_MAGICIAN), 9);
  EXPECT_EQ(tally.LevelFor(JOB_ARCHER), 6);
  EXPECT_EQ(tally.LevelFor(JOB_ROGUE), 6);
}

// Every slot is a fully built max character, not a sheet with a level set. The
// test catalog has only warrior gear, so this checks the character, not the
// outfit.
TEST(GameStateTest, MaxModeRosterSlotsAreCeilingsThemselves) {
  GameState state = MakeMaxState(kTrialLevelCap);
  ASSERT_EQ(state.inactive_characters.size(), 9u);
  for (const CharacterSave& slot : state.inactive_characters) {
    const Character& sheet = slot.character();
    SCOPED_TRACE(sheet.name());
    EXPECT_FALSE(sheet.name().empty());
    EXPECT_EQ(sheet.level(), kTrialLevelCap);
    EXPECT_EQ(sheet.ap(), 0);
    EXPECT_EQ(sheet.meso(), 50000000);
    EXPECT_EQ(slot.current_map(), kHomeMap);
    EXPECT_EQ(sheet.consumables().owned_size(),
              static_cast<int>(AllConsumables().size()));
    EXPECT_EQ(sheet.consumables().active_size(),
              static_cast<int>(AllConsumables().size()));
    // Every area the cap opens, worn: levelling really happened, instead of a
    // level written onto a blank sheet.
    ASSERT_GT(sheet.equip_presets().presets_size(), 0);
    const EquipPreset& worn = sheet.equip_presets().presets(0);
    for (EquipSlot symbol :
         {EQUIP_SLOT_SYMBOL_VANISHING_JOURNEY, EQUIP_SLOT_SYMBOL_CHU_CHU_ISLAND,
          EQUIP_SLOT_SYMBOL_LACHELEIN, EQUIP_SLOT_SYMBOL_ARCANA,
          EQUIP_SLOT_SYMBOL_MORASS, EQUIP_SLOT_SYMBOL_ESFERA}) {
      EXPECT_EQ(worn.equipped().count(symbol), 1u) << EquipSlot_Name(symbol);
    }
    EXPECT_GT(sheet.inner_ability().presets_size(), 0);
  }
}

// A sim's max character stands alone: no roster, and not even their own line's
// link skill, however high their level. See TestOptions::link_skills.
TEST(GameStateTest, MaxModeCanBeAskedForNoLinkSkillsAtAll) {
  TestOptions options;
  options.job = JOB_ADVANCEMENT_HERO;
  options.level = kTrialLevelCap;
  options.link_skills = false;
  GameState state(MaxCatalog(), MaxTraces(), {}, MaxMobs(), {},
                  EveryStageBook(), GameMode::kMax, options, std::nullopt, {},
                  MaxBosses());
  EXPECT_TRUE(state.inactive_characters.empty());
  EXPECT_TRUE(state.character.link_skills_off());
}

// The roster levels with the max character, not ahead of it: below the first
// link skill threshold, the roster gives nothing.
TEST(GameStateTest, MaxModeBelowTheFirstRungHasNoLinkSkills) {
  GameState state = MakeMaxState(60);
  EXPECT_FALSE(state.inactive_characters.empty());
  EXPECT_EQ(state.character.link_tally().LevelFor(JOB_SWORDMAN), 0);
}

// The max character at the cap: hammers used, every widened slot passed, and
// the stars the level's band pays for, with the weapon three stars higher.
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

// Every piece has the same lines, set instead of rolled: the weapon gets its
// own lines, and armour gets three lines of the job's main stat.
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

// Every buff bought and switched on, with the climb's leftover meso, not the
// workbench's hundred billion.
TEST(GameStateTest, MaxModeAtTheCapHasBoughtEveryBuff) {
  GameState state = MakeMaxState(kTrialLevelCap);
  for (const ConsumableInfo& buff : AllConsumables()) {
    EXPECT_TRUE(state.character.ConsumableOwned(buff.type)) << buff.name;
    EXPECT_TRUE(state.character.ConsumableActive(buff.type)) << buff.name;
  }
  EXPECT_EQ(state.character.meso(), 50000000);
  EXPECT_EQ(state.exp_multiplier, 1);
  EXPECT_TRUE(state.character.stackables().empty());
}

// Every Arcane Symbol the level has unlocked, worn and levelled, and none it
// hasn't. A max character is in the river, and Arcane Force is checked against
// it.
TEST(GameStateTest, MaxModeWearsTheSymbolsItsLevelOpened) {
  // Below Esfera's 235, so one area is still locked.
  const int kLevel = 230;
  GameState state = MakeMaxState(kLevel);
  int worn = 0;
  for (const std::pair<const EquipSlot, const EquipInstance*>& entry :
       state.character.equipped()) {
    if (!IsArcaneSymbol(entry.second->prototype())) {
      continue;
    }
    ++worn;
    EXPECT_LE(entry.second->prototype().arcane_symbol().area_level(), kLevel)
        << entry.second->prototype().name();
    EXPECT_EQ(SymbolLevel(entry.second->equip_state()), 10)
        << entry.second->prototype().name();
  }
  // Vanishing Journey, Chu Chu Island, Lachelein, Arcana and Morass: every area
  // open at 230, but not Esfera at 235.
  EXPECT_EQ(worn, 5);
  EXPECT_EQ(state.character.arcane_force(), 5 * SymbolArcaneForce(10));
}

// Every pool is spent: AP into the job's main stat, SP into its book, and both
// Hyper Stat allocations down to leftovers.
TEST(GameStateTest, MaxModeSpendsEveryPool) {
  GameState state = MakeMaxState(kTrialLevelCap);
  EXPECT_EQ(state.character.proto().ap(), 0);
  for (int stage = 1; stage <= 4; ++stage) {
    EXPECT_EQ(state.character.sp(stage), 0) << "stage " << stage;
  }
  for (StatPreset preset : {StatPreset::kFirst, StatPreset::kSecond}) {
    EXPECT_LT(state.character.hyper_stat_points_left(preset), 20);
  }
  EXPECT_EQ(state.character.ability(StatPreset::kSecond).rank(),
            ABILITY_RANK_LEGENDARY);
}

// Nothing goes into a stat this character's damage never uses. Each stat's
// value is measured through combat power, and neither of these affects it,
// which avoids a hand-maintained list of stats that matter.
TEST(GameStateTest, MaxModeBuysNoHyperStatThatPaysNothing) {
  GameState state = MakeMaxState(kTrialLevelCap);
  const CharacterInstance& c = state.character;
  for (StatPreset preset : {StatPreset::kFirst, StatPreset::kSecond}) {
    EXPECT_EQ(c.hyper_stat_level(HYPER_STAT_FIELD_MAX_HP, preset), 0);
    EXPECT_EQ(c.hyper_stat_level(HYPER_STAT_FIELD_EXP, preset), 0);
  }
  // A Hero attacks with STR, so the other three main stats are worth nothing to
  // them.
  EXPECT_GT(c.hyper_stat_level(HYPER_STAT_FIELD_STR, StatPreset::kFirst), 0);
  for (HyperStatField spare : {HYPER_STAT_FIELD_INT, HYPER_STAT_FIELD_LUK}) {
    EXPECT_EQ(c.hyper_stat_level(spare, StatPreset::kFirst), 0)
        << HyperStatField_Name(spare);
  }
}

// The two allocations differ where the fights differ: boss damage is worthless
// while farming, and a boss's defence is what makes Ignore Defense worth
// buying.
TEST(GameStateTest, MaxModeHyperStatsFollowTheFightTheyAreFor) {
  GameState state = MakeMaxState(kTrialLevelCap);
  const CharacterInstance& c = state.character;
  EXPECT_GT(
      c.hyper_stat_level(HYPER_STAT_FIELD_BOSS_DAMAGE, StatPreset::kSecond), 0);
  EXPECT_EQ(
      c.hyper_stat_level(HYPER_STAT_FIELD_BOSS_DAMAGE, StatPreset::kFirst), 0);
  EXPECT_GT(c.hyper_stat_level(HYPER_STAT_FIELD_IED, StatPreset::kSecond),
            c.hyper_stat_level(HYPER_STAT_FIELD_IED, StatPreset::kFirst))
      << "the boss cancels most of a swing; the monsters barely any";
}

// A fight the character's level hasn't unlocked is ignored when pricing: the
// level gate decides whether it's ahead of them.
TEST(GameStateTest, MaxModeIgnoresABossItCannotYetFight) {
  GameState shut = MakeMaxState(kTrialLevelCap, JOB_ADVANCEMENT_HERO,
                                MaxBosses(/*unlock_level=*/300));
  GameState open = MakeMaxState(kTrialLevelCap);
  EXPECT_LT(shut.character.hyper_stat_level(HYPER_STAT_FIELD_IED,
                                            StatPreset::kSecond),
            open.character.hyper_stat_level(HYPER_STAT_FIELD_IED,
                                            StatPreset::kSecond));
}

// --job names the line, not where to stop in it: the 5th advancement opens at
// 200 and the max character has taken it.
TEST(GameStateTest, MaxModeTakesEveryAdvancementTheLevelOffers) {
  GameState state = MakeMaxState(NextAdvancementLevel(kFifthJobStage - 1));
  EXPECT_EQ(state.character.proto().job_stage(), kFifthJobStage);
  EXPECT_TRUE(state.character.v_matrix_unlocked());
  EXPECT_EQ(state.character.proto().job(), JOB_HERO);
}

// A stage with no branch chosen stops levelling: nothing says which of the
// three a Swordman became.
TEST(GameStateTest, MaxModeStaysPutWhenTheLineDoesNotBranch) {
  GameState state = MakeMaxState(kTrialLevelCap, JOB_ADVANCEMENT_SWORDMAN);
  EXPECT_EQ(state.character.proto().job_stage(), 1);
}

// Every matrix node at its maximum, common ones included, and nothing left in
// the pool: the max character spent everything.
TEST(GameStateTest, MaxModeMaxesTheWholeMatrix) {
  GameState state = MakeMaxState(kTrialLevelCap);
  int nodes = 0;
  for (const std::pair<const std::string, Skill>& entry : state.skills) {
    if (entry.second.v_node() == V_NODE_KIND_UNSPECIFIED) {
      continue;
    }
    ++nodes;
    EXPECT_EQ(state.character.skill_level(entry.second),
              entry.second.max_level())
        << entry.first;
  }
  EXPECT_EQ(nodes, 2);
  EXPECT_EQ(state.character.v_points(), 0);
}

// A level 140 character is well short of the cap's band: no hammers, which cost
// 340M across all gear, and nothing cubed, since cubing opens at 180.
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

// The Cygnus shoulder is bought with a token from the fight nobody has won, so
// a character measured against the boss roster doesn't wear one, and the three
// the workbench gets aren't in the bag either.
TEST(GameStateTest, MaxModeWearsNoCygnusShoulder) {
  GameState state = MakeMaxState(kTrialLevelCap);
  EXPECT_EQ(Worn(state, EQUIP_SLOT_SHOULDER).name(),
            "Royal Black Metal Shoulder");
  EXPECT_TRUE(state.character.inventory().empty());
}

}  // namespace
}  // namespace ms
