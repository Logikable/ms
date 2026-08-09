#include "src/combat/encounter.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "src/character/character_stats.h"
#include "src/character/progression.h"
#include "src/combat/constants.h"
#include "src/combat/damage.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
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

// A mob that swings back: attack and level are what the damage-taken formula
// reads off it.
Mob MakeAttacker(const std::string& name, int max_hp, int attack, int level) {
  Mob mob = MakeMob(name, max_hp);
  mob.set_attack(attack);
  mob.set_level(level);
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
  // Both halves, so the swing lands whatever job the starting character
  // happens to be -- kStartingJob is a testing knob, not something the
  // encounter math should depend on.
  sword.mutable_base_stats()->set_attack(100);
  sword.mutable_base_stats()->set_magic_attack(100);
  state.character.PickUp(std::make_unique<EquipInstance>(sword));
  state.character.Equip(0);
}

void EquipSword(GameState& state) {
  EquipSwordAt(state, ATTACK_SPEED_AVERAGE);
}

// The same sword with armour bolted onto it, so a test can raise the
// character's DEF without a second slot to fill.
void EquipArmouredSword(GameState& state, int def) {
  EquipPrototype sword;
  sword.set_name("Padded Sword");
  sword.set_equip_type(EQUIP_TYPE_ONE_HANDED_SWORD);
  sword.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  sword.set_attack_speed(ATTACK_SPEED_AVERAGE);
  sword.mutable_base_stats()->set_attack(100);
  sword.mutable_base_stats()->set_magic_attack(100);
  sword.mutable_base_stats()->set_def(def);
  state.character.PickUp(std::make_unique<EquipInstance>(sword));
  state.character.Equip(0);
}

void EquipClaw(GameState& state) {
  EquipPrototype claw;
  claw.set_name("Garnier");
  claw.set_equip_type(EQUIP_TYPE_CLAW);
  claw.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  claw.set_attack_speed(ATTACK_SPEED_AVERAGE);
  claw.mutable_base_stats()->set_attack(100);
  state.character.PickUp(std::make_unique<EquipInstance>(claw));
  state.character.Equip(0);
}

// A passive that adds `stages` of attack speed, flat at every level. Filed
// under the warrior's book because these tests are about the swing clock, not
// about whose book the passive came out of -- and a character can only spend
// on their own.
Skill SpeedPassive(int stages) {
  Skill skill;
  skill.set_name("Haste");
  skill.set_kind(SKILL_KIND_PASSIVE);
  skill.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
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
  EXPECT_GT(params.attacks.front().swing_seconds, 0.0);
  EXPECT_DOUBLE_EQ(params.respawn_seconds,
                   kRespawnIntervalSeconds *
                       GameSpeedFactor(state.character.proto().level()));
  // With no attack skill learned the bare poke is the only option.
  ASSERT_EQ(params.attacks.size(), 1u);
  EXPECT_EQ(params.attacks[0].name, "Attack");
  EXPECT_EQ(params.attacks[0].max_enemies, 1);
  ASSERT_EQ(params.attacks[0].damage_per_hit.size(), params.types.size());
  EXPECT_GT(params.attacks[0].damage_per_hit[0], 0.0);
}

// Levels the character up until its first-job SP pool can pay for `points`.
// These tests are about combat, not about whatever job or level the starting
// character happens to carry, so they buy their own SP.
//
// Levelling is not free of side effects: the pace of the whole game stretches
// with the character's level (see GameSpeedFactor). Any test comparing two
// swing intervals has to buy its SP before measuring either of them, or it
// measures the pacing band rather than the thing it meant to.
void GrantFirstJobSp(GameState& state, int points, Job job = JOB_SWORDMAN) {
  // A skill belongs to one job's book and only that job can spend on it, so
  // the character takes the advancement the skill under test belongs to before
  // buying any of it. A character already advanced is left where they are.
  while (state.character.proto().job_stage() < 1) {
    if (state.character.CanAdvanceJob()) {
      state.character.AdvanceJob(job);
    } else {
      state.character.LevelUp();
    }
  }
  while (state.character.sp(1) < points) {
    state.character.LevelUp();
  }
}

TEST(ComputeCombatParamsTest, LearnedSkillsJoinTheBarePoke) {
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
  GrantFirstJobSp(state, 1);
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

// A cast joins the swings the fight can spend a turn on, but it never joins
// what they can land: the damage chain has no multiplier for a skill that
// deals none, so what it built for the cast is the bare poke's damage. Left
// there, casting would hit for a plain swing and pull a Final Attack behind
// it.
TEST(ComputeCombatParamsTest, ACastIsOfferedAsASwingButCarriesNoDamage) {
  Skill heal;
  heal.set_name("Heal");
  heal.set_kind(SKILL_KIND_ACTIVE);
  heal.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  heal.set_max_level(10);
  heal.set_base_delay_ms(600);
  heal.mutable_base()->set_heal_pct(0.10);
  heal.mutable_per_level()->set_heal_pct(0.10);
  Skill final_attack;
  final_attack.set_name("Final Attack");
  final_attack.set_kind(SKILL_KIND_PASSIVE);
  final_attack.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  final_attack.set_max_level(1);
  final_attack.mutable_base()->set_final_attack_chance(0.5);
  final_attack.mutable_base()->set_final_attack_pct(1.0);
  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}},
                  {{"heal", heal}, {"final_attack", final_attack}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 5);
  ASSERT_TRUE(state.character.LearnSkill(heal, 4));
  ASSERT_TRUE(state.character.LearnSkill(final_attack, 1));

  CombatParams params = ComputeCombatParams(state);
  ASSERT_EQ(params.attacks.size(), 2u);
  EXPECT_EQ(params.attacks[1].name, "Heal");
  // Four levels of a tenth of the pool apiece.
  EXPECT_DOUBLE_EQ(params.attacks[1].heal_fraction, 0.4);
  EXPECT_GT(params.attacks[0].damage_per_hit[0], 0.0);
  for (double damage : params.attacks[1].damage_per_hit) {
    EXPECT_DOUBLE_EQ(damage, 0.0);
  }
  EXPECT_TRUE(params.attacks[1].final_attack_damage.empty());
  EXPECT_FALSE(params.attacks[0].final_attack_damage.empty());
}

// A cast with no lever behind it would take a swing and give nothing back, so
// it is not offered at all.
TEST(ComputeCombatParamsTest, ACastWithNothingBehindItIsNoOption) {
  Skill shout;
  shout.set_name("Shout");
  shout.set_kind(SKILL_KIND_ACTIVE);
  shout.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  shout.set_max_level(10);
  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}}, {{"shout", shout}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 1);
  ASSERT_TRUE(state.character.LearnSkill(shout, 1));

  CombatParams params = ComputeCombatParams(state);
  ASSERT_EQ(params.attacks.size(), 1u);
  EXPECT_EQ(params.attacks[0].name, "Attack");
}

// GMS keys the swing delay on the skill, so two skills in the same hand can
// swing at different speeds. The weapon's say is its attack-speed stage, which
// scales both alike.
// A cooldown is a duration like any other here, so the pacing band stretches
// it with the swings it keeps the player from making.
TEST(ComputeCombatParamsTest, ACooldownStretchesWithEverythingElse) {
  Skill burst;
  burst.set_name("Burst");
  burst.set_kind(SKILL_KIND_ATTACK);
  burst.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  burst.set_max_level(20);
  burst.set_base_delay_ms(720);
  burst.set_cooldown_seconds(4.0);
  burst.mutable_base()->set_skill_pct(1.83);
  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}}, {{"burst", burst}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 1);
  ASSERT_TRUE(state.character.LearnSkill(burst, 1));

  CombatParams params = ComputeCombatParams(state);
  ASSERT_EQ(params.attacks.size(), 2u);
  EXPECT_DOUBLE_EQ(params.attacks[0].cooldown_seconds, 0.0);
  EXPECT_DOUBLE_EQ(params.attacks[1].cooldown_seconds,
                   4.0 * GameSpeedFactor(state.character.proto().level()));
}

TEST(ComputeCombatParamsTest, EachSwingTakesItsOwnSkillsTime) {
  Skill slow;
  slow.set_name("Slow Swing");
  slow.set_kind(SKILL_KIND_ATTACK);
  slow.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  slow.set_max_level(20);
  slow.set_base_delay_ms(1200);
  slow.mutable_base()->set_skill_pct(1.83);
  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}}, {{"slow_swing", slow}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 1);
  ASSERT_TRUE(state.character.LearnSkill(slow, 1));

  CombatParams params = ComputeCombatParams(state);
  ASSERT_EQ(params.attacks.size(), 2u);
  // The poke takes the default; the skill takes its own, which is longer.
  EXPECT_DOUBLE_EQ(
      params.attacks[0].swing_seconds,
      SwingIntervalSeconds(kDefaultSwingDelayMs, ATTACK_SPEED_AVERAGE) *
          GameSpeedFactor(state.character.proto().level()));
  EXPECT_DOUBLE_EQ(params.attacks[1].swing_seconds,
                   SwingIntervalSeconds(1200, ATTACK_SPEED_AVERAGE) *
                       GameSpeedFactor(state.character.proto().level()));
}

// A skill saying nothing about its animation is swung at the same pace as the
// bare poke, rather than instantly.
TEST(ComputeCombatParamsTest, ASwingWithNoDelayOfItsOwnTakesTheDefault) {
  Skill slash;
  slash.set_name("Slash Blast");
  slash.set_kind(SKILL_KIND_ATTACK);
  slash.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  slash.set_max_level(20);
  slash.mutable_base()->set_skill_pct(1.83);
  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}}, {{"slash_blast", slash}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 1);
  ASSERT_TRUE(state.character.LearnSkill(slash, 1));

  CombatParams params = ComputeCombatParams(state);
  ASSERT_EQ(params.attacks.size(), 2u);
  EXPECT_DOUBLE_EQ(params.attacks[1].swing_seconds,
                   params.attacks[0].swing_seconds);
}

// A skill that fires on its own clock is not one of the swings the fight
// chooses between -- it is a thing that also happens.
TEST(ComputeCombatParamsTest, AutoAttackSkillsLandOnTheirOwnList) {
  Skill evil_eye;
  evil_eye.set_name("Evil Eye Shock");
  evil_eye.set_kind(SKILL_KIND_AUTO_ATTACK);
  evil_eye.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  evil_eye.set_max_level(10);
  evil_eye.set_max_enemies(10);
  evil_eye.set_cast_interval_seconds(12.0);
  evil_eye.mutable_base()->set_skill_pct(1.23);
  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}}, {{"evil_eye_shock", evil_eye}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 1);
  ASSERT_TRUE(state.character.LearnSkill(evil_eye, 1));

  CombatParams params = ComputeCombatParams(state);
  EXPECT_EQ(params.attacks.size(), 1u);  // the bare poke, and nothing else
  ASSERT_EQ(params.auto_attacks.size(), 1u);
  EXPECT_EQ(params.auto_attacks[0].name, "Evil Eye Shock");
  EXPECT_EQ(params.auto_attacks[0].max_enemies, 10);
  EXPECT_GT(params.auto_attacks[0].damage_per_hit[0], 0.0);
  // Stretched by the pacing band, like every other duration here.
  EXPECT_DOUBLE_EQ(params.auto_attacks[0].interval_seconds,
                   12.0 * GameSpeedFactor(state.character.proto().level()));
}

// Final Attack follows the character's swing. A summon firing on its own clock
// is not that, so the option the fight gets for it carries none.
TEST(ComputeCombatParamsTest, OnlyOwnSwingsCarryFinalAttack) {
  Skill evil_eye;
  evil_eye.set_name("Evil Eye Shock");
  evil_eye.set_kind(SKILL_KIND_AUTO_ATTACK);
  evil_eye.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  evil_eye.set_max_level(10);
  evil_eye.set_cast_interval_seconds(12.0);
  evil_eye.mutable_base()->set_skill_pct(1.23);
  Skill final_attack;
  final_attack.set_name("Final Attack");
  final_attack.set_kind(SKILL_KIND_PASSIVE);
  final_attack.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  final_attack.set_max_level(20);
  final_attack.mutable_base()->set_final_attack_chance(0.40);
  final_attack.mutable_base()->set_final_attack_pct(1.60);
  GameState state(
      {}, {}, {}, {{"snail", MakeMob("Snail", 15)}}, {{"field", TwoSnailMap()}},
      {{"evil_eye_shock", evil_eye}, {"final_attack", final_attack}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 2);
  ASSERT_TRUE(state.character.LearnSkill(evil_eye, 1));
  ASSERT_TRUE(state.character.LearnSkill(final_attack, 1));

  CombatParams params = ComputeCombatParams(state);
  ASSERT_EQ(params.attacks.size(), 1u);
  ASSERT_EQ(params.auto_attacks.size(), 1u);
  ASSERT_EQ(params.attacks[0].final_attack_damage.size(), params.types.size());
  EXPECT_GT(params.attacks[0].final_attack_damage[0], 0.0);
  EXPECT_TRUE(params.auto_attacks[0].final_attack_damage.empty());
}

// A Final Attack that names a tag follows only the swings carrying it. The
// bare poke carries none, so it never sets one off.
TEST(ComputeCombatParamsTest, ATaggedFinalAttackFollowsOnlyThatTagsSwings) {
  Skill flame;
  flame.set_name("Flame Orb");
  flame.set_kind(SKILL_KIND_ATTACK);
  flame.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  flame.set_max_level(10);
  flame.add_tags(SKILL_TAG_FIRE);
  flame.mutable_base()->set_skill_pct(1.48);
  Skill cold;
  cold.set_name("Cold Beam");
  cold.set_kind(SKILL_KIND_ATTACK);
  cold.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  cold.set_max_level(10);
  cold.mutable_base()->set_skill_pct(1.48);
  Skill ignite;
  ignite.set_name("Ignite");
  ignite.set_kind(SKILL_KIND_PASSIVE);
  ignite.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  ignite.set_max_level(10);
  ignite.set_follows_skill_tag(SKILL_TAG_FIRE);
  ignite.mutable_base()->set_final_attack_chance(0.50);
  ignite.mutable_base()->set_final_attack_pct(1.20);
  GameState state(
      {}, {}, {}, {{"snail", MakeMob("Snail", 15)}}, {{"field", TwoSnailMap()}},
      {{"flame_orb", flame}, {"cold_beam", cold}, {"ignite", ignite}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 3);
  ASSERT_TRUE(state.character.LearnSkill(flame, 1));
  ASSERT_TRUE(state.character.LearnSkill(cold, 1));
  ASSERT_TRUE(state.character.LearnSkill(ignite, 1));

  CombatParams params = ComputeCombatParams(state);
  ASSERT_EQ(params.attacks.size(), 3u);
  for (const AttackOption& attack : params.attacks) {
    if (attack.name == "Flame Orb") {
      EXPECT_FALSE(attack.final_attack_damage.empty()) << attack.name;
    } else {
      EXPECT_TRUE(attack.final_attack_damage.empty()) << attack.name;
    }
  }
}

// Without the skill there is nothing to follow the swing, and the fight is
// told so rather than being handed a column of zeroes to add.
TEST(ComputeCombatParamsTest, NoFinalAttackLeavesTheSwingCarryingNone) {
  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}});
  state.current_map = "field";
  EquipSword(state);

  CombatParams params = ComputeCombatParams(state);
  ASSERT_EQ(params.attacks.size(), 1u);
  EXPECT_TRUE(params.attacks[0].final_attack_damage.empty());
}

// The interval is what makes the skill fire at all, so a skill without one is
// taken as not firing rather than as firing every step.
TEST(ComputeCombatParamsTest, AnAutoAttackNeedsAnInterval) {
  Skill evil_eye;
  evil_eye.set_name("Evil Eye Shock");
  evil_eye.set_kind(SKILL_KIND_AUTO_ATTACK);
  evil_eye.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  evil_eye.set_max_level(10);
  evil_eye.mutable_base()->set_skill_pct(1.23);
  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}}, {{"evil_eye_shock", evil_eye}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 1);
  ASSERT_TRUE(state.character.LearnSkill(evil_eye, 1));

  EXPECT_TRUE(ComputeCombatParams(state).auto_attacks.empty());
}

// Double Stab and Lucky Seven are the same damage over the same reach; the
// weapon in hand is the only thing that tells them apart, so an attack the
// weapon cannot swing must not reach the fight as an option.
TEST(ComputeCombatParamsTest, AttacksTheWeaponCannotSwingAreNotOptions) {
  Skill lucky_seven;
  lucky_seven.set_name("Lucky Seven");
  lucky_seven.set_kind(SKILL_KIND_ATTACK);
  lucky_seven.set_job_advancement(JOB_ADVANCEMENT_ROGUE);
  lucky_seven.set_max_level(20);
  lucky_seven.add_required_equip_type(EQUIP_TYPE_CLAW);
  lucky_seven.mutable_base()->set_skill_pct(0.72);
  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}}, {{"lucky_seven", lucky_seven}});
  state.current_map = "field";
  EquipSword(state);  // a one-handed sword, not a claw
  GrantFirstJobSp(state, 1, JOB_ROGUE);
  ASSERT_TRUE(state.character.LearnSkill(lucky_seven, 1));

  // Learned, and still not on the list -- but the poke always is, so the
  // character is never left with nothing to swing.
  CombatParams params = ComputeCombatParams(state);
  ASSERT_EQ(params.attacks.size(), 1u);
  EXPECT_EQ(params.attacks[0].name, "Attack");
}

TEST(ComputeCombatParamsTest, AttacksTheWeaponCanSwingAreOptions) {
  Skill lucky_seven;
  lucky_seven.set_name("Lucky Seven");
  lucky_seven.set_kind(SKILL_KIND_ATTACK);
  lucky_seven.set_job_advancement(JOB_ADVANCEMENT_ROGUE);
  lucky_seven.set_max_level(20);
  lucky_seven.add_required_equip_type(EQUIP_TYPE_CLAW);
  lucky_seven.mutable_base()->set_skill_pct(0.72);
  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}}, {{"lucky_seven", lucky_seven}});
  state.current_map = "field";
  EquipClaw(state);
  GrantFirstJobSp(state, 1, JOB_ROGUE);
  ASSERT_TRUE(state.character.LearnSkill(lucky_seven, 1));

  CombatParams params = ComputeCombatParams(state);
  ASSERT_EQ(params.attacks.size(), 2u);
  EXPECT_EQ(params.attacks[1].name, "Lucky Seven");
}

// A passive that grants a primary stat has no lever of its own in the damage
// chain -- it reaches the swing only by being summed into the character's
// equipment stats, which is what TotalEquipStats exists to do.
TEST(ComputeCombatParamsTest, StatGrantingPassivesReachTheSwing) {
  Skill nimble;
  nimble.set_name("Nimble Body");
  nimble.set_kind(SKILL_KIND_PASSIVE);
  nimble.set_job_advancement(JOB_ADVANCEMENT_ROGUE);
  nimble.set_max_level(20);
  nimble.mutable_base()->set_luk(1);
  nimble.mutable_per_level()->set_luk(1);
  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}}, {{"nimble_body", nimble}});
  state.current_map = "field";
  EquipSword(state);
  // LUK has to be a stat this job actually swings on, so the character becomes
  // a rogue rather than whatever kStartingJob happens to be.
  state.character.AdvanceJob(JOB_ROGUE);

  double before = ComputeCombatParams(state).attacks[0].damage_per_hit[0];
  GrantFirstJobSp(state, 20);
  ASSERT_TRUE(state.character.LearnSkill(nimble, 20));
  double after = ComputeCombatParams(state).attacks[0].damage_per_hit[0];
  EXPECT_GT(after, before);  // 20 LUK on a rogue's main stat
}

TEST(ComputeCombatParamsTest, AttackSpeedPassiveShortensTheSwing) {
  Skill haste = SpeedPassive(1);
  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}}, {{"haste", haste}});
  state.current_map = "field";
  EquipSword(state);  // AVERAGE (stage 4)
  // Bought up front so both readings are taken at the same level, and so at
  // the same pace: what is under test is the skill, not the band.
  GrantFirstJobSp(state, 1);
  double slow = ComputeCombatParams(state).attacks.front().swing_seconds;
  ASSERT_TRUE(state.character.LearnSkill(haste, 1));
  double fast = ComputeCombatParams(state).attacks.front().swing_seconds;
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
  GrantFirstJobSp(fast_state, 1);
  ASSERT_TRUE(fast_state.character.LearnSkill(haste, 1));

  GameState cap_state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                      {{"field", TwoSnailMap()}});
  cap_state.current_map = "field";
  EquipSwordAt(cap_state, ATTACK_SPEED_FASTEST_3);
  // Levelled to match the other character, whose SP was bought with levels.
  // Two characters at different levels run at different paces, and the
  // comparison is about the attack-speed cap.
  GrantFirstJobSp(cap_state, 1);

  EXPECT_DOUBLE_EQ(
      ComputeCombatParams(fast_state).attacks.front().swing_seconds,
      ComputeCombatParams(cap_state).attacks.front().swing_seconds);
}

// The pacing band, read straight off the params. The same character with the
// same weapon on the same map swings and respawns slower once they cross into
// a slower band -- that is the whole of the level-driven pacing.
TEST(ComputeCombatParamsTest, TheEncounterStretchesAsTheCharacterLevels) {
  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}});
  state.current_map = "field";
  EquipSword(state);
  ASSERT_EQ(state.character.proto().level(), 1);

  CombatParams early = ComputeCombatParams(state);
  while (state.character.proto().level() < 10) {
    state.character.LevelUp();
  }
  CombatParams later = ComputeCombatParams(state);

  // Whatever the two bands are tuned to, crossing one has to stretch both
  // durations by the same amount -- they are the same clock.
  double stretch = GameSpeedFactor(10) / GameSpeedFactor(1);
  ASSERT_GT(stretch, 1.0) << "the bands either side of 10 must differ";
  EXPECT_DOUBLE_EQ(later.attacks.front().swing_seconds,
                   stretch * early.attacks.front().swing_seconds);
  EXPECT_DOUBLE_EQ(later.respawn_seconds, stretch * early.respawn_seconds);
  EXPECT_DOUBLE_EQ(later.hit_seconds, stretch * early.hit_seconds);
}

TEST(ComputeCombatParamsTest, ReportsThePlayersPoolAndHowOftenItIsHit) {
  GameState state({}, {}, {}, {{"snail", MakeAttacker("Snail", 15, 20, 1)}},
                  {{"field", TwoSnailMap()}});
  state.current_map = "field";
  EquipSword(state);

  CombatParams params = ComputeCombatParams(state);
  ASSERT_TRUE(params.active);
  EXPECT_EQ(params.max_player_hp,
            DerivedStatsFor(state.character, state.skills).max_hp);
  EXPECT_GT(params.hit_seconds, 0.0);
  // The only healing there is, so a real encounter has to carry it or a map
  // becomes survivable only by clearing it.
  EXPECT_GT(params.beat_heal_fraction, 0.0);
  EXPECT_LE(params.beat_heal_fraction, 1.0);
}

TEST(ComputeCombatParamsTest, EachTypeCarriesWhatItsHitsDoToThePlayer) {
  GameState state({}, {}, {},
                  {{"snail", MakeAttacker("Snail", 15, 20, 1)},
                   {"blue_snail", MakeAttacker("Blue Snail", 20, 200, 1)}},
                  {{"field", TwoSnailMap()}});
  state.current_map = "field";
  EquipSword(state);

  CombatParams params = ComputeCombatParams(state);
  ASSERT_EQ(params.types.size(), 2u);
  EXPECT_GT(params.types[0].damage_to_player, 0.0);
  // The one that swings ten times harder hurts more, which is the only
  // relation between the two that the map's order does not decide.
  EXPECT_GT(params.types[1].damage_to_player, params.types[0].damage_to_player);
}

TEST(ComputeCombatParamsTest, TheCharactersDefenseReducesWhatMobsDo) {
  // A mob swinging hard enough that the character's bare DEF is nowhere near
  // the cap, so armour still has room to be worth something.
  GameState bare({}, {}, {}, {{"snail", MakeAttacker("Snail", 15, 200, 1)}},
                 {{"field", TwoSnailMap()}});
  bare.current_map = "field";
  EquipSword(bare);

  GameState armoured({}, {}, {}, {{"snail", MakeAttacker("Snail", 15, 200, 1)}},
                     {{"field", TwoSnailMap()}});
  armoured.current_map = "field";
  EquipArmouredSword(armoured, /*def=*/40);

  EXPECT_LT(ComputeCombatParams(armoured).types[0].damage_to_player,
            ComputeCombatParams(bare).types[0].damage_to_player);
}

}  // namespace
}  // namespace ms
