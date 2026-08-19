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

// A mob with weapon defence to ignore, so an Ignore DEF lever has something to
// cancel: every mob in the game itself has none.
Mob MakeArmouredMob(const std::string& name, int max_hp, int pdr) {
  Mob mob = MakeMob(name, max_hp);
  mob.set_pdr(pdr);
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

// A skill the book has replaced stops being an option to swing, not only a
// pile of levers that stops paying: the replacement states the whole of it, so
// offering both would be offering one skill twice.
TEST(ComputeCombatParamsTest, ASupersededSwingIsNotOffered) {
  Skill slash;
  slash.set_name("Slash Blast");
  slash.set_kind(SKILL_KIND_ATTACK);
  slash.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  slash.set_max_level(20);
  slash.set_max_enemies(6);
  slash.mutable_base()->set_skill_pct(1.83);
  Skill blast_ii = slash;
  blast_ii.set_name("Slash Blast II");
  blast_ii.set_supersedes_skill_name("Slash Blast");
  blast_ii.mutable_base()->set_skill_pct(3.20);
  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}},
                  {{"slash_blast", slash}, {"slash_blast_ii", blast_ii}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 2);
  ASSERT_TRUE(state.character.LearnSkill(slash, 1));

  // Unlearned, the replacement replaces nothing -- the original still swings.
  CombatParams before = ComputeCombatParams(state);
  ASSERT_EQ(before.attacks.size(), 2u);
  EXPECT_EQ(before.attacks[1].name, "Slash Blast");

  ASSERT_TRUE(state.character.LearnSkill(blast_ii, 1));
  CombatParams params = ComputeCombatParams(state);
  ASSERT_EQ(params.attacks.size(), 2u);
  EXPECT_EQ(params.attacks[1].name, "Slash Blast II");
}

// A swing that opens with a harder hit on one enemy carries two damage columns
// off one skill: the spread every reached mob takes, and the opening hit.
TEST(ComputeCombatParamsTest, ASwingWithAnOpeningHitCarriesBothHalves) {
  Skill burst;
  burst.set_name("Shuriken Burst");
  burst.set_kind(SKILL_KIND_ATTACK);
  burst.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  burst.set_max_level(20);
  burst.set_max_enemies(6);
  burst.set_lines(6);
  burst.set_lead_lines(1);
  burst.mutable_base()->set_skill_pct(0.48);
  burst.mutable_per_level()->set_skill_pct(0.03);
  burst.mutable_base()->set_lead_pct(4.08);
  burst.mutable_per_level()->set_lead_pct(0.08);
  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}}, {{"shuriken_burst", burst}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 2);
  ASSERT_TRUE(state.character.LearnSkill(burst, 2));

  CombatParams params = ComputeCombatParams(state);
  ASSERT_EQ(params.attacks.size(), 2u);
  ASSERT_EQ(params.attacks[1].lead_damage.size(), params.types.size());
  // Six lines of 51% against one of 416%: the opening hit is worth more even
  // though the spread strikes six times.
  EXPECT_GT(params.attacks[1].lead_damage[0],
            params.attacks[1].damage_per_hit[0]);
  // Both scale with the level, off their own halves of the skill.
  EXPECT_NEAR(
      params.attacks[1].lead_damage[0] / params.attacks[1].damage_per_hit[0],
      4.16 / (6.0 * 0.51), 1e-6);
  // Unsaid is one enemy, which is the shape the field was written for.
  EXPECT_EQ(params.attacks[1].lead_enemies, 1);
}

// The same half of a swing, told to reach further than the one enemy an
// opening hit picks: Piercing Arrow II's fragment bounces onto two of the
// eight the arrow went through.
TEST(ComputeCombatParamsTest, TheOpeningHitCarriesItsOwnReach) {
  Skill piercing;
  piercing.set_name("Piercing Arrow II");
  piercing.set_kind(SKILL_KIND_ATTACK);
  piercing.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  piercing.set_max_level(30);
  piercing.set_max_enemies(8);
  piercing.set_lines(5);
  piercing.set_lead_lines(4);
  piercing.set_lead_enemies(2);
  piercing.mutable_base()->set_skill_pct(3.43);
  piercing.mutable_base()->set_lead_pct(3.50);
  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}},
                  {{"piercing_arrow_ii", piercing}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 1);
  ASSERT_TRUE(state.character.LearnSkill(piercing, 1));

  CombatParams params = ComputeCombatParams(state);
  ASSERT_EQ(params.attacks.size(), 2u);
  EXPECT_EQ(params.attacks[1].max_enemies, 8);
  EXPECT_EQ(params.attacks[1].lead_enemies, 2);
  // Four lines of 350% against five of 343%: the fragment is worth a little
  // less than the arrow, and lands on a quarter of what the arrow reaches.
  EXPECT_NEAR(
      params.attacks[1].lead_damage[0] / params.attacks[1].damage_per_hit[0],
      (4.0 * 3.50) / (5.0 * 3.43), 1e-6);
}

// Shadow Partner doubles what a swing lands. It reaches the swing's own lines
// and its opening hit -- both are the same swing -- and stops at the summons
// and the Final Attacks, which are not.
TEST(ComputeCombatParamsTest, TheShadowCopiesTheSwingAndItsOpeningHit) {
  Skill burst;
  burst.set_name("Shuriken Burst");
  burst.set_kind(SKILL_KIND_ATTACK);
  burst.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  burst.set_max_level(20);
  burst.set_max_enemies(6);
  burst.set_lines(6);
  burst.set_lead_lines(1);
  burst.mutable_base()->set_skill_pct(0.48);
  burst.mutable_base()->set_lead_pct(4.08);
  Skill mist;
  mist.set_name("Poison Mist");
  mist.set_kind(SKILL_KIND_AUTO_ATTACK);
  mist.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  mist.set_max_level(20);
  mist.set_max_enemies(6);
  mist.set_cast_interval_seconds(1.0);
  mist.mutable_base()->set_skill_pct(1.26);
  Skill partner;
  partner.set_name("Shadow Partner");
  partner.set_kind(SKILL_KIND_PASSIVE);
  partner.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  partner.set_max_level(20);
  partner.mutable_base()->set_mirror_line_pct(0.70);

  double bare[3] = {0.0, 0.0, 0.0};
  double shadowed[3] = {0.0, 0.0, 0.0};
  for (int pass = 0; pass < 2; ++pass) {
    GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                    {{"field", TwoSnailMap()}},
                    {{"shuriken_burst", burst},
                     {"poison_mist", mist},
                     {"shadow_partner", partner}});
    state.current_map = "field";
    EquipSword(state);
    GrantFirstJobSp(state, 3);
    ASSERT_TRUE(state.character.LearnSkill(burst, 1));
    ASSERT_TRUE(state.character.LearnSkill(mist, 1));
    if (pass == 1) {
      ASSERT_TRUE(state.character.LearnSkill(partner, 1));
    }
    CombatParams params = ComputeCombatParams(state);
    ASSERT_EQ(params.attacks.size(), 2u);
    ASSERT_EQ(params.auto_attacks.size(), 1u);
    double* into = pass == 0 ? bare : shadowed;
    into[0] = params.attacks[1].damage_per_hit[0];
    into[1] = params.attacks[1].lead_damage[0];
    into[2] = params.auto_attacks[0].damage_per_hit[0];
  }
  // Six real lines and six shadow ones at 70% apiece, and the same again for
  // the single-line opening hit.
  EXPECT_NEAR(shadowed[0] / bare[0], 1.70, 1e-9);
  EXPECT_NEAR(shadowed[1] / bare[1], 1.70, 1e-9);
  // The mist pulses on its own clock, so no shadow follows it.
  EXPECT_NEAR(shadowed[2], bare[2], 1e-9);
}

// A meso falls out per LINE the swing lands, not per enemy it reaches, so a
// four-line swing is worth four times what a one-line swing is. And a skill on
// a clock of its own knocks none loose at all: the character did not swing it.
TEST(ComputeCombatParamsTest, MesosDropPerLineAndOnlyFromWhatIsSwung) {
  Skill carnival;
  carnival.set_name("Midnight Carnival");
  carnival.set_kind(SKILL_KIND_ATTACK);
  carnival.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  carnival.set_max_level(20);
  carnival.set_max_enemies(8);
  carnival.set_lines(4);
  carnival.mutable_base()->set_skill_pct(1.72);
  Skill flare;
  flare.set_name("Dark Flare");
  flare.set_kind(SKILL_KIND_AUTO_ATTACK);
  flare.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  flare.set_max_level(20);
  flare.set_max_enemies(3);
  flare.set_cast_interval_seconds(1.5);
  flare.mutable_base()->set_skill_pct(2.08);
  Skill pocket;
  pocket.set_name("Pick Pocket");
  pocket.set_kind(SKILL_KIND_PASSIVE);
  pocket.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  pocket.set_max_level(10);
  pocket.mutable_base()->set_meso_drop_chance(0.30);
  Skill explosion;
  explosion.set_name("Meso Explosion");
  explosion.set_kind(SKILL_KIND_PASSIVE);
  explosion.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  explosion.set_max_level(20);
  explosion.set_lines(2);
  explosion.mutable_base()->set_meso_hit_pct(1.00);

  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}},
                  {{"midnight_carnival", carnival},
                   {"dark_flare", flare},
                   {"pick_pocket", pocket},
                   {"meso_explosion", explosion}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 4);
  ASSERT_TRUE(state.character.LearnSkill(carnival, 1));
  ASSERT_TRUE(state.character.LearnSkill(flare, 1));
  ASSERT_TRUE(state.character.LearnSkill(pocket, 1));
  ASSERT_TRUE(state.character.LearnSkill(explosion, 1));

  CombatParams params = ComputeCombatParams(state);
  ASSERT_EQ(params.attacks.size(), 2u);
  ASSERT_EQ(params.auto_attacks.size(), 1u);
  // The bare poke is one line; Midnight Carnival is four. Both throw 30% of a
  // two-line 100% meso per line, so the swing is worth four pokes of it.
  ASSERT_FALSE(params.attacks[0].final_attack_damage.empty());
  ASSERT_FALSE(params.attacks[1].final_attack_damage.empty());
  EXPECT_NEAR(params.attacks[1].final_attack_damage[0] /
                  params.attacks[0].final_attack_damage[0],
              4.0, 1e-9);
  EXPECT_TRUE(params.auto_attacks[0].final_attack_damage.empty());

  // And the rolls behind that average say the same thing another way: one
  // source, rolled once a line, so the four-line swing rolls it four times.
  const AttackOption& poke = params.attacks[0];
  const AttackOption& swing = params.attacks[1];
  ASSERT_EQ(swing.final_attack_rolls.size(), 1u);
  EXPECT_NEAR(swing.final_attack_rolls[0].chance, 0.30, 1e-9);
  EXPECT_EQ(swing.final_attack_rolls[0].count, 4);
  EXPECT_EQ(poke.final_attack_rolls[0].count, 1);
  // One meso is worth the same wherever it was knocked loose from.
  EXPECT_DOUBLE_EQ(swing.final_attack_rolls[0].damage[0],
                   poke.final_attack_rolls[0].damage[0]);
  // The average is the roll's, or the fight and the sims part company.
  EXPECT_DOUBLE_EQ(swing.final_attack_damage[0],
                   swing.final_attack_rolls[0].damage[0] * 0.30 * 4);
}

// Nothing but a skill saying so gives a swing an opening hit.
TEST(ComputeCombatParamsTest, AnOrdinarySwingCarriesNoOpeningHit) {
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
  EXPECT_TRUE(params.attacks[0].lead_damage.empty());
  EXPECT_TRUE(params.attacks[1].lead_damage.empty());
}

// A swing is swung at the level the character has it, not the level they
// bought: a granted level buys damage like any other.
TEST(ComputeCombatParamsTest, BonusLevelsReachTheSwing) {
  Skill slash;
  slash.set_name("Slash Blast");
  slash.set_kind(SKILL_KIND_ATTACK);
  slash.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  slash.set_max_level(20);
  slash.mutable_base()->set_skill_pct(1.0);
  slash.mutable_per_level()->set_skill_pct(1.0);
  Skill orders;
  orders.set_name("Combat Orders");
  orders.set_kind(SKILL_KIND_PASSIVE);
  orders.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  orders.set_max_level(10);
  orders.mutable_base()->set_skill_level_bonus(1.0);
  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}},
                  {{"slash_blast", slash}, {"combat_orders", orders}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 5);
  ASSERT_TRUE(state.character.LearnSkill(slash, 1));
  double bought = ComputeCombatParams(state).attacks[1].damage_per_hit[0];

  ASSERT_TRUE(state.character.LearnSkill(orders, 1));
  // Level 2 of a skill worth 100% a level: twice the swing.
  EXPECT_NEAR(ComputeCombatParams(state).attacks[1].damage_per_hit[0],
              bought * 2.0, 1.0);
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

// Blizzard's passive: the same chance and the same damage as any other Final
// Attack, banked apart because it falls on one enemy rather than on each of
// them. A wide swing is worth no more of it than a narrow one.
TEST(ComputeCombatParamsTest, AFinalAttackCanStrikeOneEnemyOnly) {
  Skill wide;
  wide.set_name("Blizzard");
  wide.set_kind(SKILL_KIND_ATTACK);
  wide.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  wide.set_max_level(30);
  wide.set_base_delay_ms(900);
  wide.set_max_enemies(15);
  wide.mutable_base()->set_skill_pct(3.01);
  wide.set_final_attack_single_enemy(true);
  wide.mutable_base()->set_final_attack_chance(0.60);
  wide.mutable_base()->set_final_attack_pct(2.20);

  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}}, {{"blizzard", wide}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 1);
  ASSERT_TRUE(state.character.LearnSkill(wide, 1));

  CombatParams params = ComputeCombatParams(state);
  ASSERT_EQ(params.attacks.size(), 2u);
  const AttackOption& swing = params.attacks[1];
  ASSERT_EQ(swing.name, "Blizzard");
  // Nothing in the ordinary bank, everything in the single-enemy one.
  EXPECT_TRUE(swing.final_attack_damage.empty());
  EXPECT_TRUE(swing.final_attack_rolls.empty());
  ASSERT_EQ(swing.single_final_attack_rolls.size(), 1u);
  EXPECT_NEAR(swing.single_final_attack_rolls[0].chance, 0.60, 1e-9);
  EXPECT_EQ(swing.single_final_attack_rolls[0].count, 1);
  ASSERT_FALSE(swing.single_final_attack_damage.empty());
  EXPECT_DOUBLE_EQ(swing.single_final_attack_damage[0],
                   swing.single_final_attack_rolls[0].damage[0] * 0.60);
}

// A Night Lord's mark throws three stars where an Assassin's throws two. The
// strikes are told apart rather than folded into one percent, so each rolls
// its own crit -- and three of them are worth three times one.
TEST(ComputeCombatParamsTest, AFinalAttackLandsItsOwnStrikes) {
  Skill mark;
  mark.set_name("Night Lord's Mark");
  mark.set_kind(SKILL_KIND_PASSIVE);
  mark.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  mark.set_max_level(10);
  mark.mutable_base()->set_final_attack_chance(0.42);
  mark.mutable_base()->set_final_attack_pct(2.10);

  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}}, {{"mark", mark}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 1);
  ASSERT_TRUE(state.character.LearnSkill(mark, 1));
  CombatParams single = ComputeCombatParams(state);
  ASSERT_FALSE(single.attacks[0].final_attack_damage.empty());
  double one = single.attacks[0].final_attack_damage[0];

  mark.mutable_base()->set_final_attack_lines(3);
  state.skills["mark"] = mark;
  CombatParams params = ComputeCombatParams(state);
  const AttackOption& poke = params.attacks[0];
  EXPECT_NEAR(poke.final_attack_damage[0], one * 3.0, 1e-9);
  ASSERT_EQ(poke.final_attack_rolls.size(), 1u);
  // Three strikes, not three rolls: one chance decides whether all three land.
  EXPECT_EQ(poke.final_attack_rolls[0].count, 1);
  EXPECT_DOUBLE_EQ(poke.final_attack_damage[0],
                   poke.final_attack_rolls[0].damage[0] * 0.42);
}

// Angel Ray's shape: an attack that heals as it lands. The recovery is that
// swing's own, so it reaches the option rather than the character -- a Bishop
// swinging Big Bang instead heals for nothing.
TEST(ComputeCombatParamsTest, AnAttacksRecoveryRidesItsOwnSwing) {
  Skill ray;
  ray.set_name("Angel Ray");
  ray.set_kind(SKILL_KIND_ATTACK);
  ray.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  ray.set_max_level(30);
  ray.set_base_delay_ms(660);
  ray.mutable_base()->set_skill_pct(0.80);
  ray.mutable_base()->set_hp_recover_pct(0.03);
  ray.mutable_per_level()->set_hp_recover_pct(0.01);

  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}}, {{"angel_ray", ray}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 3);
  ASSERT_TRUE(state.character.LearnSkill(ray, 3));

  CombatParams params = ComputeCombatParams(state);
  ASSERT_EQ(params.attacks.size(), 2u);
  EXPECT_EQ(params.attacks[1].name, "Angel Ray");
  EXPECT_DOUBLE_EQ(params.attacks[1].hp_recover_pct, 0.05);
  // The bare poke stands beside it and heals for nothing, and neither does the
  // character: what the skill grants never left the swing.
  EXPECT_DOUBLE_EQ(params.attacks[0].hp_recover_pct, 0.0);
  EXPECT_DOUBLE_EQ(params.hp_recover_pct, 0.0);
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

// A key-down skill fires at its stated rate however fast the weapon swings:
// the whole point of the flag is that the stage has no say.
TEST(ComputeCombatParamsTest, AFixedDelaySwingIgnoresTheAttackSpeedStage) {
  Skill blaster;
  blaster.set_name("Arrow Blaster");
  blaster.set_kind(SKILL_KIND_ATTACK);
  blaster.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  blaster.set_max_level(20);
  blaster.set_base_delay_ms(120);
  blaster.set_fixed_delay(true);
  blaster.mutable_base()->set_skill_pct(1.24);

  // The same character and skill under the slowest weapon and the fastest.
  double at_stage[2] = {0.0, 0.0};
  AttackSpeed speeds[2] = {ATTACK_SPEED_SLOWER, ATTACK_SPEED_FASTEST_3};
  for (int i = 0; i < 2; ++i) {
    GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                    {{"field", TwoSnailMap()}}, {{"arrow_blaster", blaster}});
    state.current_map = "field";
    EquipSwordAt(state, speeds[i]);
    GrantFirstJobSp(state, 1);
    ASSERT_TRUE(state.character.LearnSkill(blaster, 1));
    CombatParams params = ComputeCombatParams(state);
    ASSERT_EQ(params.attacks.size(), 2u);
    at_stage[i] = params.attacks[1].swing_seconds;
    // The bare poke beside it still answers to the weapon, so the two weapons
    // really do differ -- the skill is what does not move.
    EXPECT_DOUBLE_EQ(params.attacks[1].swing_seconds,
                     0.120 * GameSpeedFactor(state.character.proto().level()));
  }
  EXPECT_DOUBLE_EQ(at_stage[0], at_stage[1]);
}

TEST(ComputeCombatParamsTest, AnOrdinarySwingStillAnswersToTheWeapon) {
  Skill wind;
  wind.set_name("Wind Arrow");
  wind.set_kind(SKILL_KIND_ATTACK);
  wind.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  wind.set_max_level(20);
  wind.set_base_delay_ms(810);
  wind.mutable_base()->set_skill_pct(0.83);

  double at_stage[2] = {0.0, 0.0};
  AttackSpeed speeds[2] = {ATTACK_SPEED_SLOWER, ATTACK_SPEED_FASTEST_3};
  for (int i = 0; i < 2; ++i) {
    GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                    {{"field", TwoSnailMap()}}, {{"wind_arrow", wind}});
    state.current_map = "field";
    EquipSwordAt(state, speeds[i]);
    GrantFirstJobSp(state, 1);
    ASSERT_TRUE(state.character.LearnSkill(wind, 1));
    at_stage[i] = ComputeCombatParams(state).attacks[1].swing_seconds;
  }
  EXPECT_GT(at_stage[0], at_stage[1]);
}

// The magician's exception, against the test above it: GMS casts at the
// unscaled stage whatever the staff says. The boosts still land on top --
// AttackSpeedPassiveShortensTheSwing holds that end.
TEST(ComputeCombatParamsTest, AMagiciansSwingIgnoresTheWeaponsStage) {
  double at_stage[2] = {0.0, 0.0};
  AttackSpeed speeds[2] = {ATTACK_SPEED_SLOWER, ATTACK_SPEED_FASTEST_3};
  for (int i = 0; i < 2; ++i) {
    GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                    {{"field", TwoSnailMap()}});
    state.current_map = "field";
    EquipSwordAt(state, speeds[i]);
    state.character.AdvanceJob(JOB_MAGICIAN);
    at_stage[i] = ComputeCombatParams(state).attacks.front().swing_seconds;
    EXPECT_DOUBLE_EQ(
        at_stage[i],
        SwingIntervalSeconds(kDefaultSwingDelayMs, kUnscaledAttackSpeedStage) *
            GameSpeedFactor(state.character.proto().level()));
  }
  EXPECT_DOUBLE_EQ(at_stage[0], at_stage[1]);
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

// Revenge of the Evil Eye's shape: one skill that fires on its own clock AND
// carries two more clocks behind it, each reaching a different number of
// enemies -- which is why they cannot be folded into one attack.
TEST(ComputeCombatParamsTest, ASkillCanCarrySeveralOwnClockHalves) {
  Skill revenge;
  revenge.set_name("Revenge of the Evil Eye");
  revenge.set_kind(SKILL_KIND_AUTO_ATTACK);
  revenge.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  revenge.set_max_level(30);
  revenge.set_max_enemies(10);
  revenge.set_cast_interval_seconds(5.0);
  revenge.mutable_base()->set_skill_pct(1.35);
  AutoMode* shock = revenge.add_auto_mode();
  shock->set_label("Evil Eye Shock III");
  shock->set_cast_interval_seconds(10.0);
  shock->set_max_enemies(10);
  shock->mutable_base()->set_skill_pct(3.40);
  AutoMode* auras = revenge.add_auto_mode();
  auras->set_label("Dark Auras");
  auras->set_cast_interval_seconds(10.0);
  auras->set_max_enemies(3);
  auras->mutable_base()->set_skill_pct(2.20);

  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}}, {{"revenge", revenge}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 1);
  ASSERT_TRUE(state.character.LearnSkill(revenge, 1));

  CombatParams params = ComputeCombatParams(state);
  EXPECT_EQ(params.attacks.size(), 1u);  // the bare poke, and nothing else
  ASSERT_EQ(params.auto_attacks.size(), 3u);
  // The two halves are built first, then the skill's own clock behind them.
  // Each keeps its own reach and its own period.
  double factor = GameSpeedFactor(state.character.proto().level());
  EXPECT_EQ(params.auto_attacks[0].max_enemies, 10);
  EXPECT_EQ(params.auto_attacks[1].max_enemies, 3);
  EXPECT_EQ(params.auto_attacks[2].max_enemies, 10);
  EXPECT_DOUBLE_EQ(params.auto_attacks[0].interval_seconds, 10.0 * factor);
  EXPECT_DOUBLE_EQ(params.auto_attacks[1].interval_seconds, 10.0 * factor);
  EXPECT_DOUBLE_EQ(params.auto_attacks[2].interval_seconds, 5.0 * factor);
  // 340%, then 220%, then the skill's own 135%: each is hitting for its own
  // damage rather than for the parent's.
  EXPECT_GT(params.auto_attacks[0].damage_per_hit[0],
            params.auto_attacks[1].damage_per_hit[0]);
  EXPECT_GT(params.auto_attacks[1].damage_per_hit[0],
            params.auto_attacks[2].damage_per_hit[0]);
}

// A timed buff needs a damage table of its own, because what it grants -- a
// share of the monster's DEF ignored -- cannot be applied to a damage number
// after that number has been worked out.
TEST(ComputeCombatParamsTest, ABuffGetsADamageTableOfItsOwn) {
  Skill resonance;
  resonance.set_name("Dark Resonance");
  resonance.set_kind(SKILL_KIND_ACTIVE);
  resonance.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  resonance.set_max_level(30);
  resonance.set_cooldown_seconds(70.0);
  Buff* buff = resonance.mutable_buff();
  buff->set_duration_seconds(30.0);
  buff->set_cooldown_reduction_seconds(0.35);
  buff->mutable_base()->set_final_dmg_pct(0.50);
  buff->mutable_base()->set_heal_pct(1.00);

  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}}, {{"dark_resonance", resonance}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 1);
  ASSERT_TRUE(state.character.LearnSkill(resonance, 1));

  CombatParams params = ComputeCombatParams(state);
  double factor = GameSpeedFactor(state.character.proto().level());
  ASSERT_EQ(params.buffs.size(), 1u);
  EXPECT_EQ(params.buffs[0].name, "Dark Resonance");
  EXPECT_DOUBLE_EQ(params.buffs[0].duration_seconds, 30.0 * factor);
  EXPECT_DOUBLE_EQ(params.buffs[0].cooldown_seconds, 70.0 * factor);
  EXPECT_DOUBLE_EQ(params.buffs[0].cooldown_reduction_seconds, 0.35 * factor);
  EXPECT_DOUBLE_EQ(params.buffs[0].heal_fraction, 1.00);

  // One table for the one combination there is, holding the same attacks in
  // the same order -- half again as hard, since the buff is 50% final damage.
  ASSERT_EQ(params.buffed.size(), 1u);
  ASSERT_EQ(params.Attacks(1).size(), params.attacks.size());
  EXPECT_NEAR(params.Attacks(1)[0].damage_per_hit[0],
              1.5 * params.attacks[0].damage_per_hit[0], 1e-9);
  // Out of range is the character as they stand, never a read off the end.
  EXPECT_DOUBLE_EQ(params.Attacks(0)[0].damage_per_hit[0],
                   params.attacks[0].damage_per_hit[0]);
  EXPECT_DOUBLE_EQ(params.Attacks(7)[0].damage_per_hit[0],
                   params.attacks[0].damage_per_hit[0]);
}

// Buff Mastery's lever lengthens the buff and leaves the wait alone, which is
// the whole of what it buys: a buff up longer without coming round sooner.
TEST(ComputeCombatParamsTest, BuffDurationLengthensTheBuffAndNotTheWait) {
  Skill resonance;
  resonance.set_name("Dark Resonance");
  resonance.set_kind(SKILL_KIND_ACTIVE);
  resonance.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  resonance.set_max_level(30);
  resonance.set_cooldown_seconds(70.0);
  Buff* buff = resonance.mutable_buff();
  buff->set_duration_seconds(30.0);
  buff->mutable_base()->set_final_dmg_pct(0.50);

  Skill mastery;
  mastery.set_name("Buff Mastery");
  mastery.set_kind(SKILL_KIND_PASSIVE);
  mastery.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  mastery.set_max_level(10);
  mastery.mutable_base()->set_buff_duration_pct(0.05);
  mastery.mutable_per_level()->set_buff_duration_pct(0.05);

  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}},
                  {{"dark_resonance", resonance}, {"buff_mastery", mastery}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 11);
  ASSERT_TRUE(state.character.LearnSkill(resonance, 1));
  ASSERT_TRUE(state.character.LearnSkill(mastery, 10));

  CombatParams params = ComputeCombatParams(state);
  double factor = GameSpeedFactor(state.character.proto().level());
  ASSERT_EQ(params.buffs.size(), 1u);
  EXPECT_DOUBLE_EQ(params.buffs[0].duration_seconds, 45.0 * factor);
  EXPECT_DOUBLE_EQ(params.buffs[0].cooldown_seconds, 70.0 * factor);
}

// Flame Sweep's shape: the swing leaves a burn on what it reached, priced on
// its own multiplier and given a slot of its own. Nothing on a clock of its
// own leaves one -- a summon marks nothing.
TEST(ComputeCombatParamsTest, ASwingCanLeaveABurn) {
  Skill sweep;
  sweep.set_name("Flame Sweep");
  sweep.set_kind(SKILL_KIND_ATTACK);
  sweep.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  sweep.set_max_level(30);
  sweep.set_base_delay_ms(780);
  sweep.set_max_enemies(8);
  sweep.set_lines(7);
  sweep.mutable_base()->set_skill_pct(2.00);
  Dot* burn = sweep.mutable_dot();
  burn->set_interval_seconds(1.0);
  burn->set_duration_seconds(5.0);
  burn->set_lines(1);
  burn->mutable_base()->set_skill_pct(2.40);

  Skill summon;
  summon.set_name("Ifrit");
  summon.set_kind(SKILL_KIND_AUTO_ATTACK);
  summon.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  summon.set_max_level(20);
  summon.set_max_enemies(3);
  summon.set_cast_interval_seconds(3.0);
  summon.mutable_base()->set_skill_pct(4.30);
  *summon.mutable_dot() = *burn;

  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}},
                  {{"flame_sweep", sweep}, {"ifrit", summon}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 13);
  ASSERT_TRUE(state.character.LearnSkill(sweep, 1));
  ASSERT_TRUE(state.character.LearnSkill(summon, 1));

  CombatParams params = ComputeCombatParams(state);
  double factor = GameSpeedFactor(state.character.proto().level());
  ASSERT_EQ(params.attacks.size(), 2u);
  const AttackOption& swing = params.attacks[1];
  ASSERT_EQ(swing.name, "Flame Sweep");
  ASSERT_EQ(swing.dots.size(), 1u);
  EXPECT_EQ(swing.dots[0].slot, 0);
  EXPECT_EQ(params.dot_count, 1);
  ASSERT_EQ(swing.dots[0].damage.size(), 1u);
  EXPECT_DOUBLE_EQ(swing.dots[0].interval_seconds, 1.0 * factor);
  EXPECT_DOUBLE_EQ(swing.dots[0].duration_seconds, 5.0 * factor);
  // Said nothing about either, so it takes hold every time and burns once.
  EXPECT_DOUBLE_EQ(swing.dots[0].chance, 1.0);
  EXPECT_EQ(swing.dots[0].max_stacks, 1);
  // One line at 240% against the swing's seven at 200%: the burn is priced on
  // its own multiplier, not on a share of the strike that lit it.
  EXPECT_NEAR(swing.dots[0].damage[0] / (swing.damage_per_hit[0] / 7.0), 1.2,
              0.02);

  ASSERT_EQ(params.auto_attacks.size(), 1u);
  EXPECT_EQ(params.auto_attacks[0].name, "Ifrit");
  EXPECT_TRUE(params.auto_attacks[0].dots.empty());

  // A poison is rolled for and piles up, on the ladder GMS's own walks: two
  // helpings through level 5, three through 11, four at 12. Read a level at a
  // time, because a step this shallow is exactly what floors wrongly.
  burn->set_chance(0.32);
  burn->set_chance_per_level(0.02);
  burn->set_max_stacks(2.1666667);
  burn->set_max_stacks_per_level(0.1666667);
  *sweep.mutable_dot() = *burn;
  state.skills["flame_sweep"] = sweep;
  const int kStacksAt[12] = {2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 4};
  for (int level = 1; level <= 12; ++level) {
    if (level > 1) {
      ASSERT_TRUE(state.character.LearnSkill(sweep, 1));
    }
    CombatParams at = ComputeCombatParams(state);
    ASSERT_EQ(at.attacks[1].dots.size(), 1u);
    EXPECT_NEAR(at.attacks[1].dots[0].chance, 0.30 + 0.02 * level, 1e-9)
        << "level " << level;
    EXPECT_EQ(at.attacks[1].dots[0].max_stacks, kStacksAt[level - 1])
        << "level " << level;
  }
}

// A poison on the claw is the character's, not one swing's: every swing they
// choose applies it, and all of them write the one slot -- or a monster would
// carry a different poison per swing that hit it. A burn the skill itself
// leaves takes a slot after them.
TEST(ComputeCombatParamsTest, APassivesBurnRidesEverySwing) {
  Skill venom;
  venom.set_name("Venom");
  venom.set_kind(SKILL_KIND_PASSIVE);
  venom.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  venom.set_max_level(10);
  Dot* poison = venom.mutable_dot();
  poison->set_interval_seconds(1.0);
  poison->set_duration_seconds(6.0);
  poison->set_chance(0.12);
  poison->set_chance_per_level(0.02);
  poison->mutable_base()->set_skill_pct(0.54);
  poison->mutable_per_level()->set_skill_pct(0.04);

  Skill raid;
  raid.set_name("Sudden Raid");
  raid.set_kind(SKILL_KIND_ATTACK);
  raid.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  raid.set_max_level(30);
  raid.set_base_delay_ms(900);
  raid.mutable_base()->set_skill_pct(3.49);
  Dot* own = raid.mutable_dot();
  own->set_interval_seconds(1.0);
  own->set_duration_seconds(10.0);
  own->mutable_base()->set_skill_pct(0.94);

  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}},
                  {{"venom", venom}, {"sudden_raid", raid}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 11);
  ASSERT_TRUE(state.character.LearnSkill(venom, 10));
  ASSERT_TRUE(state.character.LearnSkill(raid, 1));

  CombatParams params = ComputeCombatParams(state);
  ASSERT_EQ(params.attacks.size(), 2u);
  EXPECT_EQ(params.dot_count, 2);
  // The bare poke carries the poison too: it is on the claw, not in a skill.
  ASSERT_EQ(params.attacks[0].dots.size(), 1u);
  EXPECT_EQ(params.attacks[0].dots[0].slot, 0);
  EXPECT_NEAR(params.attacks[0].dots[0].chance, 0.30, 1e-9);
  const AttackOption& swing = params.attacks[1];
  ASSERT_EQ(swing.dots.size(), 2u);
  EXPECT_EQ(swing.dots[0].slot, 0);
  EXPECT_EQ(swing.dots[1].slot, 1);
  // The same poison whichever swing applied it, and priced off the bare stat
  // line rather than off the swing that carried it.
  EXPECT_DOUBLE_EQ(swing.dots[0].damage[0],
                   params.attacks[0].dots[0].damage[0]);
  EXPECT_DOUBLE_EQ(swing.dots[1].chance, 1.0);
  EXPECT_GT(swing.dots[1].damage[0], swing.dots[0].damage[0]);
}

// Puncture's shape: the buff hangs off an ATTACK, so it is laid by that swing
// rather than raised on a wait of its own. The fight needs to know which swing
// lays it, and that has to be the swing's index in the attack list.
TEST(ComputeCombatParamsTest, ABuffOnAnAttackIsLaidByThatSwing) {
  Skill puncture;
  puncture.set_name("Puncture");
  puncture.set_kind(SKILL_KIND_ATTACK);
  puncture.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  puncture.set_max_level(30);
  puncture.set_base_delay_ms(720);
  puncture.set_max_enemies(8);
  puncture.mutable_base()->set_skill_pct(3.13);
  Buff* wound = puncture.mutable_buff();
  wound->set_duration_seconds(45.0);
  wound->mutable_base()->set_damage_pct(0.25);

  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}}, {{"puncture", puncture}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 1);
  ASSERT_TRUE(state.character.LearnSkill(puncture, 1));

  CombatParams params = ComputeCombatParams(state);
  ASSERT_EQ(params.buffs.size(), 1u);
  // Index 1, past the bare poke: the swing that leaves the wound, and no wait
  // at all, since what it waits for is being swung again.
  ASSERT_EQ(params.attacks.size(), 2u);
  EXPECT_EQ(params.attacks[1].name, "Puncture");
  EXPECT_EQ(params.buffs[0].laid_by_attack, 1);
  EXPECT_DOUBLE_EQ(params.buffs[0].cooldown_seconds, 0.0);

  // A buff raised on its own wait says so by naming no swing -- the fight
  // tells the two apart on this field alone.
  Skill on_a_wait = puncture;
  on_a_wait.set_kind(SKILL_KIND_ACTIVE);
  on_a_wait.set_cooldown_seconds(70.0);
  state.skills["puncture"] = on_a_wait;
  EXPECT_EQ(ComputeCombatParams(state).buffs[0].laid_by_attack, -1);

  // And what the buff bleeds is pointed at it, in the base table and in the
  // buffed one alike: what ticks is the wound, so it ticks only where one was
  // left. It reaches the swing's eight rather than a count of its own.
  BuffPulse* tick = wound->mutable_pulse();
  tick->set_label("Wound");
  tick->set_cast_interval_seconds(2.0);
  tick->set_lines(1);
  tick->mutable_base()->set_skill_pct(1.65);
  state.skills["puncture"] = puncture;
  CombatParams gated = ComputeCombatParams(state);
  ASSERT_EQ(gated.auto_attacks.size(), 1u);
  EXPECT_EQ(gated.auto_attacks[0].needs_buff, 0);
  EXPECT_EQ(gated.auto_attacks[0].max_enemies, 8);
  EXPECT_DOUBLE_EQ(gated.auto_attacks[0].interval_seconds,
                   2.0 * GameSpeedFactor(state.character.proto().level()));
  ASSERT_EQ(gated.buffed.size(), 1u);
  ASSERT_EQ(gated.buffed[0].auto_attacks.size(), 1u);
  EXPECT_EQ(gated.buffed[0].auto_attacks[0].needs_buff, 0);
}

// A character with no buff carries no tables at all: the cost of the
// mechanism is paid only by the jobs that use it.
TEST(ComputeCombatParamsTest, NoBuffMeansNoExtraTables) {
  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}});
  state.current_map = "field";
  EquipSword(state);

  CombatParams params = ComputeCombatParams(state);
  EXPECT_TRUE(params.buffs.empty());
  EXPECT_TRUE(params.buffed.empty());
}

// The Thunder Sphere case. Beam Blade's normal-monster lever was written for a
// swing, and the orb that carries it now is not one -- so the thing to check is
// that a summon reaches the lever at all.
TEST(ComputeCombatParamsTest, ASummonCutsDeeperIntoANormalMonsterToo) {
  Skill orb;
  orb.set_name("Thunder Sphere");
  orb.set_kind(SKILL_KIND_AUTO_ATTACK);
  orb.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  orb.set_max_level(10);
  orb.set_cast_interval_seconds(2.0);
  orb.mutable_base()->set_skill_pct(1.00);

  double plain = 0.0;
  for (double normal : {0.0, 1.00}) {
    orb.mutable_base()->set_normal_skill_pct(normal);
    GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                    {{"field", TwoSnailMap()}}, {{"thunder_sphere", orb}});
    state.current_map = "field";
    EquipSword(state);
    GrantFirstJobSp(state, 1);
    ASSERT_TRUE(state.character.LearnSkill(orb, 1));

    CombatParams params = ComputeCombatParams(state);
    ASSERT_EQ(params.auto_attacks.size(), 1u);
    double damage = params.auto_attacks[0].damage_per_hit[0];
    if (normal == 0.0) {
      plain = damage;
      ASSERT_GT(plain, 0.0);
    } else {
      EXPECT_DOUBLE_EQ(damage, 2.0 * plain);
    }
  }
}

// Divine Mark's shape: the hammer and the brand it leaves exploding, landed as
// one swing. Kept as two hits rather than averaged so each keeps its own
// multiplier -- and so only the explosion hits an ordinary monster harder.
TEST(ComputeCombatParamsTest, ASwingCanLandTwoHitsPricedSeparately) {
  Skill mark;
  mark.set_name("Divine Mark");
  mark.set_kind(SKILL_KIND_ATTACK);
  mark.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  mark.set_max_level(30);
  mark.set_max_enemies(10);
  mark.set_lines(7);
  mark.mutable_base()->set_skill_pct(3.00);
  SwingHit* blast = mark.add_extra_hit();
  blast->set_label("Explosion");
  blast->set_lines(5);
  blast->mutable_base()->set_skill_pct(2.00);
  blast->mutable_base()->set_normal_skill_pct(0.60);

  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}}, {{"divine_mark", mark}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 1);
  ASSERT_TRUE(state.character.LearnSkill(mark, 1));

  CombatParams params = ComputeCombatParams(state);
  ASSERT_EQ(params.attacks.size(), 2u);
  // The snail is no boss, so the explosion lands 260% five times beside the
  // hammer's 300% seven: 21 + 13 poke-fuls against the poke's own 100% once.
  double poke = params.attacks[0].damage_per_hit[0];
  EXPECT_DOUBLE_EQ(params.attacks[1].damage_per_hit[0], 34.0 * poke);

  // Each half also rolls apart, on its own line count: the hammer's seven and
  // the explosion's five. Their expected damages are what damage_per_hit is
  // the sum of, which is what keeps the rolled swing and the average one
  // agreeing.
  const AttackOption& swing = params.attacks[1];
  ASSERT_EQ(swing.groups.size(), 2u);
  EXPECT_EQ(swing.groups[0].rolls.lines, 7);
  EXPECT_EQ(swing.groups[1].rolls.lines, 5);
  EXPECT_DOUBLE_EQ(swing.groups[0].damage[0] + swing.groups[1].damage[0],
                   swing.damage_per_hit[0]);

  // The bonus is worth its five strikes and not the hammer's seven, which is
  // the whole reason the two halves are priced apart rather than averaged.
  Skill plain = mark;
  plain.mutable_extra_hit(0)->mutable_base()->clear_normal_skill_pct();
  state.skills["divine_mark"] = plain;
  EXPECT_DOUBLE_EQ(ComputeCombatParams(state).attacks[1].damage_per_hit[0],
                   31.0 * poke);
}

// Raging Blow's shape: four strikes of which the final two always crit. The
// two halves are the same multiplier, so the whole of the difference between
// them is the certainty -- which is what makes it worth telling them apart.
TEST(ComputeCombatParamsTest, AHalfOfASwingCanBeCertainToCrit) {
  Skill blow;
  blow.set_name("Raging Blow");
  blow.set_kind(SKILL_KIND_ATTACK);
  blow.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  blow.set_max_level(30);
  blow.set_max_enemies(8);
  blow.set_lines(2);
  blow.mutable_base()->set_skill_pct(2.00);
  SwingHit* certain = blow.add_extra_hit();
  certain->set_label("Critical Hits");
  certain->set_lines(2);
  certain->mutable_base()->set_skill_pct(2.00);
  certain->mutable_base()->set_crit_rate(1.00);

  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}}, {{"raging_blow", blow}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 1);
  ASSERT_TRUE(state.character.LearnSkill(blow, 1));

  CombatParams params = ComputeCombatParams(state);
  ASSERT_EQ(params.attacks.size(), 2u);
  // Four poke-fuls from the ordinary half, and four more from the certain one
  // lifted by the whole of a crit rather than by the base 5% chance of one.
  double poke = params.attacks[0].damage_per_hit[0];
  double plain = 1.0 + kBaseCritRate * kBaseCritDamage;
  double certainly = 1.0 + kBaseCritDamage;
  EXPECT_NEAR(params.attacks[1].damage_per_hit[0],
              poke * (4.0 + 4.0 * certainly / plain), 1e-6);

  // The same swing with the certainty struck out is eight plain poke-fuls,
  // which is the claim the line above is really making.
  blow.mutable_extra_hit(0)->mutable_base()->clear_crit_rate();
  state.skills["raging_blow"] = blow;
  EXPECT_NEAR(ComputeCombatParams(state).attacks[1].damage_per_hit[0],
              poke * 8.0, 1e-6);
}

// Greater Vessel of Light's shape: one passive that hands a strike to two
// attacks and widens one of them. The grant is folded into the target before
// its attack is built, so the damage the fight sees is already carrying it.
TEST(ComputeCombatParamsTest, APassiveHandsStrikesAndReachToSkillsItNames) {
  Skill charge;
  charge.set_name("Divine Charge");
  charge.set_kind(SKILL_KIND_ATTACK);
  charge.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  charge.set_max_level(20);
  charge.set_max_enemies(8);
  charge.set_lines(4);
  charge.mutable_base()->set_skill_pct(2.53);
  Skill vessel;
  vessel.set_name("Greater Vessel of Light");
  vessel.set_kind(SKILL_KIND_PASSIVE);
  vessel.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  vessel.set_max_level(10);
  SkillBoost* boost = vessel.add_boost();
  boost->set_skill_name("Divine Charge");
  boost->set_lines(1);
  boost->set_max_enemies(1);
  boost->set_max_enemies_per_level(0.2);

  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}},
                  {{"divine_charge", charge}, {"vessel", vessel}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 30);
  ASSERT_TRUE(state.character.LearnSkill(charge, 1));

  // Unboosted first: four strikes across eight enemies.
  CombatParams bare = ComputeCombatParams(state);
  ASSERT_EQ(bare.attacks.size(), 2u);
  double one_line = bare.attacks[1].damage_per_hit[0] / 4.0;
  EXPECT_EQ(bare.attacks[1].max_enemies, 8);

  // At level 1 the vessel is worth a strike and one enemy.
  ASSERT_TRUE(state.character.LearnSkill(vessel, 1));
  CombatParams params = ComputeCombatParams(state);
  ASSERT_EQ(params.attacks.size(), 2u);
  EXPECT_EQ(params.attacks[1].max_enemies, 9);
  EXPECT_DOUBLE_EQ(params.attacks[1].damage_per_hit[0], 5.0 * one_line);

  // The reach climbs on the ladder its rate names, at the sixth level.
  ASSERT_TRUE(state.character.LearnSkill(vessel, 4));  // level 5
  EXPECT_EQ(ComputeCombatParams(state).attacks[1].max_enemies, 9);
  ASSERT_TRUE(state.character.LearnSkill(vessel, 1));  // level 6
  EXPECT_EQ(ComputeCombatParams(state).attacks[1].max_enemies, 10);
}

// Creeping Toxin's shape: a summon that upgrades its own pulse rather than
// another skill's swing, which is what an empty boosts_skill_name means.
TEST(ComputeCombatParamsTest, ASummonCanUpgradeItsOwnPulse) {
  Skill toxin;
  toxin.set_name("Creeping Toxin");
  toxin.set_kind(SKILL_KIND_AUTO_ATTACK);
  toxin.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  toxin.set_max_level(10);
  toxin.set_max_enemies(10);
  toxin.set_cast_interval_seconds(1.0);
  toxin.mutable_base()->set_skill_pct(1.00);
  EmpoweredForm* form = toxin.add_empowered_form();
  form->set_casts_per_trigger(4);
  form->set_lines(4);
  form->mutable_base()->set_skill_pct(2.00);
  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}}, {{"creeping_toxin", toxin}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 1);
  ASSERT_TRUE(state.character.LearnSkill(toxin, 1));

  CombatParams params = ComputeCombatParams(state);
  ASSERT_EQ(params.auto_attacks.size(), 1u);
  const AttackOption& pulse = params.auto_attacks[0];
  EXPECT_EQ(pulse.empowered_every, 4);
  ASSERT_NE(pulse.empowered, nullptr);
  // Eight times the pulse: twice the damage, four lines against the one.
  EXPECT_DOUBLE_EQ(pulse.empowered->damage_per_hit[0],
                   8.0 * pulse.damage_per_hit[0]);
  // It stands in for a pulse, so it is paced by the summon's clock and never
  // charged a swing of its own.
  EXPECT_DOUBLE_EQ(pulse.empowered->swing_seconds, 0.0);
  // Saying nothing about its reach, it goes exactly as far as the pulse it
  // replaces.
  EXPECT_EQ(pulse.empowered->max_enemies, 10);
}

// Greater Empowered Arrows' shape: one passive, one SP ladder, two swings
// upgraded. Each form names the swing it takes the place of, and neither
// lands on the other's.
TEST(ComputeCombatParamsTest, OneSkillCanEmpowerTwoDifferentSwings) {
  Skill piercing;
  piercing.set_name("Piercing Arrow II");
  piercing.set_kind(SKILL_KIND_ATTACK);
  piercing.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  piercing.set_max_level(30);
  piercing.set_max_enemies(8);
  piercing.set_lines(5);
  piercing.mutable_base()->set_skill_pct(1.00);
  Skill snipe = piercing;
  snipe.set_name("Snipe");
  snipe.set_max_enemies(1);
  snipe.set_lines(9);

  Skill greater;
  greater.set_name("Greater Empowered Arrows");
  greater.set_kind(SKILL_KIND_PASSIVE);
  greater.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  greater.set_max_level(20);
  EmpoweredForm* arrow = greater.add_empowered_form();
  arrow->set_skill_name("Piercing Arrow II");
  arrow->set_casts_per_trigger(4);
  arrow->set_max_enemies(10);
  arrow->set_lines(6);
  arrow->mutable_base()->set_skill_pct(2.00);
  EmpoweredForm* shot = greater.add_empowered_form();
  shot->set_skill_name("Snipe");
  shot->set_casts_per_trigger(4);
  shot->set_max_enemies(1);
  shot->set_lines(10);
  shot->mutable_base()->set_skill_pct(3.00);

  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}},
                  {{"piercing_arrow_ii", piercing},
                   {"snipe", snipe},
                   {"greater_empowered_arrows", greater}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 3);
  ASSERT_TRUE(state.character.LearnSkill(piercing, 1));
  ASSERT_TRUE(state.character.LearnSkill(snipe, 1));
  ASSERT_TRUE(state.character.LearnSkill(greater, 1));

  CombatParams params = ComputeCombatParams(state);
  int arrow_at = -1;
  int snipe_at = -1;
  for (int i = 0; i < static_cast<int>(params.attacks.size()); ++i) {
    if (params.attacks[i].name == "Piercing Arrow II") {
      arrow_at = i;
    }
    if (params.attacks[i].name == "Snipe") {
      snipe_at = i;
    }
  }
  ASSERT_GE(arrow_at, 0);
  ASSERT_GE(snipe_at, 0);
  const AttackOption& upgraded_arrow = params.attacks[arrow_at];
  const AttackOption& upgraded_snipe = params.attacks[snipe_at];
  ASSERT_NE(upgraded_arrow.empowered, nullptr);
  ASSERT_NE(upgraded_snipe.empowered, nullptr);
  EXPECT_EQ(upgraded_arrow.empowered->name, "Empowered Piercing Arrow II");
  EXPECT_EQ(upgraded_snipe.empowered->name, "Empowered Snipe");
  // Each form's own reach and its own multiplier, not the other's.
  EXPECT_EQ(upgraded_arrow.empowered->max_enemies, 10);
  EXPECT_EQ(upgraded_snipe.empowered->max_enemies, 1);
  // 6 lines of 200% against the arrow's 5 of 100%, and 10 of 300% against the
  // shot's 9 of 100%.
  EXPECT_DOUBLE_EQ(upgraded_arrow.empowered->damage_per_hit[0],
                   2.4 * upgraded_arrow.damage_per_hit[0]);
  EXPECT_DOUBLE_EQ(upgraded_snipe.empowered->damage_per_hit[0],
                   (10.0 * 3.0 / 9.0) * upgraded_snipe.damage_per_hit[0]);
}

// A swing that always crits states so on itself, and says nothing about the
// swing after it -- the same shape ignored defence written on an attack has.
TEST(ComputeCombatParamsTest, ACertainCritRidesOnlyItsOwnSwing) {
  Skill snipe;
  snipe.set_name("Snipe");
  snipe.set_kind(SKILL_KIND_ATTACK);
  snipe.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  snipe.set_max_level(30);
  snipe.set_max_enemies(1);
  snipe.mutable_base()->set_skill_pct(1.00);
  snipe.mutable_base()->set_crit_rate(1.0);
  Skill plain = snipe;
  plain.set_name("Plain Shot");
  plain.clear_base();
  plain.mutable_base()->set_skill_pct(1.00);

  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}},
                  {{"snipe", snipe}, {"plain_shot", plain}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 2);
  ASSERT_TRUE(state.character.LearnSkill(snipe, 1));
  ASSERT_TRUE(state.character.LearnSkill(plain, 1));

  CombatParams params = ComputeCombatParams(state);
  int snipe_at = -1;
  int plain_at = -1;
  for (int i = 0; i < static_cast<int>(params.attacks.size()); ++i) {
    if (params.attacks[i].name == "Snipe") {
      snipe_at = i;
    }
    if (params.attacks[i].name == "Plain Shot") {
      plain_at = i;
    }
  }
  ASSERT_GE(snipe_at, 0);
  ASSERT_GE(plain_at, 0);
  // Same multiplier, same lines: what separates them is the certainty.
  EXPECT_GT(params.attacks[snipe_at].damage_per_hit[0],
            params.attacks[plain_at].damage_per_hit[0]);
  EXPECT_DOUBLE_EQ(params.attacks[snipe_at].groups[0].rolls.crit_rate, 1.0);
  EXPECT_LT(params.attacks[plain_at].groups[0].rolls.crit_rate, 1.0);
}

// A summon states its ignored defence under GMS's "[Passive Effects]", meaning
// the character keeps it -- unlike a swing's, which lasts exactly as long as
// the swing does. Arrow Illusion is what states one.
TEST(ComputeCombatParamsTest, ASummonsIgnoredDefenceFollowsTheCharacter) {
  Skill plain;
  plain.set_name("Plain Shot");
  plain.set_kind(SKILL_KIND_ATTACK);
  plain.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  plain.set_max_level(20);
  plain.set_max_enemies(1);
  plain.mutable_base()->set_skill_pct(1.00);
  Skill illusion;
  illusion.set_name("Arrow Illusion");
  illusion.set_kind(SKILL_KIND_AUTO_ATTACK);
  illusion.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  illusion.set_max_level(20);
  illusion.set_max_enemies(6);
  illusion.set_cast_interval_seconds(3.0);
  illusion.mutable_base()->set_skill_pct(1.00);
  illusion.mutable_base()->set_ied_pct(0.30);

  GameState state({}, {}, {}, {{"snail", MakeArmouredMob("Snail", 15, 60)}},
                  {{"field", TwoSnailMap()}},
                  {{"plain_shot", plain}, {"arrow_illusion", illusion}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 2);
  ASSERT_TRUE(state.character.LearnSkill(plain, 1));

  CombatParams before = ComputeCombatParams(state);
  ASSERT_EQ(before.attacks.size(), 2u);
  double swing_before = before.attacks[1].damage_per_hit[0];

  ASSERT_TRUE(state.character.LearnSkill(illusion, 1));
  CombatParams after = ComputeCombatParams(state);
  // The decoy's ignored defence lifts the character's own swing, which is what
  // a passive grant means -- a swing lever would have stayed with the decoy.
  EXPECT_GT(after.attacks[1].damage_per_hit[0], swing_before);
}

// Bolt Surplus's strike lands on the swings the character chooses between and
// nowhere else: not on the bare poke, which lands once, and not on a summon,
// which they never swung.
TEST(ComputeCombatParamsTest, AnExtraStrikeReachesOnlyMultiHitSwings) {
  Skill snipe;
  snipe.set_name("Snipe");
  snipe.set_kind(SKILL_KIND_ATTACK);
  snipe.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  snipe.set_max_level(30);
  snipe.set_max_enemies(1);
  snipe.set_lines(9);
  snipe.mutable_base()->set_skill_pct(1.00);
  Skill illusion;
  illusion.set_name("Arrow Illusion");
  illusion.set_kind(SKILL_KIND_AUTO_ATTACK);
  illusion.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  illusion.set_max_level(20);
  illusion.set_max_enemies(6);
  illusion.set_lines(4);
  illusion.set_cast_interval_seconds(3.0);
  illusion.mutable_base()->set_skill_pct(1.00);
  Skill surplus;
  surplus.set_name("Bolt Surplus");
  surplus.set_kind(SKILL_KIND_PASSIVE);
  surplus.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  surplus.set_max_level(10);
  surplus.mutable_base()->set_bonus_attack_lines(1);

  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}},
                  {{"snipe", snipe},
                   {"arrow_illusion", illusion},
                   {"bolt_surplus", surplus}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 3);
  ASSERT_TRUE(state.character.LearnSkill(snipe, 1));
  ASSERT_TRUE(state.character.LearnSkill(illusion, 1));

  CombatParams before = ComputeCombatParams(state);
  ASSERT_EQ(before.attacks.size(), 2u);
  ASSERT_EQ(before.auto_attacks.size(), 1u);
  double poke_before = before.attacks[0].damage_per_hit[0];
  double snipe_before = before.attacks[1].damage_per_hit[0];
  double pulse_before = before.auto_attacks[0].damage_per_hit[0];

  ASSERT_TRUE(state.character.LearnSkill(surplus, 1));
  CombatParams after = ComputeCombatParams(state);
  // Nine lines become ten; the poke's one line and the summon's four are left
  // exactly as they were.
  EXPECT_DOUBLE_EQ(after.attacks[1].damage_per_hit[0],
                   snipe_before * 10.0 / 9.0);
  EXPECT_DOUBLE_EQ(after.attacks[0].damage_per_hit[0], poke_before);
  EXPECT_DOUBLE_EQ(after.auto_attacks[0].damage_per_hit[0], pulse_before);
}

// An empowered swing lands its own second hit the way an ordinary one does:
// Empowered Snipe spends the mark Snipe left on the target it hits.
TEST(ComputeCombatParamsTest, AnEmpoweredFormLandsItsOwnSecondHit) {
  Skill snipe;
  snipe.set_name("Snipe");
  snipe.set_kind(SKILL_KIND_ATTACK);
  snipe.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  snipe.set_max_level(30);
  snipe.set_max_enemies(1);
  snipe.set_lines(9);
  snipe.mutable_base()->set_skill_pct(1.00);

  Skill greater;
  greater.set_name("Greater Empowered Arrows");
  greater.set_kind(SKILL_KIND_PASSIVE);
  greater.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  greater.set_max_level(20);
  EmpoweredForm* shot = greater.add_empowered_form();
  shot->set_skill_name("Snipe");
  shot->set_casts_per_trigger(4);
  shot->set_max_enemies(1);
  shot->set_lines(10);
  shot->mutable_base()->set_skill_pct(1.00);
  SwingHit* mark = shot->add_extra_hit();
  mark->set_label("Marked Target");
  mark->set_lines(5);
  mark->mutable_base()->set_skill_pct(2.00);

  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}},
                  {{"snipe", snipe}, {"greater_empowered_arrows", greater}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 2);
  ASSERT_TRUE(state.character.LearnSkill(snipe, 1));
  ASSERT_TRUE(state.character.LearnSkill(greater, 1));

  CombatParams params = ComputeCombatParams(state);
  ASSERT_EQ(params.attacks.size(), 2u);
  const AttackOption& shot_at = params.attacks[1];
  ASSERT_NE(shot_at.empowered, nullptr);
  // Ten lines of 100% and five of 200%, against the ordinary nine of 100%.
  EXPECT_DOUBLE_EQ(shot_at.empowered->damage_per_hit[0],
                   (20.0 / 9.0) * shot_at.damage_per_hit[0]);
  // Each half rolls on its own, so the mark is a group beside the swing's.
  EXPECT_EQ(shot_at.empowered->groups.size(), 2u);
}

// A skill clocked by swings landed goes on its own list, not beside the ones
// clocked by seconds: the fight has to count something for these and nothing
// for those.
TEST(ComputeCombatParamsTest, ASwingClockedSkillLandsOnTheTriggeredList) {
  Skill mirage;
  mirage.set_name("Speed Mirage");
  mirage.set_kind(SKILL_KIND_AUTO_ATTACK);
  mirage.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  mirage.set_max_level(20);
  mirage.set_max_enemies(6);
  mirage.set_lines(4);
  mirage.set_attacks_per_cast(4);
  mirage.mutable_base()->set_skill_pct(3.25);
  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}}, {{"speed_mirage", mirage}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 1);
  ASSERT_TRUE(state.character.LearnSkill(mirage, 1));

  CombatParams params = ComputeCombatParams(state);
  EXPECT_EQ(params.attacks.size(), 1u);  // the bare poke, and nothing else
  EXPECT_TRUE(params.auto_attacks.empty());
  ASSERT_EQ(params.triggered_attacks.size(), 1u);
  EXPECT_EQ(params.triggered_attacks[0].name, "Speed Mirage");
  EXPECT_EQ(params.triggered_attacks[0].attacks_per_cast, 4);
  EXPECT_EQ(params.triggered_attacks[0].max_enemies, 6);
  EXPECT_GT(params.triggered_attacks[0].damage_per_hit[0], 0.0);
  // Counted in swings, so the pacing band never touches it.
  EXPECT_DOUBLE_EQ(params.triggered_attacks[0].interval_seconds, 0.0);
}

// Speed Mirage II's shape: a passive that resets the clock of the skill it
// names, rather than adding to it. Strikes and reach sum; the clock replaces.
TEST(ComputeCombatParamsTest, ABoostCanResetTheClockItNames) {
  Skill mirage;
  mirage.set_name("Speed Mirage");
  mirage.set_kind(SKILL_KIND_AUTO_ATTACK);
  mirage.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  mirage.set_max_level(20);
  mirage.set_max_enemies(6);
  mirage.set_lines(4);
  mirage.set_attacks_per_cast(4);
  mirage.mutable_base()->set_skill_pct(3.25);
  Skill second;
  second.set_name("Speed Mirage II");
  second.set_kind(SKILL_KIND_PASSIVE);
  second.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  second.set_max_level(20);
  SkillBoost* boost = second.add_boost();
  boost->set_skill_name("Speed Mirage");
  boost->set_lines(12);
  boost->set_attacks_per_cast(7);

  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}},
                  {{"speed_mirage", mirage}, {"speed_mirage_ii", second}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 30);
  ASSERT_TRUE(state.character.LearnSkill(mirage, 1));

  ASSERT_EQ(ComputeCombatParams(state).triggered_attacks.size(), 1u);
  double one_line =
      ComputeCombatParams(state).triggered_attacks[0].damage_per_hit[0] / 4.0;

  ASSERT_TRUE(state.character.LearnSkill(second, 1));
  CombatParams params = ComputeCombatParams(state);
  ASSERT_EQ(params.triggered_attacks.size(), 1u);
  EXPECT_EQ(params.triggered_attacks[0].attacks_per_cast, 7);
  EXPECT_EQ(params.triggered_attacks[0].max_enemies, 6);
  EXPECT_DOUBLE_EQ(params.triggered_attacks[0].damage_per_hit[0],
                   16.0 * one_line);
}

// A swing that lands seven times as often is worth a seventh of an attack, so
// the skill it feeds fires at the same rate whichever swing is feeding it.
TEST(ComputeCombatParamsTest, ARapidSwingCountsForLessThanAWholeAttack) {
  Skill blaster;
  blaster.set_name("Arrow Blaster");
  blaster.set_kind(SKILL_KIND_ATTACK);
  blaster.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  blaster.set_max_level(20);
  blaster.set_hits_per_attack_count(7);
  blaster.mutable_base()->set_skill_pct(1.24);
  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}}, {{"arrow_blaster", blaster}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 1);
  ASSERT_TRUE(state.character.LearnSkill(blaster, 1));

  CombatParams params = ComputeCombatParams(state);
  ASSERT_EQ(params.attacks.size(), 2u);
  EXPECT_DOUBLE_EQ(params.attacks[0].count_weight, 1.0);  // the bare poke
  EXPECT_DOUBLE_EQ(params.attacks[1].count_weight, 1.0 / 7.0);
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
