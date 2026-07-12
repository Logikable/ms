#include "src/farming.h"

#include <gtest/gtest.h>

#include <memory>

#include "src/equip_instance.h"
#include "src/game_state.h"
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

// The snail's drop item, as loaded into GameState.items.
ItemPrototype GreenSnailShell() {
  ItemPrototype item;
  item.set_name("Green Snail Shell");
  item.set_category(ITEM_CATEGORY_ETC);
  return item;
}

// A map of just snails with plenty of spawn slots.
MapData OneSnailMap() {
  MapData map;
  map.set_name("Snail Field");
  map.add_mobs("snail");
  map.set_spawn_count(6);
  return map;
}

// Equips a one-handed sword (att 100) on the state's character.
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

TEST(AdvanceFarmingTest, GrantsExpWhileFarming) {
  GameState state({}, {}, {}, {{"snail", SnailMob()}},
                  {{"field", OneSnailMap()}});
  state.current_map = "field";
  EquipSword(state);

  AdvanceFarming(state, 100000.0);  // many kills -> several level-ups
  EXPECT_GT(state.character.proto().level(), 2);
}

TEST(AdvanceFarmingTest, AccruesDropsWhileFarming) {
  GameState state({}, {}, {{"green_snail_shell", GreenSnailShell()}},
                  {{"snail", SnailMob()}}, {{"field", OneSnailMap()}});
  state.current_map = "field";
  EquipSword(state);

  AdvanceFarming(state, 100000.0);
  ASSERT_FALSE(state.character.stackables(ITEM_CATEGORY_ETC).empty());
  EXPECT_EQ(state.character.stackables(ITEM_CATEGORY_ETC)[0].name(),
            "Green Snail Shell");
}

TEST(AdvanceFarmingTest, AccruesMesoWhileFarming) {
  GameState state({}, {}, {}, {{"snail", SnailMob()}},
                  {{"field", OneSnailMap()}});
  state.current_map = "field";
  EquipSword(state);

  AdvanceFarming(state, 100000.0);
  EXPECT_GT(state.character.meso(), 0);
}

TEST(AdvanceFarmingTest, SkipsFarmingWithoutWeapon) {
  GameState state({}, {}, {}, {{"snail", SnailMob()}},
                  {{"field", OneSnailMap()}});
  state.current_map = "field";

  AdvanceFarming(state, 100000.0);
  EXPECT_EQ(state.character.proto().level(), 1);
  EXPECT_EQ(state.character.proto().exp(), 0);
}

TEST(AdvanceFarmingTest, NoOpWithoutCurrentMap) {
  GameState state({}, {}, {}, {{"snail", SnailMob()}},
                  {{"field", OneSnailMap()}});
  EquipSword(state);  // current_map left empty

  AdvanceFarming(state, 100000.0);
  EXPECT_EQ(state.character.proto().level(), 1);
  EXPECT_EQ(state.character.proto().exp(), 0);
}

}  // namespace
}  // namespace ms
