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
#include "src/protos/skill.pb.h"

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
  MapData::Spawn* snail = map.add_spawns();
  snail->set_mob("snail");
  snail->set_count(2);
  MapData::Spawn* blue = map.add_spawns();
  blue->set_mob("blue_snail");
  blue->set_count(4);
  return map;
}

void EquipSwordAt(GameState& state, AttackSpeed speed) {
  EquipPrototype sword;
  sword.set_name("Sword");
  sword.set_equip_type(EQUIP_TYPE_ONE_HANDED_SWORD);
  sword.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  sword.set_attack_speed(speed);
  sword.mutable_base_stats()->set_attack(100);
  state.character.PickUp(std::make_unique<EquipInstance>(sword));
  state.character.Equip(0);
}

void EquipSword(GameState& state) {
  EquipSwordAt(state, ATTACK_SPEED_AVERAGE);
}

// A passive that adds `stages` of attack speed, flat at every level.
Skill SpeedPassive(int stages) {
  Skill skill;
  skill.set_name("Haste");
  skill.set_kind(SKILL_KIND_PASSIVE);
  skill.set_job_advancement(JOB_ADVANCEMENT_ARCHER);
  skill.set_max_level(1);
  skill.mutable_base()->set_attack_speed(stages);
  return skill;
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
  EXPECT_EQ(params.types[0].simultaneous, 2);  // the snail's own spawn count
  EXPECT_GT(params.swing_seconds, 0.0);
  EXPECT_DOUBLE_EQ(params.respawn_seconds,
                   kRespawnIntervalSeconds * kGameSpeedFactor);
  // With no attack skill learned the bare poke is the only option.
  ASSERT_EQ(params.attacks.size(), 1u);
  EXPECT_EQ(params.attacks[0].name, "Attack");
  EXPECT_EQ(params.attacks[0].max_enemies, 1);
  ASSERT_EQ(params.attacks[0].damage_per_hit.size(), params.types.size());
  EXPECT_GT(params.attacks[0].damage_per_hit[0], 0.0);
}

TEST(ComputeCombatParamsTest, LearnedAttackSkillsJoinTheBarePokeAsOptions) {
  Skill slash;
  slash.set_name("Slash Blast");
  slash.set_kind(SKILL_KIND_ATTACK);
  slash.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  slash.set_max_level(20);
  slash.set_max_enemies(6);
  slash.mutable_base()->set_skill_pct(1.83);
  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}}, {{"slash_blast", slash}});
  state.current_map = "field";
  EquipSword(state);
  // The starting level-15 Warrior has stage-1 SP to spend.
  ASSERT_TRUE(state.character.LearnSkill(slash, 1));

  // The poke stays on the list; the skill joins it, and the fight chooses.
  CombatParams params = ComputeCombatParams(state);
  ASSERT_EQ(params.attacks.size(), 2u);
  EXPECT_EQ(params.attacks[0].name, "Attack");
  EXPECT_EQ(params.attacks[1].name, "Slash Blast");
  EXPECT_EQ(params.attacks[1].max_enemies, 6);
  // 183% against the poke's 100%, on the same mob.
  EXPECT_GT(params.attacks[1].damage_per_hit[0],
            params.attacks[0].damage_per_hit[0]);
}

TEST(ComputeCombatParamsTest, AttackSpeedPassiveShortensTheSwing) {
  Skill haste = SpeedPassive(1);
  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}}, {{"haste", haste}});
  state.current_map = "field";
  EquipSword(state);  // AVERAGE (stage 4)
  double slow = ComputeCombatParams(state).swing_seconds;
  ASSERT_TRUE(state.character.LearnSkill(haste, 1));
  double fast = ComputeCombatParams(state).swing_seconds;
  EXPECT_LT(fast, slow);  // +1 stage swings sooner
}

TEST(ComputeCombatParamsTest, AttackSpeedIsCappedAtTheFastestTier) {
  // A wildly oversized bonus can't push the swing past the top tier: the same
  // character with a plain FASTEST_3 weapon swings just as fast.
  Skill haste = SpeedPassive(100);
  GameState fast_state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                       {{"field", TwoSnailMap()}}, {{"haste", haste}});
  fast_state.current_map = "field";
  EquipSwordAt(fast_state, ATTACK_SPEED_AVERAGE);
  ASSERT_TRUE(fast_state.character.LearnSkill(haste, 1));

  GameState cap_state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                      {{"field", TwoSnailMap()}});
  cap_state.current_map = "field";
  EquipSwordAt(cap_state, ATTACK_SPEED_FASTEST_3);

  EXPECT_DOUBLE_EQ(ComputeCombatParams(fast_state).swing_seconds,
                   ComputeCombatParams(cap_state).swing_seconds);
}

}  // namespace
}  // namespace ms
