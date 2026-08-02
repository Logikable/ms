#include "src/combat/combat.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>

#include "src/combat/fight.h"
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
  MapData::Spawn* snail = map.add_spawns();
  snail->set_mob("snail");
  snail->set_count(6);
  return map;
}

// Equips a one-handed sword (100 weapon and magic attack) on the character.
void EquipSword(GameState& state) {
  EquipPrototype sword;
  sword.set_name("Sword");
  sword.set_equip_type(EQUIP_TYPE_ONE_HANDED_SWORD);
  sword.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  sword.set_attack_speed(ATTACK_SPEED_AVERAGE);
  // Both halves, so the swing lands whatever job the starting character
  // happens to be -- kStartingJob is a testing knob, not something the
  // encounter math should depend on.
  sword.mutable_base_stats()->set_attack(100);
  sword.mutable_base_stats()->set_magic_attack(100);
  state.character.PickUp(std::make_unique<EquipInstance>(sword));
  state.character.Equip(0);
}

// Levels the character to `level`. Combat is paced by level (see
// GameSpeedFactor), so a test that means to hold the pace still has to say
// which level it is holding it at.
void LevelTo(GameState& state, int level) {
  while (state.character.proto().level() < level) {
    state.character.LevelUp();
  }
}

// Farms for `seconds` of game time. A single call can advance at most one
// swing, so rewards only accrue over a loop -- as they do under the TUI's
// ticker.
void Farm(GameState& state, double seconds) {
  CombatSim sim;
  for (double elapsed = 0.0; elapsed < seconds; elapsed += 1.0) {
    AdvanceCombat(state, sim, 1.0);
  }
}

TEST(AdvanceCombatTest, GrantsExpWhileFarming) {
  GameState state({}, {}, {}, {{"snail", SnailMob()}},
                  {{"field", OneSnailMap()}});
  state.current_map = "field";
  EquipSword(state);

  Farm(state, 20000.0);  // many kills -> several level-ups
  EXPECT_GT(state.character.proto().level(), 2);
}

TEST(AdvanceCombatTest, AccruesDropsWhileFarming) {
  GameState state({}, {}, {{"green_snail_shell", GreenSnailShell()}},
                  {{"snail", SnailMob()}}, {{"field", OneSnailMap()}});
  state.current_map = "field";
  EquipSword(state);

  Farm(state, 20000.0);
  ASSERT_FALSE(state.character.stackables(ITEM_CATEGORY_ETC).empty());
  EXPECT_EQ(state.character.stackables(ITEM_CATEGORY_ETC)[0].name(),
            "Green Snail Shell");
}

TEST(AdvanceCombatTest, AccruesMesoWhileFarming) {
  GameState state({}, {}, {}, {{"snail", SnailMob()}},
                  {{"field", OneSnailMap()}});
  state.current_map = "field";
  EquipSword(state);

  Farm(state, 20000.0);
  EXPECT_GT(state.character.meso(), 0);
}

TEST(AdvanceCombatTest, SkipsFarmingWithoutWeapon) {
  GameState state({}, {}, {}, {{"snail", SnailMob()}},
                  {{"field", OneSnailMap()}});
  state.current_map = "field";

  int start_level = state.character.proto().level();
  Farm(state, 20000.0);
  EXPECT_EQ(state.character.proto().level(), start_level);
  EXPECT_EQ(state.character.proto().exp(), 0);
}

TEST(AdvanceCombatTest, NoOpWithoutCurrentMap) {
  GameState state({}, {}, {}, {{"snail", SnailMob()}},
                  {{"field", OneSnailMap()}});
  EquipSword(state);  // current_map left empty

  int start_level = state.character.proto().level();
  Farm(state, 20000.0);
  EXPECT_EQ(state.character.proto().level(), start_level);
  EXPECT_EQ(state.character.proto().exp(), 0);
}

// --- the level-banded pace ---

// Farms `seconds` at `level` and returns the EXP it paid. The character is
// levelled by hand first so the band under test is the one in force.
int64_t ExpFarmedAt(int level, double seconds) {
  GameState state({}, {}, {}, {{"snail", SnailMob()}},
                  {{"field", OneSnailMap()}});
  state.current_map = "field";
  LevelTo(state, level);
  EquipSword(state);
  int64_t before = state.character.proto().exp();
  Farm(state, seconds);
  return state.character.proto().exp() - before;
}

// The same fight pays less per second the higher the band, because the fight
// itself runs slower: these snails die in one hit at either level, so nothing
// but the pace has changed between the two.
TEST(AdvanceCombatTest, TheSameFightPaysLessAsTheGameSlowsDown) {
  int64_t at_9 = ExpFarmedAt(9, 600.0);
  int64_t at_10 = ExpFarmedAt(10, 600.0);
  int64_t at_140 = ExpFarmedAt(140, 600.0);
  ASSERT_GT(at_140, 0) << "the slowest band still has to pay something";
  EXPECT_GT(at_9, at_10) << "1x band vs 2x band";
  EXPECT_GT(at_10, at_140) << "2x band vs 10x band";
}

// The workbench's standing EXP bonus, which is what makes the level-gated
// features reachable in a sitting. It pays only EXP: the same corpses, the
// same drops, the same meso as the same fight without it.
//
// Both characters are pinned at the top level first. Otherwise the bonus
// changes what it is measuring: EXP levels the character, levelling slows the
// game down, and the boosted character ends up killing FEWER mobs over the
// same wall time. At 140 the pacing band is already the last one and a farm
// this short cannot level them, so the multiplier is all that differs.
TEST(AdvanceCombatTest, TheExpMultiplierPaysExpAndNothingElse) {
  GameState plain({}, {}, {{"green_snail_shell", GreenSnailShell()}},
                  {{"snail", SnailMob()}}, {{"field", OneSnailMap()}});
  plain.current_map = "field";
  LevelTo(plain, 140);
  EquipSword(plain);
  Farm(plain, 600.0);

  GameState boosted({}, {}, {{"green_snail_shell", GreenSnailShell()}},
                    {{"snail", SnailMob()}}, {{"field", OneSnailMap()}});
  boosted.current_map = "field";
  LevelTo(boosted, 140);
  boosted.exp_multiplier = 5;
  EquipSword(boosted);
  Farm(boosted, 600.0);

  ASSERT_EQ(boosted.character.proto().level(), 140) << "no band change";
  ASSERT_GT(plain.character.proto().exp(), 0);
  EXPECT_EQ(boosted.character.proto().exp(), 5 * plain.character.proto().exp());
  ASSERT_FALSE(plain.character.stackables(ITEM_CATEGORY_ETC).empty());
  ASSERT_FALSE(boosted.character.stackables(ITEM_CATEGORY_ETC).empty());
  EXPECT_EQ(boosted.character.stackables(ITEM_CATEGORY_ETC)[0].count(),
            plain.character.stackables(ITEM_CATEGORY_ETC)[0].count());
  EXPECT_EQ(boosted.character.meso(), plain.character.meso());
}

}  // namespace
}  // namespace ms
