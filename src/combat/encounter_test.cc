#include "src/combat/encounter.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "src/combat/constants.h"
#include "src/equip_instance.h"
#include "src/game_state.h"
#include "src/protos/equip.pb.h"
#include "src/protos/map.pb.h"
#include "src/protos/mob.pb.h"

namespace ms {
namespace {

Mob MakeMob(const std::string& name, int max_hp) {
  Mob mob;
  mob.set_name(name);
  mob.set_max_hp(max_hp);
  return mob;
}

MapData TwoSnailMap() {
  MapData map;
  map.set_name("Snail Field");
  map.add_mobs("snail");
  map.add_mobs("blue_snail");
  map.set_spawn_count(6);
  return map;
}

void EquipSword(GameState& state) {
  EquipPrototype sword;
  sword.set_name("Sword");
  sword.set_equip_type(EQUIP_TYPE_ONE_HANDED_SWORD);
  sword.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  sword.set_attack_speed(ATTACK_SPEED_AVERAGE);
  sword.mutable_base_stats()->set_attack(100);
  state.character.PickUp(std::make_unique<EquipInstance>(sword));
  state.character.Equip(0);
}

TEST(ComputeCombatParamsTest, InactiveWithoutCurrentMap) {
  GameState state({}, {}, {}, {}, {});
  EquipSword(state);
  EXPECT_FALSE(ComputeCombatParams(state).active);
}

TEST(ComputeCombatParamsTest, InactiveWithoutWeapon) {
  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}});
  state.current_map = "field";
  EXPECT_FALSE(ComputeCombatParams(state).active);
}

TEST(ComputeCombatParamsTest, ReportsTypesSimultaneousAndDurations) {
  GameState state({}, {}, {},
                  {{"snail", MakeMob("Snail", 15)},
                   {"blue_snail", MakeMob("Blue Snail", 20)}},
                  {{"field", TwoSnailMap()}});
  state.current_map = "field";
  EquipSword(state);

  CombatParams params = ComputeCombatParams(state);
  ASSERT_TRUE(params.active);
  ASSERT_EQ(params.types.size(), 2u);
  ASSERT_NE(params.types[0].mob, nullptr);
  EXPECT_EQ(params.types[0].mob->name(), "Snail");
  EXPECT_EQ(params.types[0].mob->max_hp(), 15);
  EXPECT_EQ(params.types[0].simultaneous, 3);  // 6 slots / 2 types
  EXPECT_GT(params.types[0].damage_per_hit, 0.0);
  EXPECT_GT(params.swing_seconds, 0.0);
  EXPECT_DOUBLE_EQ(params.respawn_seconds,
                   kRespawnIntervalSeconds * kGameSpeedFactor);
}

}  // namespace
}  // namespace ms
