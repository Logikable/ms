#include "src/combat/offline.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>

#include "src/combat/encounter.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/map.pb.h"
#include "src/protos/mob.pb.h"

namespace ms {
namespace {

// A weak mob: one sword hit kills it, worth 3 EXP, always drops a shell.
Mob SnailMob() {
  Mob mob;
  mob.set_name("Snail");
  mob.set_level(1);
  mob.set_max_hp(10);
  mob.set_exp(3);
  MobDrop* drop = mob.add_drops();
  drop->set_item("green_snail_shell");
  drop->set_per_kill(1.0);
  return mob;
}

ItemPrototype GreenSnailShell() {
  ItemPrototype item;
  item.set_name("Green Snail Shell");
  item.set_category(ITEM_CATEGORY_ETC);
  return item;
}

MapData SnailMap() {
  MapData map;
  map.set_name("Snail Field");
  Spawn* snail = map.add_spawns();
  snail->set_mob("snail");
  snail->set_count(6);
  return map;
}

// A mob no starting character can survive: far past what their bare DEF can
// cancel, and too much HP to chew through before it lands enough hits.
Mob OgreMob() {
  Mob mob;
  mob.set_name("Ogre");
  mob.set_level(1);
  mob.set_max_hp(1000000);
  mob.set_attack(200);
  mob.set_exp(3);
  return mob;
}

MapData OgreMap() {
  MapData map;
  map.set_name("Ogre Field");
  Spawn* ogre = map.add_spawns();
  ogre->set_mob("ogre");
  ogre->set_count(1);
  return map;
}

MapData HomeMap() {
  MapData map;
  map.set_name("Maple Island");
  return map;
}

void EquipSword(GameState& state) {
  EquipPrototype sword;
  sword.set_name("Sword");
  sword.set_equip_type(EQUIP_TYPE_ONE_HANDED_SWORD);
  sword.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  sword.set_attack_speed(ATTACK_SPEED_AVERAGE);
  sword.mutable_base_stats()->set_attack(100);
  sword.mutable_base_stats()->set_magic_attack(100);
  state.character.PickUp(std::make_unique<EquipInstance>(sword));
  state.character.Equip(0);
}

// A character standing on the snail field with a sword in hand.
std::unique_ptr<GameState> SnailFarmer() {
  std::unique_ptr<GameState> state = std::make_unique<GameState>(
      std::map<std::string, EquipPrototype>{}, std::map<std::string, Scroll>{},
      std::map<std::string, ItemPrototype>{
          {"green_snail_shell", GreenSnailShell()}},
      std::map<std::string, Mob>{{"snail", SnailMob()}},
      std::map<std::string, MapData>{{"field", SnailMap()},
                                     {kHomeMap, HomeMap()}});
  state->current_map = "field";
  EquipSword(*state);
  return state;
}

// --- the absence itself ---

TEST(AbsenceSecondsTest, MeasuresTheGapAndRefusesTheRest) {
  EXPECT_EQ(AbsenceSeconds(1000, 4600), 3600.0);
  EXPECT_EQ(AbsenceSeconds(0, 4600), 0.0) << "a save with no stamp";
  EXPECT_EQ(AbsenceSeconds(4600, 1000), 0.0) << "a clock gone backwards";
}

// --- what an absence pays ---

TEST(OfflineTest, PaysForTimeAway) {
  std::unique_ptr<GameState> state = SnailFarmer();

  OfflineReport report = ApplyOfflineProgress(*state, 3600.0);

  EXPECT_TRUE(report.farmed);
  EXPECT_EQ(report.map_name, "Snail Field");
  EXPECT_GT(report.kills, 0);
  EXPECT_GT(report.rewards.exp, 0);
  EXPECT_GT(report.rewards.meso, 0);
  ASSERT_EQ(report.rewards.items.size(), 1u);
  EXPECT_EQ(report.rewards.items[0].name, "Green Snail Shell");
  EXPECT_GT(report.end_level, report.start_level);
  EXPECT_EQ(state->character.proto().level(), report.end_level);
  EXPECT_FALSE(report.died);
  EXPECT_DOUBLE_EQ(report.seconds, 3600.0);
  EXPECT_DOUBLE_EQ(report.absence, 3600.0);
}

// An absence inside the sample is not scaled at all: the whole of it is
// stepped, so what it pays is exactly what the fight did.
TEST(OfflineTest, AShortAbsenceIsSteppedInFull) {
  std::unique_ptr<GameState> state = SnailFarmer();

  OfflineReport report =
      ApplyOfflineProgress(*state, kOfflineSampleSeconds / 2.0);

  EXPECT_GT(report.kills, 0);
  EXPECT_NEAR(report.seconds, kOfflineSampleSeconds / 2.0, kOfflineStepSeconds);
}

// The rate is frozen at the level the player logged off at, so twice the
// absence is twice the kills however many levels they climbed on the way.
TEST(OfflineTest, TwiceTheAbsencePaysTwiceTheKills) {
  std::unique_ptr<GameState> once = SnailFarmer();
  std::unique_ptr<GameState> twice = SnailFarmer();

  int64_t hour = ApplyOfflineProgress(*once, 3600.0).kills;
  int64_t two_hours = ApplyOfflineProgress(*twice, 7200.0).kills;

  EXPECT_NEAR(static_cast<double>(two_hours), 2.0 * hour, 0.01 * hour);
}

TEST(OfflineTest, NothingToFarmPaysNothing) {
  std::unique_ptr<GameState> state = SnailFarmer();
  state->current_map = "";

  OfflineReport report = ApplyOfflineProgress(*state, 3600.0);

  EXPECT_FALSE(report.farmed);
  EXPECT_EQ(report.kills, 0);
  EXPECT_EQ(report.rewards.exp, 0);
}

TEST(OfflineTest, NoTimeAwayPaysNothing) {
  std::unique_ptr<GameState> state = SnailFarmer();

  OfflineReport report = ApplyOfflineProgress(*state, 0.0);

  EXPECT_FALSE(report.farmed);
  EXPECT_EQ(report.kills, 0);
}

// --- death ---

// A map that kills the character stops the absence there: they are paid for
// what they farmed before falling and are home when they come back.
TEST(OfflineTest, DyingCutsTheAbsenceShortAndSendsThePlayerHome) {
  GameState state(std::map<std::string, EquipPrototype>{},
                  std::map<std::string, Scroll>{},
                  std::map<std::string, ItemPrototype>{},
                  std::map<std::string, Mob>{{"ogre", OgreMob()}},
                  std::map<std::string, MapData>{{"field", OgreMap()},
                                                 {kHomeMap, HomeMap()}});
  state.current_map = "field";
  EquipSword(state);

  OfflineReport report = ApplyOfflineProgress(state, 3600.0);

  EXPECT_TRUE(report.died);
  EXPECT_LT(report.seconds, 3600.0);
  EXPECT_DOUBLE_EQ(report.absence, 3600.0)
      << "the whole absence, however far into it the fall came";
  EXPECT_EQ(state.current_map, kHomeMap);
}

// The map nobody can hold forever: a character wins every fight on it but
// loses a little more of their pool between beats than comes back. No sample
// can watch that to the end, so the trough it fell to over the sample projects
// the moment it runs out -- and the absence is paid only up to there.
TEST(OfflineTest, ASlowBleedIsProjectedForwardToTheFall) {
  Mob boar;
  boar.set_name("Boar");
  boar.set_level(10);
  boar.set_max_hp(2000);
  boar.set_exp(3);
  boar.set_attack(40);
  MapData field;
  field.set_name("Boar Field");
  Spawn* spawn = field.add_spawns();
  spawn->set_mob("boar");
  spawn->set_count(6);

  GameState state(
      std::map<std::string, EquipPrototype>{}, std::map<std::string, Scroll>{},
      std::map<std::string, ItemPrototype>{},
      std::map<std::string, Mob>{{"boar", boar}},
      std::map<std::string, MapData>{{"field", field}, {kHomeMap, HomeMap()}});
  state.current_map = "field";
  EquipSword(state);
  while (state.character.proto().level() < 20) {
    state.character.LevelUp();
  }

  OfflineReport report = ApplyOfflineProgress(state, 36000.0);

  EXPECT_TRUE(report.died);
  EXPECT_GT(report.seconds, kOfflineSampleSeconds)
      << "the sample itself was survived; the fall is the projection's";
  EXPECT_LT(report.seconds, 36000.0);
  EXPECT_EQ(state.current_map, kHomeMap);
  EXPECT_GT(report.kills, 0) << "what was farmed before the fall stands";
}

TEST(OfflineTest, ASurvivableMapDoesNotSendThePlayerHome) {
  std::unique_ptr<GameState> state = SnailFarmer();

  ApplyOfflineProgress(*state, 36000.0);

  EXPECT_EQ(state->current_map, "field");
}

}  // namespace
}  // namespace ms
