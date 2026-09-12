#include "src/combat/encounter.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "src/character/character_stats.h"
#include "src/character/consumables.h"
#include "src/character/hyper_stats.h"
#include "src/character/progression.h"
#include "src/character/skill_placement.h"
#include "src/character/v_matrix.h"
#include "src/combat/constants.h"
#include "src/combat/damage.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/protos/boss.pb.h"
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

// A mob with weapon defence to ignore, so an Ignore DEF lever has something
// to cancel: MakeMob above leaves it at zero.
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
  Spawn* snail = map.add_spawns();
  snail->set_mob("snail");
  snail->set_count(2);
  Spawn* blue = map.add_spawns();
  blue->set_mob("blue_snail");
  blue->set_count(4);
  return map;
}

// The swing named, or null where the character has no such attack.
const AttackOption* FindAttack(const CombatParams& params,
                               const std::string& name) {
  for (const AttackOption& attack : params.attacks) {
    if (attack.name == name) {
      return &attack;
    }
  }
  return nullptr;
}

void EquipSwordAt(GameState& state, AttackSpeed speed) {
  EquipPrototype sword;
  sword.set_name("Sword");
  sword.set_equip_type(EQUIP_TYPE_ONE_HANDED_SWORD);
  sword.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  sword.set_attack_speed(speed);
  // Both halves, so the swing lands whatever job the starting character
  // happens to be: the encounter math must not depend on which it is.
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
  PlaceIn(skill, JOB_ADVANCEMENT_SWORDMAN);
  skill.set_max_level(1);
  skill.mutable_base()->set_attack_speed(stages);
  return skill;
}

// Where a named swing sits among the options, or -1 for one the character
// cannot swing.
int IndexOfAttack(const std::vector<AttackOption>& attacks,
                  const std::string& name) {
  for (int i = 0; i < static_cast<int>(attacks.size()); ++i) {
    if (attacks[i].name == name) {
      return i;
    }
  }
  return -1;
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
  PlaceIn(slash, JOB_ADVANCEMENT_SWORDMAN);
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

// A held swing is built as a full hold: its damage is every pulse and the
// strike it ends on, and its length is the pulses on their own clock. The
// skill's own delay is the FLOOR under that -- the shortest the player can let
// go -- and the pulses that fit inside it are the fewest a cast is worth.
TEST(ComputeCombatParamsTest, AHeldSwingIsPricedAsAFullHold) {
  Skill orb;
  orb.set_name("Lightning Orb");
  orb.set_kind(SKILL_KIND_ATTACK);
  PlaceIn(orb, JOB_ADVANCEMENT_SWORDMAN);
  orb.set_max_level(1);
  orb.set_max_enemies(8);
  orb.set_lines(15);
  orb.set_base_delay_ms(960);
  orb.mutable_base()->set_skill_pct(1.35);
  Channel* channel = orb.mutable_channel();
  channel->set_pulse_interval_ms(150);
  channel->set_max_pulses(12);
  channel->set_finish_delay_ms(200);
  channel->set_damage_taken_pct(0.5);
  channel->set_charge_seconds(11.0);
  channel->set_max_charges(5);
  channel->set_pulses_per_charge(4);
  channel->mutable_finish()->set_lines(15);
  channel->mutable_finish()->mutable_base()->set_skill_pct(7.02);
  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}}, {{"lightning_orb", orb}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 1);
  ASSERT_TRUE(state.character.LearnSkill(orb, 1));

  CombatParams params = ComputeCombatParams(state);
  int held = IndexOfAttack(params.attacks, "Lightning Orb");
  ASSERT_GE(held, 0);
  const AttackOption& attack = params.attacks[held];
  double speed = GameSpeedFactor(state.character.proto().level());
  EXPECT_EQ(attack.channel.pulses, 12);
  EXPECT_EQ(attack.channel.min_pulses, 5);
  EXPECT_DOUBLE_EQ(attack.channel.damage_taken_pct, 0.5);
  EXPECT_FALSE(attack.channel.holds_full);
  EXPECT_DOUBLE_EQ(attack.channel.pulse_seconds, 0.15 * speed);
  EXPECT_DOUBLE_EQ(attack.channel.finish_seconds, 0.2 * speed);
  // The bank is stretched with every other clock here; what it buys is not.
  EXPECT_DOUBLE_EQ(attack.channel.charge_seconds, 11.0 * speed);
  EXPECT_EQ(attack.channel.max_charges, 5);
  EXPECT_EQ(attack.channel.pulses_per_charge, 4);
  // Twelve pulses and the finish, which is longer than the floor.
  EXPECT_DOUBLE_EQ(attack.swing_seconds, 2.0 * speed);
  EXPECT_DOUBLE_EQ(HoldSeconds(attack.channel, 5), attack.channel.min_seconds);
  // One pulse is the first block of lines; the whole hold is twelve of them
  // plus the burst, which is what the swing is weighed at.
  ASSERT_EQ(attack.groups.size(), 2u);
  EXPECT_NEAR(attack.damage_per_hit[0],
              12 * attack.groups[0].damage[0] + attack.groups[1].damage[0],
              1e-6);
}

// Trickblade's shape: a skill that states a wound, hands the swings that leave
// it their depth by name, and carries the heavier form it lands while one
// stands. The form is built off the same learned level and priced as a swing
// of its own.
TEST(ComputeCombatParamsTest, AWoundIsHandedToTheSwingsThatLeaveIt) {
  Skill blow;
  blow.set_name("Sonic Blow");
  blow.set_kind(SKILL_KIND_ATTACK);
  PlaceIn(blow, JOB_ADVANCEMENT_SWORDMAN);
  blow.set_max_level(1);
  blow.set_lines(2);
  blow.mutable_base()->set_skill_pct(1.0);

  Skill blade;
  blade.set_name("Trickblade");
  blade.set_kind(SKILL_KIND_ATTACK);
  PlaceIn(blade, JOB_ADVANCEMENT_SWORDMAN, /*order=*/2);
  blade.set_max_level(1);
  blade.set_max_enemies(10);
  blade.set_lines(5);
  blade.mutable_base()->set_skill_pct(7.02);
  Wound* wound = blade.mutable_wound();
  wound->set_duration_seconds(10.0);
  wound->set_max_stacks(3);
  Wound::Source* source = wound->add_source();
  source->set_skill_name("Sonic Blow");
  source->set_stacks(3);
  WoundForm* form = wound->mutable_form();
  form->set_label("Trickblade: Finish");
  form->set_max_enemies(1);
  form->set_lines(7);
  form->set_casts(5);
  form->set_cooldown_seconds(20.0);
  form->mutable_base()->set_skill_pct(8.58);

  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}},
                  {{"sonic_blow", blow}, {"trickblade", blade}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 2);
  ASSERT_TRUE(state.character.LearnSkill(blow, 1));
  ASSERT_TRUE(state.character.LearnSkill(blade, 1));

  CombatParams params = ComputeCombatParams(state);
  double speed = GameSpeedFactor(state.character.proto().level());
  // The swing that leaves it knows nothing about it; the skill stating it does
  // the telling.
  const AttackOption& leaves =
      params.attacks[IndexOfAttack(params.attacks, "Sonic Blow")];
  EXPECT_EQ(leaves.wound_stacks, 3);
  EXPECT_EQ(leaves.wound_max_stacks, 3);
  EXPECT_DOUBLE_EQ(leaves.wound_seconds, 10.0 * speed);
  EXPECT_EQ(leaves.wound_form, nullptr);

  const AttackOption& reads =
      params.attacks[IndexOfAttack(params.attacks, "Trickblade")];
  EXPECT_EQ(reads.wound_stacks, 0);  // it leaves none of its own
  EXPECT_EQ(reads.wound_max_stacks, 3);
  ASSERT_NE(reads.wound_form, nullptr);
  EXPECT_EQ(reads.wound_form->name, "Trickblade: Finish");
  EXPECT_EQ(reads.wound_form->max_enemies, 1);
  EXPECT_DOUBLE_EQ(reads.wound_form->cooldown_seconds, 20.0 * speed);
  // Five strikes of seven lines at 858%, against the press's five at 702%.
  EXPECT_GT(reads.wound_form->damage_per_hit[0], reads.damage_per_hit[0]);
}

// Calamitous Cyclone's shape: a hold that heals on both clocks. The swing's
// own recovery is ONE pulse's, so it moves to the hold and a cast let go early
// is worth less of the pool; the strike it ends on pays per line of every one
// of its strikes.
TEST(ComputeCombatParamsTest, AHoldPaysItsRecoveryPerPulseAndPerFinishLine) {
  Skill cyclone;
  cyclone.set_name("Calamitous Cyclone");
  cyclone.set_kind(SKILL_KIND_ATTACK);
  PlaceIn(cyclone, JOB_ADVANCEMENT_SWORDMAN);
  cyclone.set_max_level(1);
  cyclone.set_max_enemies(12);
  cyclone.set_lines(8);
  cyclone.set_base_delay_ms(870);
  cyclone.mutable_base()->set_skill_pct(2.64);
  cyclone.mutable_base()->set_hp_recover_pct(0.01);
  Channel* channel = cyclone.mutable_channel();
  channel->set_pulse_interval_ms(140);
  channel->set_max_pulses(26);
  channel->set_finish_delay_ms(720);
  SwingHit* finish = channel->mutable_finish();
  finish->set_lines(15);
  finish->set_casts(8);
  finish->mutable_base()->set_skill_pct(5.87);
  finish->mutable_base()->set_hp_recover_pct(0.10);
  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}},
                  {{"calamitous_cyclone", cyclone}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 1);
  ASSERT_TRUE(state.character.LearnSkill(cyclone, 1));

  CombatParams params = ComputeCombatParams(state);
  int held = IndexOfAttack(params.attacks, "Calamitous Cyclone");
  ASSERT_GE(held, 0);
  const AttackOption& attack = params.attacks[held];
  EXPECT_DOUBLE_EQ(attack.channel.hp_recover_pct, 0.01);
  // 10% for each of the 15 final attacks in each of the 8 shockwaves, which is
  // the whole pool over twelve times and is what makes the cast a full heal.
  EXPECT_DOUBLE_EQ(attack.hp_recover_pct, 12.0);
}

// Grand Guardian's shape: a hold that grows partway through, so the full hold
// is the opening run plus the stronger one rather than one pulse multiplied.
TEST(ComputeCombatParamsTest, AHoldCanGrowPartwayThrough) {
  Skill cross;
  cross.set_name("Grand Guardian");
  cross.set_kind(SKILL_KIND_ATTACK);
  PlaceIn(cross, JOB_ADVANCEMENT_SWORDMAN);
  cross.set_max_level(1);
  cross.set_max_enemies(12);
  cross.set_lines(12);
  cross.set_base_delay_ms(1680);
  cross.mutable_base()->set_skill_pct(2.54);
  Channel* channel = cross.mutable_channel();
  channel->set_pulse_interval_ms(150);
  channel->set_max_pulses(23);
  channel->set_small_pulses(7);
  channel->set_finish_delay_ms(780);
  SwingHit* grown = channel->mutable_grown();
  grown->set_label("Grand Cross");
  grown->set_lines(12);
  grown->mutable_base()->set_skill_pct(4.89);

  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}}, {{"grand_guardian", cross}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 1);
  ASSERT_TRUE(state.character.LearnSkill(cross, 1));

  CombatParams params = ComputeCombatParams(state);
  int held = IndexOfAttack(params.attacks, "Grand Guardian");
  ASSERT_GE(held, 0);
  const AttackOption& attack = params.attacks[held];
  EXPECT_EQ(attack.channel.pulses, 23);
  EXPECT_EQ(attack.channel.small_pulses, 7);
  ASSERT_FALSE(attack.channel.grown.damage.empty());
  // The grown pulse is priced on its own multiplier and kept off the swing's
  // groups, which are read as one pulse and then the finish.
  EXPECT_EQ(attack.groups.size(), 1u);
  double one = attack.groups.front().damage[0];
  EXPECT_NEAR(attack.channel.grown.damage[0] / one, 4.89 / 2.54, 0.01);
  // Seven of the opening pulse and sixteen of the stronger one -- not 23 of
  // either.
  EXPECT_NEAR(attack.damage_per_hit[0],
              7.0 * one + 16.0 * attack.channel.grown.damage[0], 1e-6);
}

// Glacial Fury pays magic attack per Freeze Stack to ICE swings and to nothing
// else, so the gain is written onto the ice swing alone -- and it is a share of
// that swing, since damage is linear in the attack behind it.
TEST(ComputeCombatParamsTest, OnlyAnIceSwingCollectsTheStackedMagicAttack) {
  Skill crush;
  crush.set_name("Freezing Crush");
  crush.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(crush, JOB_ADVANCEMENT_SWORDMAN);
  crush.set_max_level(1);
  crush.set_freeze_stack_cap(5);
  Skill fury;
  fury.set_name("Glacial Fury");
  fury.set_kind(SKILL_KIND_ACTIVE);
  PlaceIn(fury, JOB_ADVANCEMENT_SWORDMAN);
  fury.set_max_level(1);
  fury.set_cooldown_seconds(60.0);
  fury.mutable_buff()->set_duration_seconds(20.0);
  fury.mutable_buff()->mutable_base()->set_freeze_stack_cap_bonus(8);
  fury.mutable_buff()->mutable_base()->set_magic_attack_per_freeze_stack(5);
  Skill beam;
  beam.set_name("Cold Beam");
  beam.set_kind(SKILL_KIND_ATTACK);
  beam.add_tags(SKILL_TAG_ICE);
  PlaceIn(beam, JOB_ADVANCEMENT_SWORDMAN);
  beam.set_max_level(1);
  beam.mutable_base()->set_skill_pct(1.0);
  Skill bolt = beam;
  bolt.set_name("Thunder Bolt");
  bolt.clear_tags();
  bolt.add_tags(SKILL_TAG_LIGHTNING);
  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}},
                  {{"cold_beam", beam},
                   {"freezing_crush", crush},
                   {"glacial_fury", fury},
                   {"thunder_bolt", bolt}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 4);
  ASSERT_TRUE(state.character.LearnSkill(crush, 1));
  ASSERT_TRUE(state.character.LearnSkill(fury, 1));
  ASSERT_TRUE(state.character.LearnSkill(beam, 1));
  ASSERT_TRUE(state.character.LearnSkill(bolt, 1));

  CombatParams params = ComputeCombatParams(state);
  // Down, the buff pays nothing at all and the pile is Freezing Crush's own.
  EXPECT_EQ(params.FreezeCap(0), 5);
  int ice = IndexOfAttack(params.attacks, "Cold Beam");
  int lightning = IndexOfAttack(params.attacks, "Thunder Bolt");
  ASSERT_GE(ice, 0);
  ASSERT_GE(lightning, 0);
  EXPECT_DOUBLE_EQ(params.attacks[ice].freeze_matt_gain, 0.0);

  // Up, the cap is 13 and the ice swing gains 5 magic attack a stack against
  // whatever it is swinging with.
  ASSERT_EQ(params.buffed.size(), 1u);
  EXPECT_EQ(params.FreezeCap(1), 13);
  EXPECT_GT(params.Attacks(1)[ice].freeze_matt_gain, 0.0);
  EXPECT_DOUBLE_EQ(params.Attacks(1)[lightning].freeze_matt_gain, 0.0);
}

// A skill the book has replaced stops being an option to swing, not only a
// pile of levers that stops paying: the replacement states the whole of it, so
// offering both would be offering one skill twice.
TEST(ComputeCombatParamsTest, ASupersededSwingIsNotOffered) {
  Skill slash;
  slash.set_name("Slash Blast");
  slash.set_kind(SKILL_KIND_ATTACK);
  PlaceIn(slash, JOB_ADVANCEMENT_SWORDMAN);
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
  PlaceIn(burst, JOB_ADVANCEMENT_SWORDMAN);
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
  PlaceIn(piercing, JOB_ADVANCEMENT_SWORDMAN);
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
  PlaceIn(burst, JOB_ADVANCEMENT_SWORDMAN);
  burst.set_max_level(20);
  burst.set_max_enemies(6);
  burst.set_lines(6);
  burst.set_lead_lines(1);
  burst.mutable_base()->set_skill_pct(0.48);
  burst.mutable_base()->set_lead_pct(4.08);
  Skill mist;
  mist.set_name("Poison Mist");
  mist.set_kind(SKILL_KIND_AUTO_ATTACK);
  PlaceIn(mist, JOB_ADVANCEMENT_SWORDMAN);
  mist.set_max_level(20);
  mist.set_max_enemies(6);
  mist.set_cast_interval_seconds(1.0);
  mist.mutable_base()->set_skill_pct(1.26);
  Skill partner;
  partner.set_name("Shadow Partner");
  partner.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(partner, JOB_ADVANCEMENT_SWORDMAN);
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

// A swing GMS keeps the shadow off takes none of it anywhere: not its own
// lines, not its opening hit, and not the extra hit riding it -- a hit cannot
// be copied by a partner that never came. See Skill.skips_mirror.
TEST(ComputeCombatParamsTest, ASwingTheShadowSkipsTakesNoneOfItAnywhere) {
  Skill partner;
  partner.set_name("Shadow Partner");
  partner.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(partner, JOB_ADVANCEMENT_SWORDMAN);
  partner.set_max_level(20);
  partner.mutable_base()->set_mirror_line_pct(0.70);

  double swing[2] = {0.0, 0.0};
  double lead[2] = {0.0, 0.0};
  double wide[2] = {0.0, 0.0};
  for (int pass = 0; pass < 2; ++pass) {
    Skill formation;
    formation.set_name("Slash Shadow Formation");
    formation.set_kind(SKILL_KIND_ATTACK);
    PlaceIn(formation, JOB_ADVANCEMENT_SWORDMAN);
    formation.set_max_level(30);
    formation.set_max_enemies(6);
    formation.set_lines(11);
    formation.set_lead_lines(2);
    formation.set_skips_mirror(pass == 0);
    formation.mutable_base()->set_skill_pct(7.33);
    formation.mutable_base()->set_lead_pct(3.00);
    SwingHit* leader = formation.add_extra_hit();
    leader->set_label("Leader's Slash");
    leader->set_lines(15);
    leader->set_max_enemies(8);
    leader->mutable_base()->set_skill_pct(8.20);

    GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                    {{"field", TwoSnailMap()}},
                    {{"formation", formation}, {"shadow_partner", partner}});
    state.current_map = "field";
    EquipSword(state);
    GrantFirstJobSp(state, 2);
    ASSERT_TRUE(state.character.LearnSkill(formation, 1));
    ASSERT_TRUE(state.character.LearnSkill(partner, 1));

    CombatParams params = ComputeCombatParams(state);
    ASSERT_EQ(params.attacks.size(), 2u);
    const AttackOption& attack = params.attacks[1];
    ASSERT_FALSE(attack.wide_hit_damage.empty());
    swing[pass] = attack.damage_per_hit[0];
    lead[pass] = attack.lead_damage[0];
    wide[pass] = attack.wide_hit_damage[0];
  }
  // The shadow left on is worth 70% of every line of all three halves, which
  // is what the skipped pass goes without.
  EXPECT_NEAR(swing[1] / swing[0], 1.70, 1e-9);
  EXPECT_NEAR(lead[1] / lead[0], 1.70, 1e-9);
  EXPECT_NEAR(wide[1] / wide[0], 1.70, 1e-9);
}

// A meso falls out per LINE the swing lands, not per enemy it reaches, so a
// four-line swing is worth four times what a one-line swing is. And a skill on
// a clock of its own knocks none loose at all: the character did not swing it.
TEST(ComputeCombatParamsTest, MesosDropPerLineAndOnlyFromWhatIsSwung) {
  Skill carnival;
  carnival.set_name("Midnight Carnival");
  carnival.set_kind(SKILL_KIND_ATTACK);
  PlaceIn(carnival, JOB_ADVANCEMENT_SWORDMAN);
  carnival.set_max_level(20);
  carnival.set_max_enemies(8);
  carnival.set_lines(4);
  carnival.mutable_base()->set_skill_pct(1.72);
  Skill flare;
  flare.set_name("Dark Flare");
  flare.set_kind(SKILL_KIND_AUTO_ATTACK);
  PlaceIn(flare, JOB_ADVANCEMENT_SWORDMAN);
  flare.set_max_level(20);
  flare.set_max_enemies(3);
  flare.set_cast_interval_seconds(1.5);
  flare.mutable_base()->set_skill_pct(2.08);
  Skill pocket;
  pocket.set_name("Pick Pocket");
  pocket.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(pocket, JOB_ADVANCEMENT_SWORDMAN);
  pocket.set_max_level(10);
  pocket.mutable_base()->set_meso_drop_chance(0.30);
  Skill explosion;
  explosion.set_name("Meso Explosion");
  explosion.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(explosion, JOB_ADVANCEMENT_SWORDMAN);
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

// A swing can give up part of the chance it shakes coins loose with, and only
// that swing does: the poke beside it rolls the whole of Pick Pocket. A share
// rather than a number of points, so it is half at every level of the skill
// granting the chance.
TEST(ComputeCombatParamsTest, ASwingCanShakeFewerMesosLoose) {
  Skill stab;
  stab.set_name("Cruel Stab");
  stab.set_kind(SKILL_KIND_ATTACK);
  PlaceIn(stab, JOB_ADVANCEMENT_SWORDMAN);
  stab.set_max_level(30);
  stab.set_max_enemies(8);
  stab.set_lines(6);
  stab.mutable_base()->set_skill_pct(1.92);
  stab.mutable_base()->set_meso_drop_cut(0.50);
  Skill pocket;
  pocket.set_name("Pick Pocket");
  pocket.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(pocket, JOB_ADVANCEMENT_SWORDMAN);
  pocket.set_max_level(10);
  pocket.mutable_base()->set_meso_drop_chance(0.12);
  pocket.mutable_per_level()->set_meso_drop_chance(0.02);
  Skill explosion;
  explosion.set_name("Meso Explosion");
  explosion.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(explosion, JOB_ADVANCEMENT_SWORDMAN);
  explosion.set_max_level(20);
  explosion.set_lines(2);
  explosion.mutable_base()->set_meso_hit_pct(1.00);

  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}},
                  {{"cruel_stab", stab},
                   {"pick_pocket", pocket},
                   {"meso_explosion", explosion}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 8);
  ASSERT_TRUE(state.character.LearnSkill(stab, 1));
  ASSERT_TRUE(state.character.LearnSkill(pocket, 5));
  ASSERT_TRUE(state.character.LearnSkill(explosion, 1));

  CombatParams params = ComputeCombatParams(state);
  ASSERT_EQ(params.attacks.size(), 2u);
  const AttackOption& poke = params.attacks[0];
  const AttackOption& swing = params.attacks[1];
  ASSERT_EQ(poke.final_attack_rolls.size(), 1u);
  ASSERT_EQ(swing.final_attack_rolls.size(), 1u);
  EXPECT_NEAR(poke.final_attack_rolls[0].chance, 0.20, 1e-9);
  EXPECT_NEAR(swing.final_attack_rolls[0].chance, 0.10, 1e-9);
}

// A swing can give up part of the chance it sets the extra hit off with, and
// only that swing does. Shurrikane's, and the same bargain the meso cut above
// makes -- a share, so it holds at every level of the Final Attack it cuts.
TEST(ComputeCombatParamsTest, ASwingCanSetOffFewerFinalAttacks) {
  Skill shuriken;
  shuriken.set_name("Shurrikane");
  shuriken.set_kind(SKILL_KIND_ATTACK);
  PlaceIn(shuriken, JOB_ADVANCEMENT_SWORDMAN);
  shuriken.set_max_level(30);
  shuriken.set_max_enemies(6);
  shuriken.set_lines(7);
  shuriken.mutable_base()->set_skill_pct(2.96);
  shuriken.mutable_base()->set_final_attack_chance_cut(0.60);
  Skill mark;
  mark.set_name("Assassin's Mark");
  mark.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(mark, JOB_ADVANCEMENT_SWORDMAN);
  mark.set_max_level(10);
  mark.mutable_base()->set_final_attack_chance(0.22);
  mark.mutable_base()->set_final_attack_pct(0.78);
  mark.mutable_per_level()->set_final_attack_chance(0.02);

  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}},
                  {{"shurrikane", shuriken}, {"assassins_mark", mark}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 6);
  ASSERT_TRUE(state.character.LearnSkill(shuriken, 1));
  ASSERT_TRUE(state.character.LearnSkill(mark, 5));

  CombatParams params = ComputeCombatParams(state);
  ASSERT_EQ(params.attacks.size(), 2u);
  const AttackOption& poke = params.attacks[0];
  const AttackOption& swing = params.attacks[1];
  ASSERT_EQ(poke.final_attack_rolls.size(), 1u);
  ASSERT_EQ(swing.final_attack_rolls.size(), 1u);
  EXPECT_NEAR(poke.final_attack_rolls[0].chance, 0.30, 1e-9);
  EXPECT_NEAR(swing.final_attack_rolls[0].chance, 0.12, 1e-9);
  // What the extra hit is worth follows the chance, so the sims price it too.
  EXPECT_DOUBLE_EQ(swing.final_attack_damage[0],
                   swing.final_attack_rolls[0].damage[0] * 0.12);
}

// A Reinforce aimed at the passive a Final Attack belongs to lands on the extra
// hits and nowhere else -- the swing that set them off is worth what it was.
TEST(ComputeCombatParamsTest, AFinalAttackReinforceLandsOnTheExtraHitsAlone) {
  Skill final_attack;
  final_attack.set_name("Advanced Final Attack");
  final_attack.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(final_attack, JOB_ADVANCEMENT_SWORDMAN);
  final_attack.set_max_level(30);
  final_attack.mutable_base()->set_final_attack_chance(0.60);
  final_attack.mutable_base()->set_final_attack_pct(1.70);
  Skill hyper;
  hyper.set_name("Advanced Final Attack - Reinforce");
  hyper.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(hyper, JOB_ADVANCEMENT_SWORDMAN);
  hyper.set_max_level(1);
  SkillBoost* boost = hyper.add_boost();
  boost->set_skill_name("Advanced Final Attack");
  boost->mutable_effect()->set_damage_pct(0.10);

  Mob snail = MakeMob("Snail", 15);
  std::map<std::string, Skill> book = {{"advanced_final_attack", final_attack}};
  GameState bare({}, {}, {}, {{"snail", snail}}, {{"field", TwoSnailMap()}},
                 book);
  bare.current_map = "field";
  EquipSword(bare);
  GrantFirstJobSp(bare, 30);
  ASSERT_TRUE(bare.character.LearnSkill(final_attack, 30));

  book["hyper"] = hyper;
  GameState boosted({}, {}, {}, {{"snail", snail}}, {{"field", TwoSnailMap()}},
                    book);
  boosted.current_map = "field";
  EquipSword(boosted);
  GrantFirstJobSp(boosted, 31);
  ASSERT_TRUE(boosted.character.LearnSkill(final_attack, 30));
  ASSERT_TRUE(boosted.character.LearnSkill(hyper, 1));

  CombatParams bare_params = ComputeCombatParams(bare);
  CombatParams paid_params = ComputeCombatParams(boosted);
  const AttackOption& plain = bare_params.attacks[0];
  const AttackOption& paid = paid_params.attacks[0];
  ASSERT_FALSE(plain.final_attack_damage.empty());
  ASSERT_FALSE(paid.final_attack_damage.empty());
  EXPECT_GT(paid.final_attack_damage[0], plain.final_attack_damage[0]);
  EXPECT_NEAR(paid.damage_per_hit[0], plain.damage_per_hit[0], 1e-9);
  // The chance is untouched by a Reinforce: only Opportunity moves that.
  ASSERT_EQ(paid.final_attack_rolls.size(), 1u);
  EXPECT_NEAR(paid.final_attack_rolls[0].chance, 0.60, 1e-9);
}

// Blood Money brands the coins rather than the Shadower: the throw hits a boss
// harder and every swing that knocked it loose is worth exactly what it was.
TEST(ComputeCombatParamsTest, ABrandedMesoHitsABossHarder) {
  Skill pocket;
  pocket.set_name("Pick Pocket");
  pocket.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(pocket, JOB_ADVANCEMENT_SWORDMAN);
  pocket.set_max_level(10);
  pocket.mutable_base()->set_meso_drop_chance(0.30);
  Skill explosion;
  explosion.set_name("Meso Explosion");
  explosion.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(explosion, JOB_ADVANCEMENT_SWORDMAN);
  explosion.set_max_level(20);
  explosion.set_lines(2);
  explosion.mutable_base()->set_meso_hit_pct(1.00);
  Skill money;
  money.set_name("Blood Money");
  money.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(money, JOB_ADVANCEMENT_SWORDMAN);
  money.set_max_level(20);
  SkillBoost* brand = money.add_boost();
  brand->set_skill_name("Meso Explosion");
  brand->mutable_effect()->set_boss_pct(0.50);

  Mob snail = MakeMob("Snail", 15);
  snail.set_boss(true);
  std::map<std::string, Skill> book = {{"pick_pocket", pocket},
                                       {"meso_explosion", explosion}};
  GameState bare({}, {}, {}, {{"snail", snail}}, {{"field", TwoSnailMap()}},
                 book);
  bare.current_map = "field";
  EquipSword(bare);
  GrantFirstJobSp(bare, 4);
  ASSERT_TRUE(bare.character.LearnSkill(pocket, 1));
  ASSERT_TRUE(bare.character.LearnSkill(explosion, 1));

  book["blood_money"] = money;
  GameState branded({}, {}, {}, {{"snail", snail}}, {{"field", TwoSnailMap()}},
                    book);
  branded.current_map = "field";
  EquipSword(branded);
  GrantFirstJobSp(branded, 5);
  ASSERT_TRUE(branded.character.LearnSkill(pocket, 1));
  ASSERT_TRUE(branded.character.LearnSkill(explosion, 1));
  ASSERT_TRUE(branded.character.LearnSkill(money, 1));

  CombatParams bare_params = ComputeCombatParams(bare);
  CombatParams paid_params = ComputeCombatParams(branded);
  ASSERT_EQ(bare_params.attacks.size(), 1u);
  const AttackOption& plain = bare_params.attacks[0];
  const AttackOption& paid = paid_params.attacks[0];
  ASSERT_FALSE(plain.final_attack_damage.empty());
  ASSERT_FALSE(paid.final_attack_damage.empty());
  EXPECT_NEAR(paid.final_attack_damage[0] / plain.final_attack_damage[0], 1.50,
              1e-9);
  // The swing itself is untouched.
  EXPECT_NEAR(paid.damage_per_hit[0], plain.damage_per_hit[0], 1e-9);
}

// GMS pays a thrown coin percentage POINTS against anything that is not a
// boss, and the points ride the coin rather than the character: an ordinary
// monster takes them and a boss takes none.
TEST(ComputeCombatParamsTest, AThrownMesoPaysPointsToAnOrdinaryMonster) {
  Skill pocket;
  pocket.set_name("Pick Pocket");
  pocket.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(pocket, JOB_ADVANCEMENT_SWORDMAN);
  pocket.set_max_level(10);
  pocket.mutable_base()->set_meso_drop_chance(0.30);
  Skill explosion;
  explosion.set_name("Meso Explosion");
  explosion.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(explosion, JOB_ADVANCEMENT_SWORDMAN);
  explosion.set_max_level(20);
  explosion.set_lines(2);
  explosion.mutable_base()->set_meso_hit_pct(1.00);
  Skill pointed = explosion;
  pointed.mutable_base()->set_normal_skill_pct(0.05);

  // The same pair twice over, once against an ordinary snail and once against
  // a boss, with and without the points.
  auto meso_damage = [&](bool boss, const Skill& thrown) {
    Mob snail = MakeMob("Snail", 15);
    snail.set_boss(boss);
    GameState state({}, {}, {}, {{"snail", snail}}, {{"field", TwoSnailMap()}},
                    {{"pick_pocket", pocket}, {"meso_explosion", thrown}});
    state.current_map = "field";
    EquipSword(state);
    GrantFirstJobSp(state, 4);
    EXPECT_TRUE(state.character.LearnSkill(pocket, 1));
    EXPECT_TRUE(state.character.LearnSkill(thrown, 1));
    CombatParams params = ComputeCombatParams(state);
    EXPECT_EQ(params.attacks.size(), 1u);
    EXPECT_FALSE(params.attacks[0].final_attack_damage.empty());
    return params.attacks[0].final_attack_damage[0];
  };

  // Five points on each of the coin's two lines, over a coin worth 200%: a
  // twentieth more, and not a share of anything the character carries.
  EXPECT_NEAR(meso_damage(false, pointed) / meso_damage(false, explosion), 1.05,
              1e-9);
  // A boss collects none of it.
  EXPECT_NEAR(meso_damage(true, pointed) / meso_damage(true, explosion), 1.00,
              1e-9);
}

// Meso Explosion's hypers brand the coins too: plain damage and ignored
// defence aimed at a skill that is thrown rather than swung reach the throw
// alone, and the swing that shook it loose is worth exactly what it was.
TEST(ComputeCombatParamsTest,
     ABoostedMesoCarriesItsOwnDamageAndIgnoredDefense) {
  Skill pocket;
  pocket.set_name("Pick Pocket");
  pocket.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(pocket, JOB_ADVANCEMENT_SWORDMAN);
  pocket.set_max_level(10);
  pocket.mutable_base()->set_meso_drop_chance(0.30);
  Skill explosion;
  explosion.set_name("Meso Explosion");
  explosion.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(explosion, JOB_ADVANCEMENT_SWORDMAN);
  explosion.set_max_level(20);
  explosion.mutable_base()->set_meso_hit_pct(1.00);
  Skill hyper;
  hyper.set_name("Meso Explosion - Reinforce");
  hyper.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(hyper, JOB_ADVANCEMENT_SWORDMAN);
  hyper.set_max_level(1);
  SkillBoost* boost = hyper.add_boost();
  boost->set_skill_name("Meso Explosion");
  boost->mutable_effect()->set_damage_pct(0.20);
  boost->mutable_effect()->set_ied_pct(1.00);

  Mob armoured = MakeArmouredMob("Snail", 15, 100);
  std::map<std::string, Skill> book = {{"pick_pocket", pocket},
                                       {"meso_explosion", explosion}};
  GameState bare({}, {}, {}, {{"snail", armoured}}, {{"field", TwoSnailMap()}},
                 book);
  bare.current_map = "field";
  EquipSword(bare);
  GrantFirstJobSp(bare, 4);
  ASSERT_TRUE(bare.character.LearnSkill(pocket, 1));
  ASSERT_TRUE(bare.character.LearnSkill(explosion, 1));

  book["meso_explosion_reinforce"] = hyper;
  GameState boosted({}, {}, {}, {{"snail", armoured}},
                    {{"field", TwoSnailMap()}}, book);
  boosted.current_map = "field";
  EquipSword(boosted);
  GrantFirstJobSp(boosted, 5);
  ASSERT_TRUE(boosted.character.LearnSkill(pocket, 1));
  ASSERT_TRUE(boosted.character.LearnSkill(explosion, 1));
  ASSERT_TRUE(boosted.character.LearnSkill(hyper, 1));

  CombatParams bare_params = ComputeCombatParams(bare);
  CombatParams boosted_params = ComputeCombatParams(boosted);
  ASSERT_EQ(bare_params.attacks.size(), 1u);
  const AttackOption& plain = bare_params.attacks[0];
  const AttackOption& paid = boosted_params.attacks[0];
  ASSERT_FALSE(plain.final_attack_damage.empty());
  ASSERT_FALSE(paid.final_attack_damage.empty());
  // Both levers land, and neither reaches the swing.
  EXPECT_GT(paid.final_attack_damage[0], plain.final_attack_damage[0] * 1.20);
  EXPECT_NEAR(paid.damage_per_hit[0], plain.damage_per_hit[0], 1e-9);
}

// Nothing but a skill saying so gives a swing an opening hit.
TEST(ComputeCombatParamsTest, AnOrdinarySwingCarriesNoOpeningHit) {
  Skill slash;
  slash.set_name("Slash Blast");
  slash.set_kind(SKILL_KIND_ATTACK);
  PlaceIn(slash, JOB_ADVANCEMENT_SWORDMAN);
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
  PlaceIn(slash, JOB_ADVANCEMENT_SWORDMAN);
  slash.set_max_level(20);
  slash.mutable_base()->set_skill_pct(1.0);
  slash.mutable_per_level()->set_skill_pct(1.0);
  Skill orders;
  orders.set_name("Combat Orders");
  orders.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(orders, JOB_ADVANCEMENT_SWORDMAN);
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
  PlaceIn(heal, JOB_ADVANCEMENT_SWORDMAN);
  heal.set_max_level(10);
  heal.set_base_delay_ms(600);
  heal.mutable_base()->set_heal_pct(0.10);
  heal.mutable_per_level()->set_heal_pct(0.10);
  Skill final_attack;
  final_attack.set_name("Final Attack");
  final_attack.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(final_attack, JOB_ADVANCEMENT_SWORDMAN);
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
  PlaceIn(wide, JOB_ADVANCEMENT_SWORDMAN);
  wide.set_max_level(30);
  wide.set_base_delay_ms(900);
  wide.set_max_enemies(15);
  wide.mutable_base()->set_skill_pct(3.01);
  wide.set_final_attack_max_enemies(1);
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
  ASSERT_EQ(swing.per_swing_final_attack_rolls.size(), 1u);
  EXPECT_NEAR(swing.per_swing_final_attack_rolls[0].chance, 0.60, 1e-9);
  EXPECT_EQ(swing.per_swing_final_attack_rolls[0].count, 1);
  ASSERT_FALSE(swing.per_swing_final_attack_damage.empty());
  EXPECT_DOUBLE_EQ(swing.per_swing_final_attack_damage[0],
                   swing.per_swing_final_attack_rolls[0].damage[0] * 0.60);
  EXPECT_EQ(swing.per_swing_final_attack_enemies, 1);
}

// Split Shot's shape: a buff whose whole grant is a Final Attack, gated on the
// group the swing carries and landing on ten enemies behind a swing that
// reaches one. Its own tag is what tells the two attacks apart.
TEST(ComputeCombatParamsTest, ABuffCanGrantAFinalAttackWithItsOwnReach) {
  Skill snipe;
  snipe.set_name("Snipe");
  snipe.set_kind(SKILL_KIND_ATTACK);
  PlaceIn(snipe, JOB_ADVANCEMENT_SWORDMAN);
  snipe.set_max_level(30);
  snipe.set_base_delay_ms(660);
  snipe.set_max_enemies(1);
  snipe.add_tags(SKILL_TAG_MARKSMANSHIP);
  snipe.mutable_base()->set_skill_pct(3.24);

  Skill sweep;
  sweep.set_name("High Speed Shot");
  sweep.set_kind(SKILL_KIND_ATTACK);
  PlaceIn(sweep, JOB_ADVANCEMENT_SWORDMAN);
  sweep.set_max_level(30);
  sweep.set_base_delay_ms(660);
  sweep.set_max_enemies(12);
  sweep.mutable_base()->set_skill_pct(4.50);

  Skill split;
  split.set_name("Split Shot");
  split.set_kind(SKILL_KIND_ACTIVE);
  PlaceIn(split, JOB_ADVANCEMENT_SWORDMAN);
  split.set_max_level(30);
  split.set_base_delay_ms(120);
  split.set_follows_skill_tag(SKILL_TAG_MARKSMANSHIP);
  split.set_final_attack_max_enemies(10);
  split.mutable_buff()->set_duration_seconds(72.0);
  split.mutable_buff()->mutable_base()->set_final_attack_chance(1.0);
  split.mutable_buff()->mutable_base()->set_final_attack_pct(6.06);
  split.mutable_buff()->mutable_base()->set_final_attack_lines(5);

  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}},
                  {{"snipe", snipe}, {"sweep", sweep}, {"split", split}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 3);
  ASSERT_TRUE(state.character.LearnSkill(snipe, 1));
  ASSERT_TRUE(state.character.LearnSkill(sweep, 1));
  ASSERT_TRUE(state.character.LearnSkill(split, 1));

  CombatParams params = ComputeCombatParams(state);
  ASSERT_EQ(params.buffs.size(), 1u);
  // The buff is not standing in the plain set, so nothing follows any swing.
  for (const AttackOption& attack : params.attacks) {
    EXPECT_TRUE(attack.per_swing_final_attack_damage.empty()) << attack.name;
  }
  const std::vector<AttackOption>& under = params.Attacks(1);
  const AttackOption* aimed = nullptr;
  const AttackOption* wide = nullptr;
  for (const AttackOption& attack : under) {
    if (attack.name == "Snipe") {
      aimed = &attack;
    } else if (attack.name == "High Speed Shot") {
      wide = &attack;
    }
  }
  ASSERT_NE(aimed, nullptr);
  ASSERT_NE(wide, nullptr);
  // Ten enemies behind a swing that reaches one -- the reach is the follow-up's
  // own, which is the whole reason it is a count rather than a flag.
  ASSERT_FALSE(aimed->per_swing_final_attack_damage.empty());
  EXPECT_EQ(aimed->per_swing_final_attack_enemies, 10);
  ASSERT_EQ(aimed->per_swing_final_attack_rolls.size(), 1u);
  EXPECT_DOUBLE_EQ(aimed->per_swing_final_attack_rolls[0].chance, 1.0);
  // The swing without the group carries none of it.
  EXPECT_TRUE(wide->per_swing_final_attack_damage.empty());
  EXPECT_TRUE(wide->final_attack_damage.empty());
}

// Jupiter Thunder's three marks on the fight: the rate it spends the pile at,
// the current reaching past the orb, and the stun it leaves -- which lifts the
// character's OTHER lightning and never its own.
TEST(ComputeCombatParamsTest, AStunLeavesAMarkTheOtherLightningCollects) {
  Skill crush;
  crush.set_name("Freezing Crush");
  crush.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(crush, JOB_ADVANCEMENT_SWORDMAN);
  crush.set_max_level(10);
  crush.set_freeze_stack_cap(5);
  crush.mutable_base()->set_crit_dmg_per_freeze_stack(0.001);

  Skill bolt;
  bolt.set_name("Chain Lightning");
  bolt.set_kind(SKILL_KIND_ATTACK);
  PlaceIn(bolt, JOB_ADVANCEMENT_SWORDMAN);
  bolt.set_max_level(30);
  bolt.set_base_delay_ms(660);
  bolt.add_tags(SKILL_TAG_LIGHTNING);
  bolt.mutable_base()->set_skill_pct(1.33);

  Skill orb;
  orb.set_name("Jupiter Thunder");
  orb.set_kind(SKILL_KIND_ATTACK);
  PlaceIn(orb, JOB_ADVANCEMENT_SWORDMAN);
  orb.set_max_level(30);
  orb.set_base_delay_ms(810);
  orb.add_tags(SKILL_TAG_LIGHTNING);
  orb.set_max_enemies(1);
  orb.set_lines(8);
  orb.set_freeze_lines_per_spend(5);
  orb.mutable_stun()->set_duration_seconds(4.0);
  orb.mutable_stun()->set_final_dmg_pct(0.12);
  orb.mutable_stun()->set_lifted_tag(SKILL_TAG_LIGHTNING);
  orb.mutable_base()->set_skill_pct(8.71);
  SwingHit* current = orb.add_extra_hit();
  current->set_label("Electric Current");
  current->set_lines(4);
  current->set_max_enemies(2);
  current->mutable_base()->set_skill_pct(5.13);

  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}},
                  {{"freezing_crush", crush},
                   {"chain_lightning", bolt},
                   {"jupiter_thunder", orb}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 3);
  ASSERT_TRUE(state.character.LearnSkill(crush, 1));
  ASSERT_TRUE(state.character.LearnSkill(bolt, 1));
  ASSERT_TRUE(state.character.LearnSkill(orb, 1));

  CombatParams params = ComputeCombatParams(state);
  const AttackOption* shock = nullptr;
  const AttackOption* chain = nullptr;
  for (const AttackOption& attack : params.attacks) {
    if (attack.name == "Jupiter Thunder") {
      shock = &attack;
    } else if (attack.name == "Chain Lightning") {
      chain = &attack;
    }
  }
  ASSERT_NE(shock, nullptr);
  ASSERT_NE(chain, nullptr);

  // The stun is the orb's to leave and Chain Lightning's to collect.
  EXPECT_GT(shock->stun_seconds, 0.0);
  EXPECT_DOUBLE_EQ(shock->stun_lift_pct, 0.12);
  EXPECT_FALSE(shock->collects_stun_lift);
  EXPECT_TRUE(chain->collects_stun_lift);
  EXPECT_DOUBLE_EQ(chain->stun_seconds, 0.0);

  // One stack every five lines, where the plain lightning swing pays per line.
  EXPECT_EQ(shock->freeze_lines_per_spend, 5);
  EXPECT_EQ(chain->freeze_lines_per_spend, 1);

  // The current is banked apart, reaching two where the orb reaches one, and
  // is no part of what the orb does to the enemy it rides.
  EXPECT_EQ(shock->max_enemies, 1);
  EXPECT_EQ(shock->wide_hit_enemies, 2);
  ASSERT_FALSE(shock->wide_hit_damage.empty());
  EXPECT_GT(shock->wide_hit_damage[0], 0.0);
  ASSERT_EQ(shock->wide_hit_groups.size(), 1u);
  EXPECT_EQ(shock->wide_hit_groups[0].rolls.lines, 4);
  EXPECT_TRUE(chain->wide_hit_damage.empty());
}

// Spirit of Snow's shape: a summon on a cooldown, which is a buff with a
// pulse. The blizzard it calls down is ice whoever called it, so it feeds the
// pile exactly as the character's own ice swing would.
TEST(ComputeCombatParamsTest, ABuffsPulseStrikesWithItsSkillsElement) {
  Skill crush;
  crush.set_name("Freezing Crush");
  crush.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(crush, JOB_ADVANCEMENT_SWORDMAN);
  crush.set_max_level(10);
  crush.set_freeze_stack_cap(5);
  crush.mutable_base()->set_crit_dmg_per_freeze_stack(0.001);

  Skill spirit;
  spirit.set_name("Spirit of Snow");
  spirit.set_kind(SKILL_KIND_ACTIVE);
  PlaceIn(spirit, JOB_ADVANCEMENT_SWORDMAN);
  spirit.set_max_level(30);
  spirit.add_tags(SKILL_TAG_ICE);
  spirit.set_freeze_seconds(8.0);
  spirit.mutable_buff()->set_duration_seconds(30.0);
  BuffPulse* blizzard = spirit.mutable_buff()->mutable_pulse();
  blizzard->set_label("Blizzard");
  blizzard->set_cast_interval_seconds(3.0);
  blizzard->set_lines(12);
  blizzard->set_max_enemies(10);
  blizzard->mutable_base()->set_skill_pct(8.16);

  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}},
                  {{"freezing_crush", crush}, {"spirit_of_snow", spirit}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 2);
  ASSERT_TRUE(state.character.LearnSkill(crush, 1));
  ASSERT_TRUE(state.character.LearnSkill(spirit, 1));

  CombatParams params = ComputeCombatParams(state);
  ASSERT_EQ(params.auto_attacks.size(), 1u);
  const AttackOption& blast = params.auto_attacks[0];
  EXPECT_EQ(blast.name, "Spirit of Snow");
  // One stack per line, and the eight seconds of ice the skill states -- both
  // read off the skill the pulse belongs to rather than off the pulse.
  EXPECT_EQ(blast.freeze_build, 12);
  EXPECT_EQ(blast.freeze_build_alone, 0);
  EXPECT_DOUBLE_EQ(blast.freeze_seconds,
                   8.0 * GameSpeedFactor(state.character.proto().level()));
  // It never spends the pile, as no summon does.
  EXPECT_FALSE(blast.freeze_spends);
}

// A skill that states its own count replaces the stack-per-line outright, both
// halves of it: Spirit of Snow pays 3 to a lone enemy and 1 to a crowd, where
// twelve lines would otherwise have bought twelve.
TEST(ComputeCombatParamsTest, ASkillCanStateTheFreezeStacksItLeaves) {
  Skill crush;
  crush.set_name("Freezing Crush");
  crush.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(crush, JOB_ADVANCEMENT_SWORDMAN);
  crush.set_max_level(10);
  crush.set_freeze_stack_cap(5);
  crush.mutable_base()->set_crit_dmg_per_freeze_stack(0.001);

  Skill spirit;
  spirit.set_name("Spirit of Snow");
  spirit.set_kind(SKILL_KIND_ACTIVE);
  PlaceIn(spirit, JOB_ADVANCEMENT_SWORDMAN);
  spirit.set_max_level(30);
  spirit.add_tags(SKILL_TAG_ICE);
  spirit.set_freeze_seconds(8.0);
  spirit.mutable_freeze_build()->set_alone(3);
  spirit.mutable_freeze_build()->set_crowd(1);
  spirit.mutable_buff()->set_duration_seconds(30.0);
  BuffPulse* blizzard = spirit.mutable_buff()->mutable_pulse();
  blizzard->set_label("Blizzard");
  blizzard->set_cast_interval_seconds(3.0);
  blizzard->set_lines(12);
  blizzard->set_max_enemies(10);
  blizzard->mutable_base()->set_skill_pct(8.16);

  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}},
                  {{"freezing_crush", crush}, {"spirit_of_snow", spirit}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 2);
  ASSERT_TRUE(state.character.LearnSkill(crush, 1));
  ASSERT_TRUE(state.character.LearnSkill(spirit, 1));

  CombatParams params = ComputeCombatParams(state);
  ASSERT_EQ(params.auto_attacks.size(), 1u);
  EXPECT_EQ(params.auto_attacks[0].freeze_build, 1);
  EXPECT_EQ(params.auto_attacks[0].freeze_build_alone, 3);
}

// Frost Ark's shape: the same buff, but its shock is struck by the orb the
// character left standing as well as by the bolts they cast. Every other Final
// Attack stops at the swing, so the summon carries one only where the granting
// skill says it follows an own clock.
TEST(ComputeCombatParamsTest, AFinalAttackCanFollowASummonsOwnClock) {
  Skill bolt;
  bolt.set_name("Chain Lightning");
  bolt.set_kind(SKILL_KIND_ATTACK);
  PlaceIn(bolt, JOB_ADVANCEMENT_SWORDMAN);
  bolt.set_max_level(30);
  bolt.set_base_delay_ms(660);
  bolt.set_max_enemies(6);
  bolt.add_tags(SKILL_TAG_LIGHTNING);
  bolt.mutable_base()->set_skill_pct(1.33);

  Skill orb;
  orb.set_name("Thunder Sphere");
  orb.set_kind(SKILL_KIND_AUTO_ATTACK);
  PlaceIn(orb, JOB_ADVANCEMENT_SWORDMAN);
  orb.set_max_level(20);
  orb.set_max_enemies(6);
  orb.set_cast_interval_seconds(1.8);
  orb.add_tags(SKILL_TAG_LIGHTNING);
  orb.mutable_base()->set_skill_pct(2.01);

  Skill ark;
  ark.set_name("Frost Ark");
  ark.set_kind(SKILL_KIND_ACTIVE);
  PlaceIn(ark, JOB_ADVANCEMENT_SWORDMAN);
  ark.set_max_level(30);
  ark.set_base_delay_ms(120);
  ark.set_follows_skill_tag(SKILL_TAG_LIGHTNING);
  ark.set_follows_own_clock(true);
  ark.set_final_attack_max_enemies(15);
  ark.mutable_buff()->set_duration_seconds(20.0);
  ark.mutable_buff()->mutable_base()->set_final_attack_chance(1.0);
  ark.mutable_buff()->mutable_base()->set_final_attack_pct(1.82);
  ark.mutable_buff()->mutable_base()->set_final_attack_lines(5);

  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}},
                  {{"bolt", bolt}, {"orb", orb}, {"ark", ark}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 3);
  ASSERT_TRUE(state.character.LearnSkill(bolt, 1));
  ASSERT_TRUE(state.character.LearnSkill(orb, 1));
  ASSERT_TRUE(state.character.LearnSkill(ark, 1));

  CombatParams params = ComputeCombatParams(state);
  ASSERT_EQ(params.buffs.size(), 1u);
  ASSERT_EQ(params.AutoAttacks(1).size(), 1u);
  const AttackOption& summoned = params.AutoAttacks(1)[0];
  EXPECT_EQ(summoned.name, "Thunder Sphere");
  // The whole point: the orb sets the ark off, over the ark's own crowd.
  ASSERT_FALSE(summoned.per_swing_final_attack_damage.empty());
  EXPECT_EQ(summoned.per_swing_final_attack_enemies, 15);
  ASSERT_EQ(summoned.per_swing_final_attack_rolls.size(), 1u);
  EXPECT_DOUBLE_EQ(summoned.per_swing_final_attack_rolls[0].chance, 1.0);
  EXPECT_DOUBLE_EQ(summoned.per_swing_final_attack_damage[0],
                   summoned.per_swing_final_attack_rolls[0].damage[0]);
  // And the character's own lightning swing still carries it.
  const AttackOption* swung = nullptr;
  for (const AttackOption& attack : params.Attacks(1)) {
    if (attack.name == "Chain Lightning") {
      swung = &attack;
    }
  }
  ASSERT_NE(swung, nullptr);
  EXPECT_EQ(swung->per_swing_final_attack_enemies, 15);

  // Without the flag the orb is a summon like any other and follows nothing,
  // while the swing is untouched.
  ark.set_follows_own_clock(false);
  GameState plain({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}},
                  {{"bolt", bolt}, {"orb", orb}, {"ark", ark}});
  plain.current_map = "field";
  EquipSword(plain);
  GrantFirstJobSp(plain, 3);
  ASSERT_TRUE(plain.character.LearnSkill(bolt, 1));
  ASSERT_TRUE(plain.character.LearnSkill(orb, 1));
  ASSERT_TRUE(plain.character.LearnSkill(ark, 1));

  CombatParams bare = ComputeCombatParams(plain);
  ASSERT_EQ(bare.AutoAttacks(1).size(), 1u);
  EXPECT_TRUE(bare.AutoAttacks(1)[0].per_swing_final_attack_damage.empty());
  EXPECT_EQ(bare.AutoAttacks(1)[0].per_swing_final_attack_enemies, 1);
  for (const AttackOption& attack : bare.Attacks(1)) {
    if (attack.name == "Chain Lightning") {
      EXPECT_EQ(attack.per_swing_final_attack_enemies, 15);
    }
  }
}

// A Night Lord's mark throws three stars where an Assassin's throws two. The
// strikes are told apart rather than folded into one percent, so each rolls
// its own crit -- and three of them are worth three times one.
TEST(ComputeCombatParamsTest, AFinalAttackLandsItsOwnStrikes) {
  Skill mark;
  mark.set_name("Night Lord's Mark");
  mark.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(mark, JOB_ADVANCEMENT_SWORDMAN);
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
  PlaceIn(ray, JOB_ADVANCEMENT_SWORDMAN);
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
  PlaceIn(shout, JOB_ADVANCEMENT_SWORDMAN);
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
  PlaceIn(burst, JOB_ADVANCEMENT_SWORDMAN);
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
  PlaceIn(slow, JOB_ADVANCEMENT_SWORDMAN);
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
  PlaceIn(blaster, JOB_ADVANCEMENT_SWORDMAN);
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

// An attack GMS lets the player throw mid-swing plays no cast action, so the
// animation the file records is not what it costs -- and there is none left
// for a faster weapon to shorten. See Skill.waives_cast.
TEST(ComputeCombatParamsTest, AWaivedCastCostsThePressAndIgnoresTheWeapon) {
  Skill formation;
  formation.set_name("Slash Shadow Formation");
  formation.set_kind(SKILL_KIND_ATTACK);
  PlaceIn(formation, JOB_ADVANCEMENT_SWORDMAN);
  formation.set_max_level(30);
  formation.set_base_delay_ms(600);
  formation.set_waives_cast(true);
  formation.mutable_base()->set_skill_pct(7.33);

  double at_stage[2] = {0.0, 0.0};
  AttackSpeed speeds[2] = {ATTACK_SPEED_SLOWER, ATTACK_SPEED_FASTEST_3};
  for (int i = 0; i < 2; ++i) {
    GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                    {{"field", TwoSnailMap()}}, {{"formation", formation}});
    state.current_map = "field";
    EquipSwordAt(state, speeds[i]);
    GrantFirstJobSp(state, 1);
    ASSERT_TRUE(state.character.LearnSkill(formation, 1));
    CombatParams params = ComputeCombatParams(state);
    ASSERT_EQ(params.attacks.size(), 2u);
    at_stage[i] = params.attacks[1].swing_seconds;
    // The press alone, not the 600ms of frames the file still records.
    EXPECT_DOUBLE_EQ(at_stage[i],
                     0.120 * GameSpeedFactor(state.character.proto().level()));
  }
  EXPECT_DOUBLE_EQ(at_stage[0], at_stage[1]);
}

TEST(ComputeCombatParamsTest, AnOrdinarySwingStillAnswersToTheWeapon) {
  Skill wind;
  wind.set_name("Wind Arrow");
  wind.set_kind(SKILL_KIND_ATTACK);
  PlaceIn(wind, JOB_ADVANCEMENT_SWORDMAN);
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
  PlaceIn(slash, JOB_ADVANCEMENT_SWORDMAN);
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
  PlaceIn(evil_eye, JOB_ADVANCEMENT_SWORDMAN);
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

// A boost naming a skill reaches the turret that skill leaves standing as well
// as the volley: an own-clock half keeps its parent's name, and a boost is
// keyed by name. Hurricane - Reinforce and Gritty Gust both rely on it -- GMS
// aims each at Arrow Blaster and at the installed Arrow Blaster alike.
TEST(ComputeCombatParamsTest, ABoostReachesBothHalvesOfTheSkillItNames) {
  Skill blaster;
  blaster.set_name("Arrow Blaster");
  blaster.set_kind(SKILL_KIND_ATTACK);
  PlaceIn(blaster, JOB_ADVANCEMENT_SWORDMAN);
  blaster.set_max_level(20);
  blaster.set_max_enemies(4);
  blaster.mutable_base()->set_skill_pct(1.24);
  AutoMode* turret = blaster.add_auto_mode();
  turret->set_label("Turret");
  turret->set_cast_interval_seconds(0.21);
  turret->set_max_enemies(4);
  turret->mutable_base()->set_skill_pct(0.66);

  Skill hyper;
  hyper.set_name("Gritty Gust");
  hyper.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(hyper, JOB_ADVANCEMENT_SWORDMAN);
  hyper.set_max_level(1);
  SkillBoost* boost = hyper.add_boost();
  boost->set_skill_name("Arrow Blaster");
  boost->mutable_effect()->set_skill_pct(0.90);

  GameState bare({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                 {{"field", TwoSnailMap()}}, {{"blaster", blaster}});
  bare.current_map = "field";
  EquipSword(bare);
  GrantFirstJobSp(bare, 1);
  ASSERT_TRUE(bare.character.LearnSkill(blaster, 1));

  GameState boosted({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                    {{"field", TwoSnailMap()}},
                    {{"blaster", blaster}, {"hyper", hyper}});
  boosted.current_map = "field";
  EquipSword(boosted);
  GrantFirstJobSp(boosted, 2);
  ASSERT_TRUE(boosted.character.LearnSkill(blaster, 1));
  ASSERT_TRUE(boosted.character.LearnSkill(hyper, 1));

  CombatParams before = ComputeCombatParams(bare);
  CombatParams after = ComputeCombatParams(boosted);
  // The volley: 124% to 214%, so the swing is worth a shade under twice what
  // it was.
  ASSERT_EQ(before.attacks.size(), 2u);
  ASSERT_EQ(after.attacks.size(), 2u);
  EXPECT_NEAR(
      after.attacks[1].damage_per_hit[0] / before.attacks[1].damage_per_hit[0],
      2.14 / 1.24, 1e-6);
  // The turret standing behind it: 66% to 156%, off the same one boost.
  ASSERT_EQ(before.auto_attacks.size(), 1u);
  ASSERT_EQ(after.auto_attacks.size(), 1u);
  EXPECT_NEAR(after.auto_attacks[0].damage_per_hit[0] /
                  before.auto_attacks[0].damage_per_hit[0],
              1.56 / 0.66, 1e-6);
}

// A boost node hands its damage out from level 1, one more enemy at 20 and
// ignored defence at 40, so what the fight sees depends on how far the node is
// taught. Written as a real node rather than a passive: a boost node belongs
// to no book, and every gate on the way has to ask the matrix instead.
TEST(ComputeCombatParamsTest, ABoostNodePaysEachTierAtItsOwnLevel) {
  Skill swing;
  swing.set_name("Raging Blow");
  swing.set_kind(SKILL_KIND_ATTACK);
  PlaceIn(swing, JOB_ADVANCEMENT_SWORDMAN);
  swing.set_max_level(20);
  swing.set_max_enemies(4);
  swing.mutable_base()->set_skill_pct(1.24);

  Skill node;
  node.set_name("Raging Blow Boost");
  node.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(node, JOB_ADVANCEMENT_SWORDMAN);
  node.set_v_node(V_NODE_KIND_BOOST);
  node.set_max_level(MaxVNodeLevel(V_NODE_KIND_BOOST));
  SkillBoost* damage = node.add_boost();
  damage->set_skill_name("Raging Blow");
  damage->mutable_effect()->set_final_dmg_pct(0.02);
  damage->mutable_effect_per_level()->set_final_dmg_pct(0.02);
  SkillBoost* reach = node.add_boost();
  reach->set_skill_name("Raging Blow");
  reach->set_min_level(20);
  reach->set_max_enemies(1);

  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}},
                  {{"swing", swing}, {"node", node}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 1);
  while (state.character.proto().job_stage() < kFifthJobStage) {
    state.character.AdvanceJob(state.character.proto().job());
  }
  state.character.AddVPoints(VNodeCost(V_NODE_KIND_BOOST, 0, 20));
  ASSERT_TRUE(state.character.LearnSkill(swing, 1));

  CombatParams bare = ComputeCombatParams(state);
  ASSERT_TRUE(state.character.LearnSkill(node, 19));
  CombatParams under = ComputeCombatParams(state);
  EXPECT_EQ(under.attacks[1].max_enemies, 4);
  // 38% final damage at 19, which is the whole of what the node pays yet.
  EXPECT_NEAR(
      under.attacks[1].damage_per_hit[0] / bare.attacks[1].damage_per_hit[0],
      1.38, 1e-6);

  ASSERT_TRUE(state.character.LearnSkill(node, 1));
  CombatParams over = ComputeCombatParams(state);
  EXPECT_EQ(over.attacks[1].max_enemies, 5);
  EXPECT_NEAR(
      over.attacks[1].damage_per_hit[0] / bare.attacks[1].damage_per_hit[0],
      1.40, 1e-6);
}

// Revenge of the Evil Eye's shape: one skill that fires on its own clock AND
// carries two more clocks behind it, each reaching a different number of
// enemies -- which is why they cannot be folded into one attack.
TEST(ComputeCombatParamsTest, ASkillCanCarrySeveralOwnClockHalves) {
  Skill revenge;
  revenge.set_name("Revenge of the Evil Eye");
  revenge.set_kind(SKILL_KIND_AUTO_ATTACK);
  PlaceIn(revenge, JOB_ADVANCEMENT_SWORDMAN);
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

// Weapon Aura's shape: a passive whose levers ride the character, carrying an
// own-clock half that fights. The fight can spend no swing on it, so the gate
// that offers swings must not be the one that decides whether its wave fires.
TEST(ComputeCombatParamsTest, APassiveStillFiresItsOwnClockHalf) {
  Skill aura;
  aura.set_name("Weapon Aura");
  aura.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(aura, JOB_ADVANCEMENT_SWORDMAN);
  aura.set_max_level(30);
  aura.mutable_base()->set_ied_pct(0.102);
  AutoMode* wave = aura.add_auto_mode();
  wave->set_label("Aura Wave");
  wave->set_cast_interval_seconds(5.0);
  wave->set_max_enemies(10);
  wave->set_lines(6);
  wave->mutable_base()->set_skill_pct(5.20);

  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}}, {{"aura", aura}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 1);
  ASSERT_TRUE(state.character.LearnSkill(aura, 1));

  CombatParams params = ComputeCombatParams(state);
  EXPECT_EQ(params.attacks.size(), 1u);  // the bare poke; a passive is no swing
  ASSERT_EQ(params.auto_attacks.size(), 1u);
  EXPECT_EQ(params.auto_attacks[0].max_enemies, 10);
  EXPECT_DOUBLE_EQ(params.auto_attacks[0].interval_seconds,
                   5.0 * GameSpeedFactor(state.character.proto().level()));
  EXPECT_EQ(params.auto_attacks[0].lines, 6);
}

// A timed buff needs a damage table of its own, because what it grants -- a
// share of the monster's DEF ignored -- cannot be applied to a damage number
// after that number has been worked out.
TEST(ComputeCombatParamsTest, ABuffGetsADamageTableOfItsOwn) {
  Skill resonance;
  resonance.set_name("Dark Resonance");
  resonance.set_kind(SKILL_KIND_ACTIVE);
  PlaceIn(resonance, JOB_ADVANCEMENT_SWORDMAN);
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

// Epic Adventure's shape: one buff over the whole party, raised in turn. The
// caster's own wait is untouched -- what shortens is the gap they spend
// without it, because a partner's cast covers them too.
TEST(ComputeCombatParamsTest, APartySharedBuffComesRoundOncePerHolder) {
  Skill epic;
  epic.set_name("Epic Adventure");
  epic.set_kind(SKILL_KIND_ACTIVE);
  PlaceIn(epic, JOB_ADVANCEMENT_SWORDMAN);
  epic.set_max_level(1);
  epic.set_base_delay_ms(900);
  epic.set_cooldown_seconds(120.0);
  Buff* buff = epic.mutable_buff();
  buff->set_duration_seconds(60.0);
  buff->set_party_shared(true);
  buff->mutable_base()->set_damage_pct(0.10);

  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}}, {{"epic_adventure", epic}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 1);
  ASSERT_TRUE(state.character.LearnSkill(epic, 1));
  double factor = GameSpeedFactor(state.character.proto().level());

  ASSERT_EQ(ComputeCombatParams(state).buffs.size(), 1u);
  EXPECT_DOUBLE_EQ(ComputeCombatParams(state).buffs[0].cooldown_seconds,
                   120.0 * factor);

  // A partner holding it takes every second turn, so it is up twice as often.
  std::mt19937 rng(1);
  Character ally = state.character.proto();
  state.party.emplace_back(rng, ally);
  EXPECT_DOUBLE_EQ(ComputeCombatParams(state).buffs[0].cooldown_seconds,
                   60.0 * factor);

  // One who never learned it covers nobody.
  ally.mutable_skill_levels()->clear();
  state.party.emplace_back(rng, ally);
  EXPECT_DOUBLE_EQ(ComputeCombatParams(state).buffs[0].cooldown_seconds,
                   60.0 * factor);
}

// Holy Magic Shell's shape: one shell over the caster and the party alike, so
// both halves of the buff carry the same count of blocks -- read at whichever
// level the character holding it learned.
TEST(ComputeCombatParamsTest, AShellReachesTheCasterAndThePartyAlike) {
  Skill shell;
  shell.set_name("Holy Magic Shell");
  shell.set_kind(SKILL_KIND_ACTIVE);
  PlaceIn(shell, JOB_ADVANCEMENT_SWORDMAN);
  shell.set_max_level(20);
  shell.set_base_delay_ms(120);
  shell.set_cooldown_seconds(90.0);
  Buff* buff = shell.mutable_buff();
  buff->set_duration_seconds(10.25);
  buff->set_duration_seconds_per_level(0.25);
  buff->mutable_base()->set_heal_pct(0.31);
  buff->mutable_ally_base()->set_heal_pct(0.31);
  buff->mutable_shield()->set_party(true);
  buff->mutable_shield()->set_hits(5.5);
  buff->mutable_shield()->set_hits_per_level(0.5);
  buff->mutable_shield()->set_boss_damage_taken_pct(0.10);

  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}}, {{"holy_magic_shell", shell}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 20);

  // A Priest in the party and none in the mirror: the shell reaches this
  // character as a window of somebody else's, at that somebody's level.
  std::mt19937 rng(1);
  Character ally = state.character.proto();
  (*ally.mutable_skill_levels())["Holy Magic Shell"] = 10;
  state.party.emplace_back(rng, ally);

  CombatParams params = ComputeCombatParams(state);
  ASSERT_EQ(params.buffs.size(), 1u);
  EXPECT_EQ(params.buffs[0].shield_hits, 10);
  EXPECT_DOUBLE_EQ(params.buffs[0].boss_damage_taken_pct, 0.10);
  EXPECT_NEAR(params.buffs[0].heal_fraction, 0.31, 1e-9);

  // A shell that is not the party's shelters its caster and nobody else --
  // Divine Shield is the White Knight's own skin. See Shield.party.
  shell.mutable_buff()->mutable_shield()->set_party(false);
  state.skills["holy_magic_shell"] = shell;
  params = ComputeCombatParams(state);
  ASSERT_EQ(params.buffs.size(), 1u);
  EXPECT_EQ(params.buffs[0].shield_hits, 0);
  shell.mutable_buff()->mutable_shield()->set_party(true);
  state.skills["holy_magic_shell"] = shell;
  params = ComputeCombatParams(state);

  // Learning it themselves puts the shell on their own clock instead: a
  // party grant never doubles up on somebody already holding the skill.
  ASSERT_TRUE(state.character.LearnSkill(shell, 20));
  params = ComputeCombatParams(state);
  // One shell, not two: the party's grant never doubles up on somebody
  // already holding the skill.
  ASSERT_EQ(params.buffs.size(), 1u);
  EXPECT_EQ(params.buffs[0].shield_hits, 15);
  EXPECT_DOUBLE_EQ(params.buffs[0].boss_damage_taken_pct, 0.10);
  EXPECT_NEAR(params.buffs[0].heal_fraction, 0.31, 1e-9);
}

// Holy Magic Shell's three hypers: seconds onto the buff's clock, hits onto
// its shell, and a share onto what its shell takes off a boss's hit. Read off
// the CASTER's book, so a party member's hypers deepen the shell they raise
// and not the one raised over them.
TEST(ComputeCombatParamsTest, ABoostDeepensTheShellItNames) {
  Skill shell;
  shell.set_name("Holy Magic Shell");
  shell.set_kind(SKILL_KIND_ACTIVE);
  PlaceIn(shell, JOB_ADVANCEMENT_SWORDMAN);
  shell.set_max_level(20);
  shell.set_cooldown_seconds(90.0);
  Buff* buff = shell.mutable_buff();
  buff->set_duration_seconds(10.25);
  buff->set_duration_seconds_per_level(0.25);
  buff->mutable_ally_base()->set_heal_pct(0.31);
  buff->mutable_shield()->set_party(true);
  buff->mutable_shield()->set_hits(5.5);
  buff->mutable_shield()->set_hits_per_level(0.5);
  buff->mutable_shield()->set_boss_damage_taken_pct(0.10);

  Skill hyper;
  hyper.set_name("Holy Magic Shell - Extra Guard");
  hyper.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(hyper, JOB_ADVANCEMENT_SWORDMAN);
  hyper.set_max_level(1);
  SkillBoost* boost = hyper.add_boost();
  boost->set_skill_name("Holy Magic Shell");
  boost->set_shield_hits(2.0);
  boost->set_buff_duration_seconds(5.0);
  boost->set_shield_boss_damage_taken_pct(0.05);

  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}},
                  {{"holy_magic_shell", shell}, {"extra_guard", hyper}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 21);
  ASSERT_TRUE(state.character.LearnSkill(shell, 20));

  CombatParams before = ComputeCombatParams(state);
  ASSERT_EQ(before.buffs.size(), 1u);
  EXPECT_EQ(before.buffs[0].shield_hits, 15);
  // Game-scaled, as every duration here is: 15 seconds at this level's pace.
  EXPECT_NEAR(before.buffs[0].duration_seconds, 45.0, 1e-9);

  ASSERT_TRUE(state.character.LearnSkill(hyper, 1));
  CombatParams params = ComputeCombatParams(state);
  ASSERT_EQ(params.buffs.size(), 1u);
  EXPECT_EQ(params.buffs[0].shield_hits, 17);
  EXPECT_NEAR(params.buffs[0].duration_seconds, 60.0, 1e-9);
  EXPECT_DOUBLE_EQ(params.buffs[0].boss_damage_taken_pct, 0.15);

  // The same shell raised by somebody else, whose book holds no hyper: what
  // reaches this character is the shell their caster actually stands up.
  std::mt19937 rng(1);
  Character ally = state.character.proto();
  ally.mutable_skill_levels()->erase("Holy Magic Shell - Extra Guard");
  CharacterInstance bare(rng, ally);
  std::vector<CharacterInstance> party;
  party.push_back(std::move(bare));
  GameState other({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}},
                  {{"holy_magic_shell", shell}, {"extra_guard", hyper}});
  other.current_map = "field";
  EquipSword(other);
  other.party = std::move(party);
  CombatParams from_ally = ComputeCombatParams(other);
  ASSERT_EQ(from_ally.buffs.size(), 1u);
  EXPECT_EQ(from_ally.buffs[0].shield_hits, 15);
}

// A Vengeance form and the Benevolence skill it stands in for are one row of
// the book: the switch decides which of the two the fight is offered.
TEST(ComputeCombatParamsTest, AToggleSwapsWhichFormIsSwung) {
  Skill heal;
  heal.set_name("Heal");
  heal.set_kind(SKILL_KIND_ACTIVE);
  PlaceIn(heal, JOB_ADVANCEMENT_SWORDMAN);
  heal.set_max_level(10);
  heal.mutable_base()->set_heal_pct(0.10);
  heal.mutable_per_level()->set_heal_pct(0.10);

  Skill wrath;
  wrath.set_name("Angelic Wrath");
  wrath.set_kind(SKILL_KIND_ATTACK);
  PlaceIn(wrath, JOB_ADVANCEMENT_SWORDMAN);
  wrath.set_max_level(10);
  wrath.set_max_enemies(6);
  wrath.set_lines(5);
  wrath.set_replaces_skill_name("Heal");
  wrath.set_toggle_skill_name("Righteously Indignant");
  wrath.mutable_base()->set_skill_pct(2.60);

  Skill toggle;
  toggle.set_name("Righteously Indignant");
  toggle.set_kind(SKILL_KIND_ACTIVE);
  PlaceIn(toggle, JOB_ADVANCEMENT_SWORDMAN);
  toggle.set_max_level(1);
  toggle.set_toggle(true);

  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}},
                  {{"heal", heal}, {"wrath", wrath}, {"toggle", toggle}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 11);
  ASSERT_TRUE(state.character.LearnSkill(heal, 10));
  ASSERT_TRUE(state.character.LearnSkill(toggle, 1));

  CombatParams off = ComputeCombatParams(state);
  ASSERT_EQ(off.attacks.size(), 2u);
  EXPECT_EQ(off.attacks[1].name, "Heal");

  ASSERT_TRUE(state.character.ToggleSkill(toggle));
  CombatParams on = ComputeCombatParams(state);
  ASSERT_EQ(on.attacks.size(), 2u);
  EXPECT_EQ(on.attacks[1].name, "Angelic Wrath")
      << "and it swings at the level Heal was bought to";
  EXPECT_GT(on.attacks[1].damage_per_hit[0], 0.0);
}

// Smokescreen's shape: an ally's buff stands over this character on the
// caster's clock, at the caster's level, and never touches a damage table --
// all it can hand over is a share off what a hit costs.
TEST(ComputeCombatParamsTest, APartysBuffComesInOnItsCastersClock) {
  Skill smoke;
  smoke.set_name("Smokescreen");
  smoke.set_kind(SKILL_KIND_ACTIVE);
  PlaceIn(smoke, JOB_ADVANCEMENT_SWORDMAN);
  smoke.set_max_level(10);
  smoke.set_base_delay_ms(720);
  smoke.set_cooldown_seconds(120.0);
  Buff* buff = smoke.mutable_buff();
  buff->set_duration_seconds(30.0);
  buff->mutable_base()->set_damage_taken_pct(0.01);
  buff->mutable_per_level()->set_damage_taken_pct(0.01);
  buff->mutable_ally_base()->set_damage_taken_pct(0.01);
  buff->mutable_ally_per_level()->set_damage_taken_pct(0.01);

  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}}, {{"smokescreen", smoke}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 0);
  double factor = GameSpeedFactor(state.character.proto().level());
  EXPECT_TRUE(ComputeCombatParams(state).buffs.empty());

  // The party member holds it; the character reading these params does not.
  std::mt19937 rng(1);
  Character ally = state.character.proto();
  (*ally.mutable_skill_levels())["Smokescreen"] = 8;
  state.party.emplace_back(rng, ally);

  CombatParams params = ComputeCombatParams(state);
  ASSERT_EQ(params.buffs.size(), 1u);
  EXPECT_EQ(params.buffs[0].name, "Smokescreen");
  EXPECT_DOUBLE_EQ(params.buffs[0].duration_seconds, 30.0 * factor);
  EXPECT_DOUBLE_EQ(params.buffs[0].cooldown_seconds, 120.0 * factor);
  // Their level, not the reader's: eight points is 8% off a hit.
  EXPECT_NEAR(params.buffs[0].damage_taken_pct, 0.08, 1e-9);
  // It gets a damage table of its own, exactly as a buff of the character's
  // own would: what a party buff grants is not damage taken alone.
  EXPECT_EQ(params.buffed.size(), 1u);
}

// A party buff is timed by whoever cast it: the caster's Buff Duration
// lengthens it, and the reader's does nothing to it.
TEST(ComputeCombatParamsTest, APartysBuffTakesItsCastersBuffDuration) {
  Skill smoke;
  smoke.set_name("Smokescreen");
  smoke.set_kind(SKILL_KIND_ACTIVE);
  PlaceIn(smoke, JOB_ADVANCEMENT_SWORDMAN);
  smoke.set_max_level(10);
  smoke.set_cooldown_seconds(120.0);
  Buff* buff = smoke.mutable_buff();
  buff->set_duration_seconds(30.0);
  buff->mutable_ally_base()->set_damage_taken_pct(0.01);
  buff->mutable_ally_per_level()->set_damage_taken_pct(0.01);

  Skill mastery;
  mastery.set_name("Buff Mastery");
  mastery.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(mastery, JOB_ADVANCEMENT_SWORDMAN);
  mastery.set_max_level(10);
  mastery.mutable_base()->set_buff_duration_pct(0.05);
  mastery.mutable_per_level()->set_buff_duration_pct(0.05);

  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}},
                  {{"smokescreen", smoke}, {"buff_mastery", mastery}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 18);
  double factor = GameSpeedFactor(state.character.proto().level());

  // The ally holds both: their fifty percent stretches the cloud to 45s.
  std::mt19937 rng(1);
  Character ally = state.character.proto();
  (*ally.mutable_skill_levels())["Smokescreen"] = 8;
  (*ally.mutable_skill_levels())["Buff Mastery"] = 10;
  state.party.emplace_back(rng, ally);

  CombatParams params = ComputeCombatParams(state);
  ASSERT_EQ(params.buffs.size(), 1u);
  EXPECT_DOUBLE_EQ(params.buffs[0].duration_seconds, 45.0 * factor);
  // The wait is untouched, as it is for a buff of one's own.
  EXPECT_DOUBLE_EQ(params.buffs[0].cooldown_seconds, 120.0 * factor);

  // The reader's own Buff Duration is not what times somebody else's cast.
  ASSERT_TRUE(state.character.LearnSkill(mastery, 10));
  params = ComputeCombatParams(state);
  ASSERT_EQ(params.buffs.size(), 1u);
  EXPECT_DOUBLE_EQ(params.buffs[0].duration_seconds, 45.0 * factor);
}

// Buff Mastery's lever lengthens the buff and leaves the wait alone, which is
// the whole of what it buys: a buff up longer without coming round sooner.
TEST(ComputeCombatParamsTest, BuffDurationLengthensTheBuffAndNotTheWait) {
  Skill resonance;
  resonance.set_name("Dark Resonance");
  resonance.set_kind(SKILL_KIND_ACTIVE);
  PlaceIn(resonance, JOB_ADVANCEMENT_SWORDMAN);
  resonance.set_max_level(30);
  resonance.set_cooldown_seconds(70.0);
  Buff* buff = resonance.mutable_buff();
  buff->set_duration_seconds(30.0);
  buff->mutable_base()->set_final_dmg_pct(0.50);

  Skill mastery;
  mastery.set_name("Buff Mastery");
  mastery.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(mastery, JOB_ADVANCEMENT_SWORDMAN);
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

// A shedding buff is run as one window per stage: three clocks off one cast,
// falling ten seconds apart, each granting a stage's worth. Only the first is
// charged the cast, since one press raises them all.
TEST(ComputeCombatParamsTest, ASheddingBuffRunsOneWindowPerStage) {
  Skill overdrive;
  overdrive.set_name("Arcane Overdrive");
  overdrive.set_kind(SKILL_KIND_ACTIVE);
  PlaceIn(overdrive, JOB_ADVANCEMENT_SWORDMAN);
  overdrive.set_max_level(30);
  overdrive.set_base_delay_ms(120);
  overdrive.set_cooldown_seconds(120.0);
  Buff* buff = overdrive.mutable_buff();
  buff->set_duration_seconds(30.0);
  buff->set_stages(3);
  buff->set_stage_interval_seconds(10.0);
  buff->mutable_base()->set_final_dmg_pct(0.07);

  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}},
                  {{"arcane_overdrive", overdrive}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 1);
  ASSERT_TRUE(state.character.LearnSkill(overdrive, 1));

  CombatParams params = ComputeCombatParams(state);
  double factor = GameSpeedFactor(state.character.proto().level());
  ASSERT_EQ(params.buffs.size(), 3u);
  EXPECT_DOUBLE_EQ(params.buffs[0].duration_seconds, 10.0 * factor);
  EXPECT_DOUBLE_EQ(params.buffs[1].duration_seconds, 20.0 * factor);
  EXPECT_DOUBLE_EQ(params.buffs[2].duration_seconds, 30.0 * factor);
  for (const BuffOption& stage : params.buffs) {
    EXPECT_EQ(stage.name, "Arcane Overdrive");
    EXPECT_DOUBLE_EQ(stage.cooldown_seconds, 120.0 * factor);
  }
  EXPECT_GT(params.buffs[0].cast_seconds, 0.0);
  EXPECT_DOUBLE_EQ(params.buffs[1].cast_seconds, 0.0);
  EXPECT_DOUBLE_EQ(params.buffs[2].cast_seconds, 0.0);

  // All three up is three stages of the lever; the last stage alone is one.
  EXPECT_GT(params.Attacks(0b111).front().damage_per_hit.front(),
            params.Attacks(0b100).front().damage_per_hit.front());
  EXPECT_GT(params.Attacks(0b100).front().damage_per_hit.front(),
            params.attacks.front().damage_per_hit.front());
}

// A buff shorter than its stages sheds what it has time for and takes the rest
// down with it: level 1 Arcane Overdrive stands 15 seconds, so its second and
// third stages both end there rather than running past the buff.
TEST(ComputeCombatParamsTest, ASheddingBuffNeverOutlastsItsDuration) {
  Skill overdrive;
  overdrive.set_name("Arcane Overdrive");
  overdrive.set_kind(SKILL_KIND_ACTIVE);
  PlaceIn(overdrive, JOB_ADVANCEMENT_SWORDMAN);
  overdrive.set_max_level(30);
  overdrive.set_base_delay_ms(120);
  overdrive.set_cooldown_seconds(120.0);
  Buff* buff = overdrive.mutable_buff();
  buff->set_duration_seconds(15.0);
  buff->set_stages(3);
  buff->set_stage_interval_seconds(10.0);
  buff->mutable_base()->set_final_dmg_pct(0.07);

  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}},
                  {{"arcane_overdrive", overdrive}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 1);
  ASSERT_TRUE(state.character.LearnSkill(overdrive, 1));

  CombatParams params = ComputeCombatParams(state);
  double factor = GameSpeedFactor(state.character.proto().level());
  ASSERT_EQ(params.buffs.size(), 3u);
  EXPECT_DOUBLE_EQ(params.buffs[0].duration_seconds, 10.0 * factor);
  EXPECT_DOUBLE_EQ(params.buffs[1].duration_seconds, 15.0 * factor);
  EXPECT_DOUBLE_EQ(params.buffs[2].duration_seconds, 15.0 * factor);
}

// GMS marks every fifth job skill notIncBuffDuration, so the matrix stands
// outside the lever: the same book lengthens an ordinary buff and leaves a
// node's alone.
TEST(ComputeCombatParamsTest, BuffDurationSkipsAVNode) {
  Skill node;
  node.set_name("Arcane Overdrive");
  node.set_kind(SKILL_KIND_ACTIVE);
  PlaceIn(node, JOB_ADVANCEMENT_COMMON);
  node.set_v_node(V_NODE_KIND_COMMON);
  node.set_max_level(MaxVNodeLevel(V_NODE_KIND_COMMON));
  node.set_cooldown_seconds(120.0);
  node.mutable_buff()->set_duration_seconds(30.0);
  node.mutable_buff()->mutable_base()->set_final_dmg_pct(0.20);

  Skill mastery;
  mastery.set_name("Buff Mastery");
  mastery.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(mastery, JOB_ADVANCEMENT_SWORDMAN);
  mastery.set_max_level(10);
  mastery.mutable_base()->set_buff_duration_pct(0.05);
  mastery.mutable_per_level()->set_buff_duration_pct(0.05);

  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}},
                  {{"arcane_overdrive", node}, {"buff_mastery", mastery}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 11);
  while (state.character.proto().job_stage() < kFifthJobStage) {
    state.character.AdvanceJob(state.character.proto().job());
  }
  state.character.AddVPoints(VNodeCost(V_NODE_KIND_COMMON, 0, 1));
  ASSERT_TRUE(state.character.LearnSkill(node, 1));
  ASSERT_TRUE(state.character.LearnSkill(mastery, 10));

  CombatParams params = ComputeCombatParams(state);
  double factor = GameSpeedFactor(state.character.proto().level());
  ASSERT_EQ(params.buffs.size(), 1u);
  EXPECT_DOUBLE_EQ(params.buffs[0].duration_seconds, 30.0 * factor);
}

// Flame Sweep's shape: the swing leaves a burn on what it reached, priced on
// its own multiplier and given a slot of its own. Nothing on a clock of its
// own leaves one -- a summon marks nothing.
TEST(ComputeCombatParamsTest, ASwingCanLeaveABurn) {
  Skill sweep;
  sweep.set_name("Flame Sweep");
  sweep.set_kind(SKILL_KIND_ATTACK);
  PlaceIn(sweep, JOB_ADVANCEMENT_SWORDMAN);
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
  PlaceIn(summon, JOB_ADVANCEMENT_SWORDMAN);
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
  EXPECT_EQ(params.dot_count, 2);
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

  // A summon states its own burn and leaves it: what a clock of its own drops
  // is the poison the CHARACTER carries, not the mark the skill itself makes.
  // It writes a slot of its own, so the two burns never overwrite each other.
  ASSERT_EQ(params.auto_attacks.size(), 1u);
  EXPECT_EQ(params.auto_attacks[0].name, "Ifrit");
  ASSERT_EQ(params.auto_attacks[0].dots.size(), 1u);
  EXPECT_EQ(params.auto_attacks[0].dots[0].slot, 1);

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
  PlaceIn(venom, JOB_ADVANCEMENT_SWORDMAN);
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
  PlaceIn(raid, JOB_ADVANCEMENT_SWORDMAN);
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

// Mist Eruption's half: a boost that lifts the mark a skill leaves rather than
// the strike that leaves it. The burn already takes what a boost hands the
// swing, through the stat line it is priced off -- only the multiplier is its
// own, so only that lever has to be aimed at it.
TEST(ComputeCombatParamsTest, ABoostCanLiftTheNamedSkillsBurn) {
  Skill venom;
  venom.set_name("Venom");
  venom.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(venom, JOB_ADVANCEMENT_SWORDMAN);
  venom.set_max_level(10);
  Dot* poison = venom.mutable_dot();
  poison->set_interval_seconds(1.0);
  poison->set_duration_seconds(6.0);
  poison->mutable_base()->set_skill_pct(0.54);

  Skill mist;
  mist.set_name("Poison Mist");
  mist.set_kind(SKILL_KIND_ATTACK);
  PlaceIn(mist, JOB_ADVANCEMENT_SWORDMAN);
  mist.set_max_level(20);
  mist.set_base_delay_ms(660);
  mist.set_max_enemies(6);
  mist.mutable_base()->set_skill_pct(0.50);
  Dot* fog = mist.mutable_dot();
  fog->set_interval_seconds(1.0);
  fog->set_duration_seconds(9.6);
  fog->set_lines(1);
  fog->mutable_base()->set_skill_pct(2.40);

  // A second burn, so a boost naming one cannot be read as lifting them all.
  Skill haze;
  haze.set_name("Flame Haze");
  haze.set_kind(SKILL_KIND_ATTACK);
  PlaceIn(haze, JOB_ADVANCEMENT_SWORDMAN);
  haze.set_max_level(20);
  haze.set_base_delay_ms(660);
  haze.mutable_base()->set_skill_pct(1.60);
  *haze.mutable_dot() = *fog;

  Skill eruption;
  eruption.set_name("Mist Eruption");
  eruption.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(eruption, JOB_ADVANCEMENT_SWORDMAN);
  eruption.set_max_level(20);
  SkillBoost* boost = eruption.add_boost();
  boost->set_skill_name("Poison Mist");
  boost->set_dot_skill_pct(0.315);
  boost->set_dot_skill_pct_per_level(0.015);
  // The burn's other half, which is its clock. Poison Mist - Aftermath's.
  boost->set_dot_duration_seconds(6.0);

  Mob snail = MakeMob("Snail", 15);
  std::map<std::string, Skill> book = {
      {"venom", venom}, {"poison_mist", mist}, {"flame_haze", haze}};
  GameState bare({}, {}, {}, {{"snail", snail}}, {{"field", TwoSnailMap()}},
                 book);
  bare.current_map = "field";
  EquipSword(bare);
  GrantFirstJobSp(bare, 22);
  ASSERT_TRUE(bare.character.LearnSkill(venom, 10));
  ASSERT_TRUE(bare.character.LearnSkill(mist, 1));
  ASSERT_TRUE(bare.character.LearnSkill(haze, 1));

  book["mist_eruption"] = eruption;
  GameState lifted({}, {}, {}, {{"snail", snail}}, {{"field", TwoSnailMap()}},
                   book);
  lifted.current_map = "field";
  EquipSword(lifted);
  GrantFirstJobSp(lifted, 42);
  ASSERT_TRUE(lifted.character.LearnSkill(venom, 10));
  ASSERT_TRUE(lifted.character.LearnSkill(mist, 1));
  ASSERT_TRUE(lifted.character.LearnSkill(haze, 1));
  ASSERT_TRUE(lifted.character.LearnSkill(eruption, 20));

  CombatParams plain = ComputeCombatParams(bare);
  CombatParams paid = ComputeCombatParams(lifted);
  const AttackOption* bare_mist = FindAttack(plain, "Poison Mist");
  const AttackOption* paid_mist = FindAttack(paid, "Poison Mist");
  ASSERT_NE(bare_mist, nullptr);
  ASSERT_NE(paid_mist, nullptr);
  // The character's poison first, the skill's own burn after it.
  ASSERT_EQ(bare_mist->dots.size(), 2u);
  ASSERT_EQ(paid_mist->dots.size(), 2u);
  // 240% + 60 points at Mist Eruption 20, which is a quarter more per tick.
  EXPECT_NEAR(paid_mist->dots[1].damage[0] / bare_mist->dots[1].damage[0],
              3.00 / 2.40, 1e-6);
  // And six seconds longer, on top of the 9.6 the mist states.
  EXPECT_NEAR(
      paid_mist->dots[1].duration_seconds / bare_mist->dots[1].duration_seconds,
      15.6 / 9.6, 1e-6);
  // The strike that lays the mist is untouched: the lever names the mark.
  EXPECT_NEAR(paid_mist->damage_per_hit[0], bare_mist->damage_per_hit[0], 1e-9);
  // So is the poison on the claw, which is the character's and not the
  // skill's, and so is the other skill's burn.
  EXPECT_NEAR(paid_mist->dots[0].damage[0], bare_mist->dots[0].damage[0], 1e-9);
  const AttackOption* bare_haze = FindAttack(plain, "Flame Haze");
  const AttackOption* paid_haze = FindAttack(paid, "Flame Haze");
  ASSERT_NE(bare_haze, nullptr);
  ASSERT_NE(paid_haze, nullptr);
  ASSERT_EQ(paid_haze->dots.size(), 2u);
  EXPECT_NEAR(paid_haze->dots[1].damage[0], bare_haze->dots[1].damage[0], 1e-9);
  EXPECT_NEAR(paid_haze->dots[1].duration_seconds,
              bare_haze->dots[1].duration_seconds, 1e-9);
  EXPECT_NEAR(paid_mist->dots[0].duration_seconds,
              bare_mist->dots[0].duration_seconds, 1e-9);
}

// Megiddo Flame's shape: the swing is thrown as scattered strikes, and what
// the fight reads is the count and the cut a repeat takes. The damage chain
// knows nothing about it -- every strike is the same swing.
TEST(ComputeCombatParamsTest, AScatteredSwingCarriesItsStrikesAndItsCut) {
  Skill megiddo;
  megiddo.set_name("Megiddo Flame");
  megiddo.set_kind(SKILL_KIND_ATTACK);
  PlaceIn(megiddo, JOB_ADVANCEMENT_SWORDMAN);
  megiddo.set_max_level(1);
  megiddo.set_base_delay_ms(660);
  megiddo.set_max_enemies(11);
  megiddo.set_lines(4);
  megiddo.mutable_base()->set_skill_pct(3.80);
  megiddo.mutable_scatter()->set_hits(11);
  megiddo.mutable_scatter()->set_repeat_final_dmg_pct(-0.55);

  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}}, {{"megiddo", megiddo}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 1);
  ASSERT_TRUE(state.character.LearnSkill(megiddo, 1));

  const AttackOption* swing =
      FindAttack(ComputeCombatParams(state), "Megiddo Flame");
  ASSERT_NE(swing, nullptr);
  EXPECT_EQ(swing->scatter_hits, 11);
  EXPECT_NEAR(swing->scatter_repeat_kept, 0.45, 1e-9);
  // One strike's damage, not eleven: what the count buys is landings, and the
  // fight is what spends them.
  EXPECT_EQ(swing->lines, 4);
}

// Throw Blasting's charms: one bank, hung on every swing the character has
// rather than on the one skill a magazine names. The load is the character's
// own swing for all that -- it is what the press throws -- so the Mark goes
// off on it, which is what tells it from the shuriken a swing merely sets off.
TEST(ComputeCombatParamsTest, ALoadSpentByEverySwingHangsOnAllOfThem) {
  Skill quad;
  quad.set_name("Quad Star");
  quad.set_kind(SKILL_KIND_ATTACK);
  PlaceIn(quad, JOB_ADVANCEMENT_ROGUE);
  quad.set_max_level(20);
  quad.add_required_equip_type(EQUIP_TYPE_CLAW);
  quad.set_lines(4);
  quad.mutable_base()->set_skill_pct(3.27);

  Skill showdown = quad;
  showdown.set_name("Showdown");
  showdown.set_max_enemies(6);

  Skill mark;
  mark.set_name("Assassin's Mark");
  mark.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(mark, JOB_ADVANCEMENT_ROGUE);
  mark.set_max_level(1);
  mark.mutable_base()->set_final_attack_chance(0.42);
  mark.mutable_base()->set_final_attack_pct(2.10);

  Skill blasting;
  blasting.set_name("Throw Blasting");
  blasting.set_kind(SKILL_KIND_ACTIVE);
  PlaceIn(blasting, JOB_ADVANCEMENT_ROGUE);
  blasting.set_max_level(20);
  blasting.add_required_equip_type(EQUIP_TYPE_CLAW);
  blasting.set_base_delay_ms(960);
  blasting.mutable_buff()->set_duration_seconds(60.0);
  Magazine* charm = blasting.mutable_buff()->mutable_magazine();
  charm->set_label("Explosive Charm");
  charm->set_charges(46);
  charm->set_charges_per_swing(3);
  charm->set_spent_by_every_swing(true);
  charm->set_recharge_seconds(10.0);
  charm->set_recharge_max(1);
  charm->set_max_enemies(6);
  charm->set_lines(5);
  charm->mutable_base()->set_skill_pct(4.94);

  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}},
                  {{"quad_star", quad},
                   {"showdown", showdown},
                   {"mark", mark},
                   {"throw_blasting", blasting}});
  state.current_map = "field";
  EquipClaw(state);
  GrantFirstJobSp(state, 4, JOB_ROGUE);
  ASSERT_TRUE(state.character.LearnSkill(quad, 1));
  ASSERT_TRUE(state.character.LearnSkill(showdown, 1));
  ASSERT_TRUE(state.character.LearnSkill(mark, 1));
  ASSERT_TRUE(state.character.LearnSkill(blasting, 1));

  CombatParams params = ComputeCombatParams(state);
  const AttackOption* load = FindAttack(params, "Explosive Charm");
  ASSERT_NE(load, nullptr);
  // Out of the choice: a load riding other presses is no button of its own.
  EXPECT_GE(load->spent_by_attack, 0);
  EXPECT_EQ(load->charges, 46);
  EXPECT_EQ(load->charges_per_swing, 3);
  EXPECT_DOUBLE_EQ(load->recharge_seconds, 10.0);
  EXPECT_EQ(load->recharge_max, 1);
  // What the press throws, so the character's own Final Attack rides it --
  // unlike the strike a swing merely sets off beside itself.
  EXPECT_FALSE(load->final_attack_rolls.empty());

  const AttackOption* quad_swing = FindAttack(params, "Quad Star");
  const AttackOption* showdown_swing = FindAttack(params, "Showdown");
  ASSERT_NE(quad_swing, nullptr);
  ASSERT_NE(showdown_swing, nullptr);
  ASSERT_NE(quad_swing->loaded, nullptr);
  ASSERT_NE(showdown_swing->loaded, nullptr);
  // One bank, however many buttons spend it.
  EXPECT_EQ(quad_swing->loaded_attack, showdown_swing->loaded_attack);
  // The bare poke is not an attack skill, and neither is the load itself.
  EXPECT_EQ(params.attacks[0].name, "Attack");
  EXPECT_EQ(params.attacks[0].loaded, nullptr);
  EXPECT_EQ(load->loaded, nullptr);
}

// Showdown's shuriken: a second attack the swing sets off, with its own reach,
// its own strikes, its own scatter and a wait of its own. Nothing rides it --
// it is not the character's swing -- and a skill on its own clock never
// carries one.
TEST(ComputeCombatParamsTest, ASwingCanSetOffAStrikeOnAWaitOfItsOwn) {
  Skill showdown;
  showdown.set_name("Showdown");
  showdown.set_kind(SKILL_KIND_ATTACK);
  PlaceIn(showdown, JOB_ADVANCEMENT_SWORDMAN);
  showdown.set_max_level(30);
  showdown.set_base_delay_ms(780);
  showdown.set_max_enemies(6);
  showdown.set_lines(2);
  showdown.mutable_base()->set_skill_pct(3.73);
  SideStrike* side = showdown.mutable_side_strike();
  side->set_label("Shuriken");
  side->set_lines(6);
  side->set_cooldown_seconds(5.0);
  side->mutable_base()->set_skill_pct(0.09);
  side->mutable_base()->set_normal_skill_pct(2.00);
  side->mutable_scatter()->set_hits(6);
  side->mutable_scatter()->set_repeat_final_dmg_pct(-0.50);

  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}}, {{"showdown", showdown}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 1);
  ASSERT_TRUE(state.character.LearnSkill(showdown, 1));

  CombatParams params = ComputeCombatParams(state);
  double factor = GameSpeedFactor(state.character.proto().level());
  ASSERT_EQ(params.attacks.size(), 2u);
  const AttackOption& swing = params.attacks[1];
  ASSERT_NE(swing.side, nullptr);
  EXPECT_EQ(swing.side->name, "Shuriken");
  // Said nothing about its reach, so it goes as wide as the swing that lit it.
  EXPECT_EQ(swing.side->max_enemies, 6);
  EXPECT_DOUBLE_EQ(swing.side->cooldown_seconds, 5.0 * factor);
  // Six shurikens, each hunting one enemy: they spread before they double up,
  // and a second one on the same enemy keeps half of itself.
  EXPECT_EQ(swing.side->scatter_hits, 6);
  EXPECT_NEAR(swing.side->scatter_repeat_kept, 0.50, 1e-9);
  // Six lines of 209% against the swing's two of 373%: the strike is priced on
  // its own multiplier, and no mob here is a boss so it takes the whole of the
  // bonus against an ordinary monster.
  EXPECT_NEAR(swing.side->damage_per_hit[0] / (swing.damage_per_hit[0] / 2.0),
              6.0 * 2.09 / 3.73, 0.02);
  // The strike is nobody's swing, so the swing's own riders are not on it.
  EXPECT_TRUE(swing.side->final_attack_rolls.empty());
  EXPECT_TRUE(swing.side->dots.empty());
  EXPECT_EQ(params.attacks[0].side, nullptr);
}

// Mighty Mjolnir's shockwave: one side strike landed four times, each rolling
// on its own, and no wait of its own -- the skill's cooldown is the wait.
TEST(ComputeCombatParamsTest, ASideStrikeCanBeLandedSeveralTimes) {
  Skill mjolnir;
  mjolnir.set_name("Mighty Mjolnir");
  mjolnir.set_kind(SKILL_KIND_ATTACK);
  PlaceIn(mjolnir, JOB_ADVANCEMENT_SWORDMAN);
  mjolnir.set_max_level(30);
  mjolnir.set_base_delay_ms(570);
  mjolnir.set_cooldown_seconds(12.0);
  mjolnir.set_max_enemies(4);
  mjolnir.set_lines(6);
  mjolnir.mutable_base()->set_skill_pct(2.45);
  SideStrike* side = mjolnir.mutable_side_strike();
  side->set_label("Shockwave");
  side->set_max_enemies(6);
  side->set_lines(9);
  side->set_casts(4);
  side->mutable_base()->set_skill_pct(2.86);

  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}}, {{"mjolnir", mjolnir}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 1);
  ASSERT_TRUE(state.character.LearnSkill(mjolnir, 1));

  CombatParams params = ComputeCombatParams(state);
  ASSERT_EQ(params.attacks.size(), 2u);
  const AttackOption& swing = params.attacks[1];
  ASSERT_NE(swing.side, nullptr);
  // Four shockwaves of nine lines against the swing's six of 245%.
  EXPECT_NEAR(swing.side->damage_per_hit[0] / (swing.damage_per_hit[0] / 6.0),
              4.0 * 9.0 * 2.86 / 2.45, 0.02);
  // Each rolls for itself, so there are four groups rather than one of
  // thirty-six lines.
  EXPECT_EQ(swing.side->groups.size(), 4u);
  EXPECT_DOUBLE_EQ(swing.side->cooldown_seconds, 0.0);
}

// A lever the SKILL states rides its swing and stops there: GMS gives Mighty
// Mjolnir's extra critical rate to the hammer that tracks a target down, not
// to the shockwave behind it. A strike states its own or has none.
TEST(ComputeCombatParamsTest, ASwingsOwnLeversStopAtTheStrikeItSetsOff) {
  Skill mjolnir;
  mjolnir.set_name("Mighty Mjolnir");
  mjolnir.set_kind(SKILL_KIND_ATTACK);
  PlaceIn(mjolnir, JOB_ADVANCEMENT_SWORDMAN);
  mjolnir.set_max_level(1);
  mjolnir.set_base_delay_ms(570);
  mjolnir.set_cooldown_seconds(36.0);
  mjolnir.set_max_enemies(4);
  mjolnir.set_lines(6);
  mjolnir.mutable_base()->set_skill_pct(2.45);
  SideStrike* side = mjolnir.mutable_side_strike();
  side->set_label("Shockwave");
  side->set_max_enemies(6);
  side->set_lines(9);
  side->mutable_base()->set_skill_pct(2.86);

  GameState bare({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                 {{"field", TwoSnailMap()}}, {{"mjolnir", mjolnir}});
  bare.current_map = "field";
  EquipSword(bare);
  GrantFirstJobSp(bare, 1);
  ASSERT_TRUE(bare.character.LearnSkill(mjolnir, 1));
  CombatParams without = ComputeCombatParams(bare);

  mjolnir.mutable_base()->set_crit_rate(0.50);
  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}}, {{"mjolnir", mjolnir}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 1);
  ASSERT_TRUE(state.character.LearnSkill(mjolnir, 1));
  CombatParams with = ComputeCombatParams(state);

  int i = IndexOfAttack(with.attacks, "Mighty Mjolnir");
  int j = IndexOfAttack(without.attacks, "Mighty Mjolnir");
  ASSERT_GE(i, 0);
  ASSERT_GE(j, 0);
  EXPECT_GT(with.attacks[i].damage_per_hit[0],
            without.attacks[j].damage_per_hit[0]);
  ASSERT_NE(with.attacks[i].side, nullptr);
  ASSERT_NE(without.attacks[j].side, nullptr);
  EXPECT_DOUBLE_EQ(with.attacks[i].side->damage_per_hit[0],
                   without.attacks[j].side->damage_per_hit[0]);
}

// GMS's Showdown - Reinforce lifts the talisman and says in so many words that
// the shuriken is left out of it. Anything the book aims at the skill by name
// stops at the swing.
TEST(ComputeCombatParamsTest, ABoostStopsAtTheStrikeTheSwingSetsOff) {
  Skill showdown;
  showdown.set_name("Showdown");
  showdown.set_kind(SKILL_KIND_ATTACK);
  PlaceIn(showdown, JOB_ADVANCEMENT_SWORDMAN);
  showdown.set_max_level(30);
  showdown.set_base_delay_ms(780);
  showdown.set_max_enemies(6);
  showdown.set_lines(2);
  showdown.mutable_base()->set_skill_pct(3.73);
  SideStrike* side = showdown.mutable_side_strike();
  side->set_label("Shuriken");
  side->set_max_enemies(6);
  side->set_lines(6);
  side->set_cooldown_seconds(5.0);
  side->mutable_base()->set_skill_pct(0.09);

  Skill reinforce;
  reinforce.set_name("Showdown - Reinforce");
  reinforce.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(reinforce, JOB_ADVANCEMENT_SWORDMAN);
  reinforce.set_max_level(1);
  SkillBoost* boost = reinforce.add_boost();
  boost->set_skill_name("Showdown");
  boost->mutable_effect()->set_damage_pct(0.20);

  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}},
                  {{"showdown", showdown}, {"reinforce", reinforce}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 2);
  ASSERT_TRUE(state.character.LearnSkill(showdown, 1));

  CombatParams before = ComputeCombatParams(state);
  ASSERT_TRUE(state.character.LearnSkill(reinforce, 1));
  CombatParams after = ComputeCombatParams(state);

  ASSERT_NE(before.attacks[1].side, nullptr);
  ASSERT_NE(after.attacks[1].side, nullptr);
  EXPECT_NEAR(after.attacks[1].damage_per_hit[0],
              before.attacks[1].damage_per_hit[0] * 1.20, 1e-6);
  EXPECT_DOUBLE_EQ(after.attacks[1].side->damage_per_hit[0],
                   before.attacks[1].side->damage_per_hit[0]);
}

// Puncture's shape: the buff hangs off an ATTACK, so it is laid by that swing
// rather than raised on a wait of its own. The fight needs to know which swing
// lays it, and that has to be the swing's index in the attack list.
TEST(ComputeCombatParamsTest, ABuffOnAnAttackIsLaidByThatSwing) {
  Skill puncture;
  puncture.set_name("Puncture");
  puncture.set_kind(SKILL_KIND_ATTACK);
  PlaceIn(puncture, JOB_ADVANCEMENT_SWORDMAN);
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
  // Tagged in the buffed window too, which is built the moment it is read.
  ASSERT_EQ(gated.buffed.size(), 1u);
  ASSERT_EQ(gated.AutoAttacks(1).size(), 1u);
  EXPECT_EQ(gated.AutoAttacks(1)[0].needs_buff, 0);
  // A wound borrowing the swing's reach lands once a tick and never runs out.
  EXPECT_EQ(gated.auto_attacks[0].strikes_per_pulse, 1);
  EXPECT_EQ(gated.auto_attacks[0].max_pulses, 0);

  // Darkness Aura's shape: the pulse states a recovery of its own, which the
  // clearing of the swing's riders must not take with it -- the aura heals for
  // every beat, not for the cast that raised it.
  tick->mutable_base()->set_hp_recover_pct(0.03);
  tick->mutable_per_level()->set_hp_recover_pct(0.01);
  state.skills["puncture"] = puncture;
  CombatParams healing = ComputeCombatParams(state);
  ASSERT_EQ(healing.auto_attacks.size(), 1u);
  EXPECT_DOUBLE_EQ(healing.auto_attacks[0].hp_recover_pct, 0.03);
  EXPECT_DOUBLE_EQ(healing.attacks[1].hp_recover_pct, 0.0);

  // Cry Valhalla's shape over the same field: its own reach, three strikes a
  // tick, and a count that runs out inside the window.
  tick->set_max_enemies(6);
  tick->set_casts(3);
  tick->set_max_pulses(12);
  state.skills["puncture"] = puncture;
  CombatParams capped = ComputeCombatParams(state);
  ASSERT_EQ(capped.auto_attacks.size(), 1u);
  EXPECT_EQ(capped.auto_attacks[0].max_enemies, 6);
  EXPECT_EQ(capped.auto_attacks[0].strikes_per_pulse, 3);
  EXPECT_EQ(capped.auto_attacks[0].max_pulses, 12);
}

// Storm of Arrows' shape: the rain carries one more of its own lines, built as
// a strike of exactly one so the fight can land as many as the swing earned.
TEST(ComputeCombatParamsTest, APulseThatGrowsWithTheCrowdCarriesOneMoreLine) {
  Skill storm;
  storm.set_name("Storm of Arrows");
  storm.set_kind(SKILL_KIND_ACTIVE);
  PlaceIn(storm, JOB_ADVANCEMENT_SWORDMAN);
  storm.set_max_level(30);
  storm.set_cooldown_seconds(120.0);
  Buff* buff = storm.mutable_buff();
  buff->set_duration_seconds(70.0);
  BuffPulse* rain = buff->mutable_pulse();
  rain->set_label("Arrow Rain");
  rain->set_cast_interval_seconds(5.0);
  rain->set_max_enemies(10);
  rain->set_lines(7);
  rain->set_lines_per_extra_enemy(2);
  rain->set_max_extra_lines(8);
  rain->mutable_base()->set_skill_pct(16.5);

  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}}, {{"storm_of_arrows", storm}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 1);
  ASSERT_TRUE(state.character.LearnSkill(storm, 1));

  CombatParams params = ComputeCombatParams(state);
  ASSERT_EQ(params.auto_attacks.size(), 1u);
  const AttackOption& pulse = params.auto_attacks[0];
  EXPECT_EQ(pulse.lines, 7);
  EXPECT_EQ(pulse.lines_per_extra_enemy, 2);
  EXPECT_EQ(pulse.max_extra_lines, 8);
  ASSERT_NE(pulse.extra_line, nullptr);
  // One of the rain's seven, and the rest of the pulse carried whole.
  EXPECT_EQ(pulse.extra_line->lines, 1);
  EXPECT_EQ(pulse.extra_line->max_enemies, 10);
  ASSERT_FALSE(pulse.damage_per_hit.empty());
  EXPECT_NEAR(pulse.extra_line->damage_per_hit[0],
              pulse.damage_per_hit[0] / 7.0, 1e-6);

  // A ramp with no cap on it is not a ramp: nothing is built.
  rain->clear_max_extra_lines();
  state.skills["storm_of_arrows"] = storm;
  EXPECT_EQ(ComputeCombatParams(state).auto_attacks[0].extra_line, nullptr);
}

// Poison Chain's shape: the pulse carries one stronger form per helping of
// poison, each the whole strike again at its own damage. The step climbs with
// the level, as every other number here does.
TEST(ComputeCombatParamsTest, ARampedPulseCarriesAFormPerHelping) {
  Skill chain;
  chain.set_name("Poison Chain");
  chain.set_kind(SKILL_KIND_ACTIVE);
  PlaceIn(chain, JOB_ADVANCEMENT_SWORDMAN);
  chain.set_max_level(30);
  chain.set_cooldown_seconds(25.0);
  Buff* buff = chain.mutable_buff();
  buff->set_duration_seconds(20.0);
  BuffPulse* blast = buff->mutable_pulse();
  blast->set_label("Poison Explosion");
  blast->set_cast_interval_seconds(2.0);
  blast->set_lines(5);
  blast->set_max_pulses(9);
  blast->set_max_repeats(3);
  blast->set_skill_pct_per_repeat(0.31);
  blast->set_skill_pct_per_repeat_per_level(0.01);
  blast->set_final_repeat_strike(true);
  blast->mutable_base()->set_skill_pct(1.56);

  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}}, {{"poison_chain", chain}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 1);
  ASSERT_TRUE(state.character.LearnSkill(chain, 1));

  CombatParams params = ComputeCombatParams(state);
  ASSERT_EQ(params.auto_attacks.size(), 1u);
  const AttackOption& pulse = params.auto_attacks[0];
  ASSERT_EQ(pulse.repeats.size(), 3u);
  EXPECT_TRUE(pulse.final_repeat_strike);
  ASSERT_FALSE(pulse.damage_per_hit.empty());
  // 1.56 opening, and 0.31 more on each form at level 1.
  for (int repeat = 1; repeat <= 3; ++repeat) {
    EXPECT_NEAR(pulse.repeats[repeat - 1]->damage_per_hit[0],
                pulse.damage_per_hit[0] * (1.56 + 0.31 * repeat) / 1.56, 1e-6)
        << "repeat " << repeat;
  }

  // A step with no cap on it is not a ramp: nothing is built.
  blast->clear_max_repeats();
  state.skills["poison_chain"] = chain;
  EXPECT_TRUE(ComputeCombatParams(state).auto_attacks[0].repeats.empty());
}

// Dark Lord's Omen's shape: the tick throws a volley worth what the crowd is
// and a fixed handful besides, the second a strike of its own so a lone boss
// takes all of it -- and the scroll bursts on a shape of its own as it leaves.
TEST(ComputeCombatParamsTest, APulseThrowsItsFixedStrikesAndBurstsAsItLeaves) {
  Skill omen;
  omen.set_name("Dark Lord's Omen");
  omen.set_kind(SKILL_KIND_ACTIVE);
  PlaceIn(omen, JOB_ADVANCEMENT_SWORDMAN);
  omen.set_max_level(30);
  omen.set_cooldown_seconds(60.0);
  Buff* buff = omen.mutable_buff();
  buff->set_duration_seconds(12.0);
  BuffPulse* stars = buff->mutable_pulse();
  stars->set_label("Throwing Stars");
  stars->set_cast_interval_seconds(0.99);
  stars->set_lines(6);
  stars->set_max_enemies(7);
  stars->set_max_pulses(12);
  stars->mutable_base()->set_skill_pct(7.27);
  // A lever the pulse states rides the whole turret, the burst included -- it
  // is what holds the damage relation below.
  stars->mutable_base()->set_crit_rate(0.20);
  stars->mutable_fixed_strikes()->set_hits(7);
  SwingHit* burst = stars->mutable_final_strike();
  burst->set_label("Explosion");
  burst->set_lines(12);
  burst->set_max_enemies(12);
  burst->mutable_base()->set_skill_pct(16.32);

  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}}, {{"omen", omen}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 1);
  ASSERT_TRUE(state.character.LearnSkill(omen, 1));

  CombatParams params = ComputeCombatParams(state);
  ASSERT_EQ(params.auto_attacks.size(), 2u);
  const AttackOption& volley = params.auto_attacks[0];
  const AttackOption& fixed = params.auto_attacks[1];
  // One turret, two things it fires: the same clock, the same window, and the
  // same buff standing over both.
  EXPECT_EQ(fixed.interval_seconds, volley.interval_seconds);
  EXPECT_EQ(fixed.max_pulses, volley.max_pulses);
  EXPECT_EQ(fixed.needs_buff, volley.needs_buff);
  EXPECT_GE(volley.needs_buff, 0);
  // Six lines the crowd earns against seven single strikes scattered over it,
  // each worth exactly one of the volley's.
  EXPECT_EQ(volley.lines, 6);
  EXPECT_EQ(fixed.lines, 1);
  EXPECT_EQ(fixed.scatter_hits, 7);
  EXPECT_EQ(fixed.max_enemies, volley.max_enemies);
  ASSERT_FALSE(volley.damage_per_hit.empty());
  ASSERT_FALSE(fixed.damage_per_hit.empty());
  EXPECT_NEAR(fixed.damage_per_hit[0], volley.damage_per_hit[0] / 6.0, 1e-6);
  // The burst hangs off the volley's clock rather than standing as a clock of
  // its own, and carries its own damage, lines and reach.
  ASSERT_NE(volley.final_strike, nullptr);
  EXPECT_EQ(fixed.final_strike, nullptr);
  EXPECT_EQ(volley.final_strike->lines, 12);
  EXPECT_EQ(volley.final_strike->max_enemies, 12);
  EXPECT_NEAR(volley.final_strike->damage_per_hit[0],
              volley.damage_per_hit[0] * (16.32 / 7.27) * 12.0 / 6.0, 1e-6);

  // Nothing thrown and nothing to go out on is nothing built: the volley
  // stands alone.
  stars->clear_fixed_strikes();
  stars->clear_final_strike();
  state.skills["omen"] = omen;
  CombatParams bare = ComputeCombatParams(state);
  ASSERT_EQ(bare.auto_attacks.size(), 1u);
  EXPECT_EQ(bare.auto_attacks[0].final_strike, nullptr);
}

// Instinctual Combo's shape: the tear rides Raging Blow rather than a clock,
// so it falls exactly as often as that swing does -- and quickens with it when
// the weapon does. A ride nobody answers to leaves no pulse at all.
TEST(ComputeCombatParamsTest, APulseRidesTheSwingItNames) {
  Skill blow;
  blow.set_name("Raging Blow");
  blow.set_kind(SKILL_KIND_ATTACK);
  PlaceIn(blow, JOB_ADVANCEMENT_SWORDMAN);
  blow.set_max_level(30);
  blow.set_base_delay_ms(660);
  blow.set_max_enemies(8);
  blow.mutable_base()->set_skill_pct(3.87);
  Skill instinct;
  instinct.set_name("Instinctual Combo");
  instinct.set_kind(SKILL_KIND_ACTIVE);
  PlaceIn(instinct, JOB_ADVANCEMENT_SWORDMAN);
  instinct.set_max_level(30);
  instinct.set_cooldown_seconds(120.0);
  Buff* buff = instinct.mutable_buff();
  buff->set_duration_seconds(20.0);
  BuffPulse* tear = buff->mutable_pulse();
  tear->set_label("Tear in Space");
  tear->set_paced_by_skill_name("Raging Blow");
  tear->set_lines(6);
  tear->set_casts(3);
  tear->set_max_enemies(6);
  tear->mutable_base()->set_skill_pct(4.0);

  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}},
                  {{"raging_blow", blow}, {"instinctual_combo", instinct}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 2);
  ASSERT_TRUE(state.character.LearnSkill(blow, 1));
  ASSERT_TRUE(state.character.LearnSkill(instinct, 1));

  CombatParams params = ComputeCombatParams(state);
  ASSERT_EQ(params.attacks.size(), 2u);
  ASSERT_EQ(params.attacks[1].name, "Raging Blow");
  ASSERT_EQ(params.auto_attacks.size(), 1u);
  EXPECT_DOUBLE_EQ(params.auto_attacks[0].interval_seconds,
                   params.attacks[1].swing_seconds);
  EXPECT_EQ(params.auto_attacks[0].strikes_per_pulse, 3);
  EXPECT_EQ(params.auto_attacks[0].max_enemies, 6);
  EXPECT_EQ(params.auto_attacks[0].needs_buff, 0);

  // The ride is the swing's, not the tear's own animation: a slower Raging
  // Blow is a slower tear.
  blow.set_base_delay_ms(900);
  state.skills["raging_blow"] = blow;
  CombatParams slower = ComputeCombatParams(state);
  ASSERT_EQ(slower.auto_attacks.size(), 1u);
  EXPECT_GT(slower.auto_attacks[0].interval_seconds,
            params.auto_attacks[0].interval_seconds);
  EXPECT_DOUBLE_EQ(slower.auto_attacks[0].interval_seconds,
                   slower.attacks[1].swing_seconds);

  // And a swing nobody in the book answers to bleeds nothing.
  tear->set_paced_by_skill_name("Nothing At All");
  state.skills["instinctual_combo"] = instinct;
  EXPECT_TRUE(ComputeCombatParams(state).auto_attacks.empty());
}

// Burning Soul Blade's shape: one buff, two forms, one pulse apiece. Both
// pulses sit in the table tagged with the form that fires them, and each form
// knows where its own pulse is, so the fight can price them without hunting.
//
// Stood up in the first job's book so one skill point buys it. What is under
// test is the buff, and nothing about it asks which book it came from.
TEST(ComputeCombatParamsTest, ABuffWithFormsBuildsAPulseForEach) {
  Skill sword;
  sword.set_name("Burning Soul Blade");
  sword.set_kind(SKILL_KIND_ACTIVE);
  sword.set_max_level(30);
  sword.set_cooldown_seconds(120.0);
  PlaceIn(sword, JOB_ADVANCEMENT_SWORDMAN);
  Buff* summon = sword.mutable_buff();
  Stance* mobile = summon->add_stance();
  mobile->set_label("Mobile");
  mobile->set_duration_seconds(20.0);
  BuffPulse* dense = mobile->mutable_pulse();
  dense->set_cast_interval_seconds(0.81);
  dense->set_lines(12);
  dense->set_max_enemies(8);
  dense->mutable_base()->set_skill_pct(4.70);
  Stance* stationary = summon->add_stance();
  stationary->set_label("Stationary");
  stationary->set_duration_seconds(120.0);
  BuffPulse* thin = stationary->mutable_pulse();
  thin->set_cast_interval_seconds(1.0);
  thin->set_lines(6);
  thin->set_max_enemies(8);
  thin->mutable_base()->set_skill_pct(2.52);

  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}}, {{"sword", sword}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 1);
  ASSERT_TRUE(state.character.LearnSkill(sword, 1));

  CombatParams params = ComputeCombatParams(state);
  double speed = GameSpeedFactor(state.character.proto().level());
  ASSERT_EQ(params.buffs.size(), 1u);
  // The buff's own length is the longest form's, so a caller asking how long
  // it runs still has an answer.
  EXPECT_DOUBLE_EQ(params.buffs[0].duration_seconds, 120.0 * speed);
  ASSERT_EQ(params.buffs[0].stances.size(), 2u);
  EXPECT_DOUBLE_EQ(params.buffs[0].stances[0].duration_seconds, 20.0 * speed);
  EXPECT_DOUBLE_EQ(params.buffs[0].stances[1].duration_seconds, 120.0 * speed);
  EXPECT_DOUBLE_EQ(params.buffs[0].stances[0].pulse_interval_seconds,
                   0.81 * speed);

  // Both pulses are in, each gated on the buff AND on its own form.
  ASSERT_EQ(params.auto_attacks.size(), 2u);
  EXPECT_EQ(params.auto_attacks[0].needs_buff, 0);
  EXPECT_EQ(params.auto_attacks[0].needs_buff_stance, 0);
  EXPECT_EQ(params.auto_attacks[1].needs_buff, 0);
  EXPECT_EQ(params.auto_attacks[1].needs_buff_stance, 1);
  EXPECT_EQ(params.buffs[0].stances[0].pulse_attack, 0);
  EXPECT_EQ(params.buffs[0].stances[1].pulse_attack, 1);
  // The dense form lands twice the lines for twice the damage, which is what
  // makes the choice between the forms a real one.
  EXPECT_GT(params.auto_attacks[0].damage_per_hit[0],
            params.auto_attacks[1].damage_per_hit[0]);
  // A pulse waiting on a buff is not part of what the fight expects to be
  // dealing before one goes up.
  EXPECT_DOUBLE_EQ(params.reference_dps, params.attacks[0].damage_per_hit[0] /
                                             params.attacks[0].swing_seconds);
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
  PlaceIn(orb, JOB_ADVANCEMENT_SWORDMAN);
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
  PlaceIn(mark, JOB_ADVANCEMENT_SWORDMAN);
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

// Sword Illusion's shape: one strike priced once and landed twelve times, with
// five explosions behind it. What it is WORTH is the same as one swing of all
// those lines -- what it buys is that every strike rolls for itself, which is
// what the ledger draws and what the page states.
TEST(ComputeCombatParamsTest, ASwingCanLandTheSameStrikeSeveralTimes) {
  Skill illusion;
  illusion.set_name("Sword Illusion");
  illusion.set_kind(SKILL_KIND_ATTACK);
  PlaceIn(illusion, JOB_ADVANCEMENT_SWORDMAN);
  illusion.set_max_level(30);
  illusion.set_max_enemies(8);
  illusion.set_lines(4);
  illusion.set_casts(12);
  illusion.mutable_base()->set_skill_pct(1.30);
  SwingHit* burst = illusion.add_extra_hit();
  burst->set_label("Explosion");
  burst->set_lines(5);
  burst->set_casts(5);
  burst->mutable_base()->set_skill_pct(2.60);

  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}}, {{"sword_illusion", illusion}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 1);
  ASSERT_TRUE(state.character.LearnSkill(illusion, 1));

  CombatParams params = ComputeCombatParams(state);
  ASSERT_EQ(params.attacks.size(), 2u);
  const AttackOption& swing = params.attacks[1];
  // 130% four times twelve over, and 260% five times five: 62.4 + 65 poke-fuls
  // against the poke's own 100% once.
  double poke = params.attacks[0].damage_per_hit[0];
  EXPECT_NEAR(swing.damage_per_hit[0], 127.4 * poke, 1e-6);
  // Seventeen groups rather than two: twelve slashes of four lines and five
  // explosions of five, each rolling its own mastery and criticals.
  ASSERT_EQ(swing.groups.size(), 17u);
  EXPECT_EQ(swing.groups[0].rolls.lines, 4);
  EXPECT_EQ(swing.groups[11].rolls.lines, 4);
  EXPECT_EQ(swing.groups[12].rolls.lines, 5);
  double summed = 0.0;
  for (const HitGroup& group : swing.groups) {
    summed += group.damage[0];
  }
  EXPECT_NEAR(summed, swing.damage_per_hit[0], 1e-6);
  // The lines the rest of the fight counts are every strike's, not one
  // strike's: a Final Attack on a line and a hit-counting buff both read this.
  EXPECT_EQ(swing.lines, 48);

  // A beat between the strikes takes the fold apart: one strike is priced and
  // the fight is told how many to land, so the dead can be cleared between
  // them. See ASequencedSwingClearsTheDeadBetweenItsStrikes in fight_test.
  Skill walled = illusion;
  walled.clear_extra_hit();
  walled.set_cast_interval_ms(210);
  GameState marched({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                    {{"field", TwoSnailMap()}}, {{"bolt_barrage", walled}});
  marched.current_map = "field";
  EquipSword(marched);
  GrantFirstJobSp(marched, 1);
  ASSERT_TRUE(marched.character.LearnSkill(walled, 1));
  CombatParams walls = ComputeCombatParams(marched);
  const AttackOption& wall = walls.attacks[1];
  EXPECT_EQ(wall.strikes_in_sequence, 12);
  // One strike's worth, one group, one strike's lines -- the twelve are the
  // fight's business now.
  EXPECT_NEAR(wall.damage_per_hit[0], 5.2 * poke, 1e-6);
  ASSERT_EQ(wall.groups.size(), 1u);
  EXPECT_EQ(wall.lines, 4);

  // Folded into one 48-line swing it would be worth exactly the same, which is
  // the claim that lets the two be told apart on shape alone.
  Skill folded = illusion;
  folded.clear_casts();
  folded.set_lines(48);
  folded.clear_extra_hit();
  SwingHit* whole = folded.add_extra_hit();
  whole->set_label("Explosion");
  whole->set_lines(25);
  whole->mutable_base()->set_skill_pct(2.60);
  state.skills["sword_illusion"] = folded;
  CombatParams flattened = ComputeCombatParams(state);
  const AttackOption& flat = flattened.attacks[1];
  EXPECT_NEAR(flat.damage_per_hit[0], swing.damage_per_hit[0], 1e-6);
  EXPECT_EQ(flat.groups.size(), 2u);
}

// Raging Blow's shape: four strikes of which the final two always crit. The
// two halves are the same multiplier, so the whole of the difference between
// them is the certainty -- which is what makes it worth telling them apart.
TEST(ComputeCombatParamsTest, AHalfOfASwingCanBeCertainToCrit) {
  Skill blow;
  blow.set_name("Raging Blow");
  blow.set_kind(SKILL_KIND_ATTACK);
  PlaceIn(blow, JOB_ADVANCEMENT_SWORDMAN);
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

// Assassinate's shape: six strikes and a finishing blow that alone carries
// Murderous Intent's final damage. The swing's own base states none, so the
// opening lines must come out at the bare multiplier.
TEST(ComputeCombatParamsTest, AHalfOfASwingCanCarryItsOwnFinalDamage) {
  Skill strike;
  strike.set_name("Assassinate");
  strike.set_kind(SKILL_KIND_ATTACK);
  PlaceIn(strike, JOB_ADVANCEMENT_SWORDMAN);
  strike.set_max_level(30);
  strike.set_max_enemies(1);
  strike.set_lines(2);
  strike.mutable_base()->set_skill_pct(2.00);
  SwingHit* blow = strike.add_extra_hit();
  blow->set_label("Finishing Blow");
  blow->set_lines(2);
  blow->mutable_base()->set_skill_pct(2.00);
  blow->mutable_base()->set_final_dmg_pct(0.50);

  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}}, {{"assassinate", strike}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 1);
  ASSERT_TRUE(state.character.LearnSkill(strike, 1));

  CombatParams params = ComputeCombatParams(state);
  ASSERT_EQ(params.attacks.size(), 2u);
  // Four poke-fuls from the opening half, and four more from the blow lifted
  // by half again: ten.
  double poke = params.attacks[0].damage_per_hit[0];
  EXPECT_NEAR(params.attacks[1].damage_per_hit[0], poke * 10.0, 1e-6);

  // Stated on the swing instead it reaches both halves, and the same skill is
  // worth twelve. That fold is what this lever exists to undo.
  Skill whole = strike;
  whole.mutable_extra_hit(0)->mutable_base()->clear_final_dmg_pct();
  whole.mutable_base()->set_final_dmg_pct(0.50);
  state.skills["assassinate"] = whole;
  EXPECT_NEAR(ComputeCombatParams(state).attacks[1].damage_per_hit[0],
              poke * 12.0, 1e-6);
}

// Greater Vessel of Light's shape: one passive that hands a strike to two
// attacks and widens one of them. The grant is folded into the target before
// its attack is built, so the damage the fight sees is already carrying it.
TEST(ComputeCombatParamsTest, APassiveHandsStrikesAndReachToSkillsItNames) {
  Skill charge;
  charge.set_name("Divine Charge");
  charge.set_kind(SKILL_KIND_ATTACK);
  PlaceIn(charge, JOB_ADVANCEMENT_SWORDMAN);
  charge.set_max_level(20);
  charge.set_max_enemies(8);
  charge.set_lines(4);
  charge.mutable_base()->set_skill_pct(2.53);
  Skill vessel;
  vessel.set_name("Greater Vessel of Light");
  vessel.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(vessel, JOB_ADVANCEMENT_SWORDMAN);
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

// Heaven's Hammer - Cooldown Cutter's shape: a passive that takes a share off
// the wait of the skill it names, at every level that skill is taught.
TEST(ComputeCombatParamsTest, ABoostCanShortenTheWaitOfTheSkillItNames) {
  Skill hammer;
  hammer.set_name("Heaven's Hammer");
  hammer.set_kind(SKILL_KIND_ATTACK);
  PlaceIn(hammer, JOB_ADVANCEMENT_SWORDMAN);
  hammer.set_max_level(30);
  hammer.set_max_enemies(15);
  hammer.set_lines(8);
  hammer.set_cooldown_seconds(29.5);
  hammer.set_cooldown_seconds_per_level(-0.5);
  hammer.mutable_base()->set_skill_pct(4.46);
  Skill hyper;
  hyper.set_name("Heaven's Hammer - Cooldown Cutter");
  hyper.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(hyper, JOB_ADVANCEMENT_SWORDMAN);
  hyper.set_max_level(1);
  SkillBoost* boost = hyper.add_boost();
  boost->set_skill_name("Heaven's Hammer");
  boost->set_cooldown_pct(0.30);

  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}},
                  {{"hammer", hammer}, {"hyper", hyper}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 32);
  ASSERT_TRUE(state.character.LearnSkill(hammer, 1));

  double factor = GameSpeedFactor(state.character.proto().level());
  CombatParams bare = ComputeCombatParams(state);
  ASSERT_EQ(bare.attacks.size(), 2u);
  EXPECT_DOUBLE_EQ(bare.attacks[1].cooldown_seconds, 29.5 * factor);

  ASSERT_TRUE(state.character.LearnSkill(hyper, 1));
  EXPECT_DOUBLE_EQ(ComputeCombatParams(state).attacks[1].cooldown_seconds,
                   29.5 * 0.7 * factor);

  // The same share further up the ladder, where the skill's own wait is
  // shorter: the cut is taken off the whole of it, not off its first level.
  ASSERT_TRUE(state.character.LearnSkill(hammer, 29));
  EXPECT_DOUBLE_EQ(ComputeCombatParams(state).attacks[1].cooldown_seconds,
                   15.0 * 0.7 * factor);
}

// Creeping Toxin's shape: a summon that upgrades its own pulse rather than
// another skill's swing, which is what a form naming no skill means.
TEST(ComputeCombatParamsTest, ASummonCanUpgradeItsOwnPulse) {
  Skill toxin;
  toxin.set_name("Creeping Toxin");
  toxin.set_kind(SKILL_KIND_AUTO_ATTACK);
  PlaceIn(toxin, JOB_ADVANCEMENT_SWORDMAN);
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
  PlaceIn(piercing, JOB_ADVANCEMENT_SWORDMAN);
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
  PlaceIn(greater, JOB_ADVANCEMENT_SWORDMAN);
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

// The Marksman's Extra Strike hypers: a strike for what the swing lands beside
// itself -- the arrow's fragment, and the mark the empowered shot spends --
// rather than for the swing's own lines.
TEST(ComputeCombatParamsTest, ABoostAddsAStrikeToASwingsSecondHits) {
  Skill piercing;
  piercing.set_name("Piercing Arrow II");
  piercing.set_kind(SKILL_KIND_ATTACK);
  PlaceIn(piercing, JOB_ADVANCEMENT_SWORDMAN);
  piercing.set_max_level(30);
  piercing.set_max_enemies(8);
  piercing.set_lines(5);
  piercing.set_lead_lines(4);
  piercing.set_lead_enemies(2);
  piercing.mutable_base()->set_skill_pct(1.00);
  piercing.mutable_base()->set_lead_pct(2.00);

  Skill greater;
  greater.set_name("Greater Empowered Arrows");
  greater.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(greater, JOB_ADVANCEMENT_SWORDMAN);
  greater.set_max_level(20);
  EmpoweredForm* form = greater.add_empowered_form();
  form->set_skill_name("Piercing Arrow II");
  form->set_casts_per_trigger(4);
  form->set_max_enemies(10);
  form->set_lines(6);
  form->mutable_base()->set_skill_pct(2.00);
  SwingHit* burst = form->add_extra_hit();
  burst->set_label("Fragment");
  burst->set_lines(10);
  burst->mutable_base()->set_skill_pct(2.00);

  Skill extra;
  extra.set_name("Piercing Arrow - Extra Strike");
  extra.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(extra, JOB_ADVANCEMENT_SWORDMAN);
  extra.set_max_level(1);

  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}},
                  {{"piercing_arrow_ii", piercing},
                   {"greater_empowered_arrows", greater},
                   {"piercing_arrow_extra_strike", extra}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 3);
  ASSERT_TRUE(state.character.LearnSkill(piercing, 1));
  ASSERT_TRUE(state.character.LearnSkill(greater, 1));
  ASSERT_TRUE(state.character.LearnSkill(extra, 1));

  CombatParams plain = ComputeCombatParams(state);
  const AttackOption* before = FindAttack(plain, "Piercing Arrow II");
  ASSERT_NE(before, nullptr);
  ASSERT_NE(before->empowered, nullptr);
  double lead = before->lead_damage[0];
  double swing = before->damage_per_hit[0];
  double form_swing = before->empowered->damage_per_hit[0];

  SkillBoost* boost = state.skills["piercing_arrow_extra_strike"].add_boost();
  boost->set_skill_name("Piercing Arrow II");
  boost->set_lines(1);
  boost->set_extra_hit_lines(1);
  boost->set_reach(BOOST_REACH_BOTH);
  CombatParams boosted = ComputeCombatParams(state);
  const AttackOption* after = FindAttack(boosted, "Piercing Arrow II");
  ASSERT_NE(after, nullptr);
  ASSERT_NE(after->empowered, nullptr);
  // Five lines to six, and the fragment's four to five.
  EXPECT_NEAR(after->damage_per_hit[0], swing * 6.0 / 5.0, 1e-9);
  EXPECT_NEAR(after->lead_damage[0], lead * 5.0 / 4.0, 1e-9);
  EXPECT_EQ(after->lead_enemies, 2);
  // The form gains both: six lines to seven, and its explosion's ten to
  // eleven. Its damage is the two summed, so they are checked together.
  EXPECT_NEAR(after->empowered->damage_per_hit[0],
              form_swing * (7.0 * 2.0 + 11.0 * 2.0) / (6.0 * 2.0 + 10.0 * 2.0),
              1e-9);
}

// A boost naming a swing lands on the ordinary one, on both, or on the form
// alone, as its reach says.
TEST(ComputeCombatParamsTest, ABoostLandsWhereItsReachSays) {
  Skill piercing;
  piercing.set_name("Piercing Arrow II");
  piercing.set_kind(SKILL_KIND_ATTACK);
  PlaceIn(piercing, JOB_ADVANCEMENT_SWORDMAN);
  piercing.set_max_level(30);
  piercing.set_max_enemies(8);
  piercing.set_lines(5);
  piercing.mutable_base()->set_skill_pct(1.00);

  Skill greater;
  greater.set_name("Greater Empowered Arrows");
  greater.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(greater, JOB_ADVANCEMENT_SWORDMAN);
  greater.set_max_level(20);
  EmpoweredForm* form = greater.add_empowered_form();
  form->set_skill_name("Piercing Arrow II");
  form->set_casts_per_trigger(4);
  form->set_max_enemies(10);
  form->set_lines(6);
  form->mutable_base()->set_skill_pct(2.00);

  Skill spread;
  spread.set_name("Piercing Arrow - Spread");
  spread.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(spread, JOB_ADVANCEMENT_SWORDMAN);
  spread.set_max_level(1);
  SkillBoost* boost = spread.add_boost();
  boost->set_skill_name("Piercing Arrow II");
  boost->set_max_enemies(2);
  boost->mutable_effect()->set_damage_pct(0.20);

  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}},
                  {{"piercing_arrow_ii", piercing},
                   {"greater_empowered_arrows", greater},
                   {"piercing_arrow_spread", spread}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 3);
  ASSERT_TRUE(state.character.LearnSkill(piercing, 1));
  ASSERT_TRUE(state.character.LearnSkill(greater, 1));
  ASSERT_TRUE(state.character.LearnSkill(spread, 1));

  // Stopping at the ordinary swing: the form keeps its own reach and its own
  // damage, which is the bargain every empowered form struck before hypers.
  CombatParams stops = ComputeCombatParams(state);
  const AttackOption* arrow = FindAttack(stops, "Piercing Arrow II");
  ASSERT_NE(arrow, nullptr);
  ASSERT_NE(arrow->empowered, nullptr);
  EXPECT_EQ(arrow->max_enemies, 10);
  EXPECT_EQ(arrow->empowered->max_enemies, 10);
  double plain_form = arrow->empowered->damage_per_hit[0];

  state.skills["piercing_arrow_spread"].mutable_boost(0)->set_reach(
      BOOST_REACH_BOTH);
  CombatParams follows = ComputeCombatParams(state);
  arrow = FindAttack(follows, "Piercing Arrow II");
  ASSERT_NE(arrow, nullptr);
  ASSERT_NE(arrow->empowered, nullptr);
  EXPECT_EQ(arrow->max_enemies, 10);
  EXPECT_EQ(arrow->empowered->max_enemies, 12);
  // The 20% the boost pays only the skill it names, now collected twice.
  EXPECT_NEAR(arrow->empowered->damage_per_hit[0], plain_form * 1.2, 1e-9);

  // The form alone: the ordinary swing is left exactly where it started, which
  // is what the Paladin's Blast node needs of the Divine Brand's own tier.
  double plain_swing = arrow->damage_per_hit[0] / 1.2;
  state.skills["piercing_arrow_spread"].mutable_boost(0)->set_reach(
      BOOST_REACH_EMPOWERED);
  CombatParams form_only = ComputeCombatParams(state);
  arrow = FindAttack(form_only, "Piercing Arrow II");
  ASSERT_NE(arrow, nullptr);
  ASSERT_NE(arrow->empowered, nullptr);
  EXPECT_EQ(arrow->max_enemies, 8);
  EXPECT_EQ(arrow->empowered->max_enemies, 12);
  EXPECT_NEAR(arrow->damage_per_hit[0], plain_swing, 1e-9);
  EXPECT_NEAR(arrow->empowered->damage_per_hit[0], plain_form * 1.2, 1e-9);
}

// A swing that always crits states so on itself, and says nothing about the
// swing after it -- the same shape ignored defence written on an attack has.
TEST(ComputeCombatParamsTest, ACertainCritRidesOnlyItsOwnSwing) {
  Skill snipe;
  snipe.set_name("Snipe");
  snipe.set_kind(SKILL_KIND_ATTACK);
  PlaceIn(snipe, JOB_ADVANCEMENT_SWORDMAN);
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
  PlaceIn(plain, JOB_ADVANCEMENT_SWORDMAN);
  plain.set_max_level(20);
  plain.set_max_enemies(1);
  plain.mutable_base()->set_skill_pct(1.00);
  Skill illusion;
  illusion.set_name("Arrow Illusion");
  illusion.set_kind(SKILL_KIND_AUTO_ATTACK);
  PlaceIn(illusion, JOB_ADVANCEMENT_SWORDMAN);
  illusion.set_max_level(20);
  illusion.set_max_enemies(6);
  illusion.set_cast_interval_seconds(3.0);
  illusion.mutable_base()->set_skill_pct(1.00);
  illusion.mutable_passive()->set_ied_pct(0.30);

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
  // GMS's "[Passive Effects]" means. Written in `base` it would have stayed
  // with the decoy, the way Radiant Evil's does.
  EXPECT_GT(after.attacks[1].damage_per_hit[0], swing_before);
}

// Bolt Surplus's strike lands on the swings the character chooses between and
// nowhere else: not on the bare poke, which lands once, and not on a summon,
// which they never swung.
TEST(ComputeCombatParamsTest, AnExtraStrikeReachesOnlyMultiHitSwings) {
  Skill snipe;
  snipe.set_name("Snipe");
  snipe.set_kind(SKILL_KIND_ATTACK);
  PlaceIn(snipe, JOB_ADVANCEMENT_SWORDMAN);
  snipe.set_max_level(30);
  snipe.set_max_enemies(1);
  snipe.set_lines(9);
  snipe.mutable_base()->set_skill_pct(1.00);
  Skill illusion;
  illusion.set_name("Arrow Illusion");
  illusion.set_kind(SKILL_KIND_AUTO_ATTACK);
  PlaceIn(illusion, JOB_ADVANCEMENT_SWORDMAN);
  illusion.set_max_level(20);
  illusion.set_max_enemies(6);
  illusion.set_lines(4);
  illusion.set_cast_interval_seconds(3.0);
  illusion.mutable_base()->set_skill_pct(1.00);
  Skill surplus;
  surplus.set_name("Bolt Surplus");
  surplus.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(surplus, JOB_ADVANCEMENT_SWORDMAN);
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
  PlaceIn(snipe, JOB_ADVANCEMENT_SWORDMAN);
  snipe.set_max_level(30);
  snipe.set_max_enemies(1);
  snipe.set_lines(9);
  snipe.mutable_base()->set_skill_pct(1.00);

  Skill greater;
  greater.set_name("Greater Empowered Arrows");
  greater.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(greater, JOB_ADVANCEMENT_SWORDMAN);
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
  PlaceIn(mirage, JOB_ADVANCEMENT_SWORDMAN);
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

// Inhuman Speed's shape: an own-clock half counted in the character's swings
// rather than in seconds, silenced by the buff its own skill raises. It joins
// the whole skills so clocked on the triggered list, and knows which buff
// silences it.
TEST(ComputeCombatParamsTest, ASwingClockedHalfIsSilencedByItsOwnBuff) {
  Skill inhuman;
  inhuman.set_name("Inhuman Speed");
  inhuman.set_kind(SKILL_KIND_ACTIVE);
  PlaceIn(inhuman, JOB_ADVANCEMENT_SWORDMAN);
  inhuman.set_max_level(30);
  inhuman.set_cooldown_seconds(120.0);
  inhuman.mutable_buff()->set_duration_seconds(30.0);
  AutoMode* afterimage = inhuman.add_auto_mode();
  afterimage->set_label("Afterimage");
  afterimage->set_attacks_per_cast(10);
  afterimage->set_casts(5);
  afterimage->set_max_enemies(1);
  afterimage->set_lines(3);
  afterimage->set_silent_while_buff_stands(true);
  afterimage->mutable_base()->set_skill_pct(4.16);

  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}}, {{"inhuman", inhuman}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 1);
  ASSERT_TRUE(state.character.LearnSkill(inhuman, 1));

  CombatParams params = ComputeCombatParams(state);
  EXPECT_TRUE(params.auto_attacks.empty());
  ASSERT_EQ(params.triggered_attacks.size(), 1u);
  const AttackOption& half = params.triggered_attacks[0];
  EXPECT_EQ(half.name, "Inhuman Speed");
  EXPECT_EQ(half.attacks_per_cast, 10);
  EXPECT_EQ(half.strikes_per_pulse, 5);  // five shots in the second it stands
  EXPECT_EQ(half.max_enemies, 1);
  EXPECT_GT(half.damage_per_hit[0], 0.0);
  // Counted in swings, so the pacing band never touches it.
  EXPECT_DOUBLE_EQ(half.interval_seconds, 0.0);
  // And pointed at the buff that silences it, in the base table and in the
  // buffed window alike.
  ASSERT_EQ(params.buffs.size(), 1u);
  EXPECT_TRUE(half.silent_while_buff);
  EXPECT_EQ(half.needs_buff, 0);
  ASSERT_EQ(params.TriggeredAttacks(1).size(), 1u);
  EXPECT_TRUE(params.TriggeredAttacks(1)[0].silent_while_buff);
  EXPECT_EQ(params.TriggeredAttacks(1)[0].needs_buff, 0);
}

// A skill clocked by enemies defeated joins the swing-clocked ones on the
// triggered list: both are counted rather than timed, and the fight tells them
// apart by which count they name.
TEST(ComputeCombatParamsTest, AKillClockedSkillLandsOnTheTriggeredList) {
  // A common node, which is also the only shape that has one: it belongs to no
  // book, so nothing here may gate the swing on an advancement.
  Skill fountain;
  fountain.set_name("Erda Fountain");
  fountain.set_kind(SKILL_KIND_AUTO_ATTACK);
  PlaceIn(fountain, JOB_ADVANCEMENT_COMMON);
  fountain.set_v_node(V_NODE_KIND_COMMON);
  fountain.set_max_level(MaxVNodeLevel(V_NODE_KIND_COMMON));
  fountain.set_max_enemies(10);
  fountain.set_lines(4);
  fountain.set_kills_per_cast(12);
  fountain.mutable_base()->set_skill_pct(4.65);
  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}}, {{"erda_fountain", fountain}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 1);
  while (state.character.proto().job_stage() < kFifthJobStage) {
    state.character.AdvanceJob(state.character.proto().job());
  }
  state.character.AddVPoints(VNodeCost(V_NODE_KIND_COMMON, 0, 1));
  ASSERT_TRUE(state.character.LearnSkill(fountain, 1));

  CombatParams params = ComputeCombatParams(state);
  EXPECT_EQ(params.attacks.size(), 1u);  // the bare poke, and nothing else
  EXPECT_TRUE(params.auto_attacks.empty());
  ASSERT_EQ(params.triggered_attacks.size(), 1u);
  const AttackOption& cast = params.triggered_attacks[0];
  EXPECT_EQ(cast.name, "Erda Fountain");
  EXPECT_EQ(cast.kills_per_cast, 12);
  EXPECT_EQ(cast.attacks_per_cast, 0);  // it names one clock, not two
  EXPECT_EQ(cast.max_enemies, 10);
  EXPECT_GT(cast.damage_per_hit[0], 0.0);
  // Counted in defeats, so neither the pacing band nor the swing touches it.
  EXPECT_DOUBLE_EQ(cast.interval_seconds, 0.0);
  EXPECT_DOUBLE_EQ(cast.swing_seconds, 0.0);
}

// Speed Mirage II's shape: a passive that resets the clock of the skill it
// names, rather than adding to it. Strikes and reach sum; the clock replaces.
TEST(ComputeCombatParamsTest, ABoostCanResetTheClockItNames) {
  Skill mirage;
  mirage.set_name("Speed Mirage");
  mirage.set_kind(SKILL_KIND_AUTO_ATTACK);
  PlaceIn(mirage, JOB_ADVANCEMENT_SWORDMAN);
  mirage.set_max_level(20);
  mirage.set_max_enemies(6);
  mirage.set_lines(4);
  mirage.set_attacks_per_cast(4);
  mirage.mutable_base()->set_skill_pct(3.25);
  Skill second;
  second.set_name("Speed Mirage II");
  second.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(second, JOB_ADVANCEMENT_SWORDMAN);
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
  PlaceIn(blaster, JOB_ADVANCEMENT_SWORDMAN);
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
  PlaceIn(evil_eye, JOB_ADVANCEMENT_SWORDMAN);
  evil_eye.set_max_level(10);
  evil_eye.set_cast_interval_seconds(12.0);
  evil_eye.mutable_base()->set_skill_pct(1.23);
  Skill final_attack;
  final_attack.set_name("Final Attack");
  final_attack.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(final_attack, JOB_ADVANCEMENT_SWORDMAN);
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

// The poison a passive keeps on the claw is lit by the character swinging it.
// A summon firing on its own clock carries no claw, so its pulses leave no
// mark -- the same rule that strips the shadow and the mesos off it.
TEST(ComputeCombatParamsTest, OnlyOwnSwingsCarryTheClawsPoison) {
  Skill evil_eye;
  evil_eye.set_name("Evil Eye Shock");
  evil_eye.set_kind(SKILL_KIND_AUTO_ATTACK);
  PlaceIn(evil_eye, JOB_ADVANCEMENT_SWORDMAN);
  evil_eye.set_max_level(10);
  evil_eye.set_cast_interval_seconds(12.0);
  evil_eye.mutable_base()->set_skill_pct(1.23);
  Skill venom;
  venom.set_name("Venom");
  venom.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(venom, JOB_ADVANCEMENT_SWORDMAN);
  venom.set_max_level(10);
  Dot* poison = venom.mutable_dot();
  poison->set_interval_seconds(1.0);
  poison->set_duration_seconds(8.0);
  poison->mutable_base()->set_skill_pct(1.15);
  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}},
                  {{"evil_eye_shock", evil_eye}, {"venom", venom}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 2);
  ASSERT_TRUE(state.character.LearnSkill(evil_eye, 1));
  ASSERT_TRUE(state.character.LearnSkill(venom, 1));

  CombatParams params = ComputeCombatParams(state);
  ASSERT_EQ(params.attacks.size(), 1u);
  ASSERT_EQ(params.auto_attacks.size(), 1u);
  ASSERT_EQ(params.attacks[0].dots.size(), 1u);
  EXPECT_TRUE(params.attacks[0].dots[0].carried);
  EXPECT_TRUE(params.auto_attacks[0].dots.empty());
}

// A Final Attack that names a tag follows only the swings carrying it. The
// bare poke carries none, so it never sets one off.
TEST(ComputeCombatParamsTest, ATaggedFinalAttackFollowsOnlyThatTagsSwings) {
  Skill flame;
  flame.set_name("Flame Orb");
  flame.set_kind(SKILL_KIND_ATTACK);
  PlaceIn(flame, JOB_ADVANCEMENT_SWORDMAN);
  flame.set_max_level(10);
  flame.add_tags(SKILL_TAG_FIRE);
  flame.mutable_base()->set_skill_pct(1.48);
  Skill cold;
  cold.set_name("Cold Beam");
  cold.set_kind(SKILL_KIND_ATTACK);
  PlaceIn(cold, JOB_ADVANCEMENT_SWORDMAN);
  cold.set_max_level(10);
  cold.mutable_base()->set_skill_pct(1.48);
  Skill ignite;
  ignite.set_name("Ignite");
  ignite.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(ignite, JOB_ADVANCEMENT_SWORDMAN);
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
  PlaceIn(evil_eye, JOB_ADVANCEMENT_SWORDMAN);
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
  PlaceIn(lucky_seven, JOB_ADVANCEMENT_ROGUE);
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
  PlaceIn(lucky_seven, JOB_ADVANCEMENT_ROGUE);
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
  PlaceIn(nimble, JOB_ADVANCEMENT_ROGUE);
  nimble.set_max_level(20);
  nimble.mutable_base()->set_luk(1);
  nimble.mutable_per_level()->set_luk(1);
  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}}, {{"nimble_body", nimble}});
  state.current_map = "field";
  EquipSword(state);
  // LUK has to be a stat this job actually swings on, so the character becomes
  // a rogue rather than whatever job a new character starts on.
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

// The barrier reaches the fight down the same wire armour does.
TEST(ComputeCombatParamsTest, ABarrierWeakensWhatMobsSwingWith) {
  Skill curse;
  curse.set_name("Frailty Curse");
  curse.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(curse, JOB_ADVANCEMENT_SWORDMAN);
  curse.set_max_level(20);
  curse.mutable_base()->set_enemy_attack_pct(0.30);

  GameState bare({}, {}, {}, {{"snail", MakeAttacker("Snail", 15, 200, 1)}},
                 {{"field", TwoSnailMap()}}, {{"frailty_curse", curse}});
  bare.current_map = "field";
  EquipSword(bare);

  GameState cursed({}, {}, {}, {{"snail", MakeAttacker("Snail", 15, 200, 1)}},
                   {{"field", TwoSnailMap()}}, {{"frailty_curse", curse}});
  cursed.current_map = "field";
  EquipSword(cursed);
  GrantFirstJobSp(cursed, 1);
  ASSERT_TRUE(cursed.character.LearnSkill(curse, 1));

  EXPECT_LT(ComputeCombatParams(cursed).types[0].damage_to_player,
            ComputeCombatParams(bare).types[0].damage_to_player);
}

// A map inside Arcane River, which asks for Arcane Force before it will let
// the character fight properly.
MapData ArcaneMap(int required) {
  MapData map = TwoSnailMap();
  map.set_arcane_force(required);
  return map;
}

// A worn symbol at `level`, which is 10 Arcane Force a level plus 20.
void EquipSymbol(GameState& state, int level) {
  EquipPrototype symbol;
  symbol.set_name("Arcane Symbol: Vanishing Journey");
  symbol.set_equip_slot(EQUIP_SLOT_SYMBOL_VANISHING_JOURNEY);
  symbol.mutable_arcane_symbol()->set_meso_cost_base(8);
  Equip state_proto;
  state_proto.set_symbol_level(level);
  state.character.PickUp(std::make_unique<EquipInstance>(symbol, state_proto));
  state.character.Equip(0);
}

// Both sides of the fight move together: a character short of the requirement
// deals a fraction and takes a multiple, and one over it deals more and takes
// almost nothing.
TEST(ComputeCombatParamsTest, ArcaneForceScalesBothSidesOfTheFight) {
  GameState met({}, {}, {}, {{"snail", MakeAttacker("Snail", 15, 200, 1)}},
                {{"field", ArcaneMap(30)}});
  met.current_map = "field";
  EquipSword(met);
  EquipSymbol(met, /*level=*/1);  // 30 force, exactly what the map asks

  GameState short_of({}, {}, {}, {{"snail", MakeAttacker("Snail", 15, 200, 1)}},
                     {{"field", ArcaneMap(300)}});
  short_of.current_map = "field";
  EquipSword(short_of);
  EquipSymbol(short_of, /*level=*/1);  // 30 of 300, a tenth of the way

  CombatParams full = ComputeCombatParams(met);
  CombatParams starved = ComputeCombatParams(short_of);
  ASSERT_FALSE(full.attacks.empty());
  ASSERT_FALSE(starved.attacks.empty());
  // 10% met deals 30% and takes 2.4x; meeting it deals and takes the whole.
  EXPECT_NEAR(starved.attacks[0].damage_per_hit[0],
              full.attacks[0].damage_per_hit[0] * 0.30, 1e-6);
  EXPECT_NEAR(starved.types[0].damage_to_player,
              full.types[0].damage_to_player * 2.4, 1e-6);
}

// Half again over the requirement is GMS's ceiling: 150% dealt, and a monster
// reduced to the 1 damage the floor insists on.
TEST(ComputeCombatParamsTest, ArcaneForceOverTheRequirementCapsOut) {
  GameState state({}, {}, {}, {{"snail", MakeAttacker("Snail", 15, 200, 1)}},
                  {{"field", ArcaneMap(30)}});
  state.current_map = "field";
  EquipSword(state);
  EquipSymbol(state, /*level=*/3);  // 50 force against 30 asked

  CombatParams params = ComputeCombatParams(state);
  ASSERT_FALSE(params.attacks.empty());
  EXPECT_DOUBLE_EQ(params.types[0].damage_to_player, 1.0)
      << "the floor GMS leaves a monster that can no longer hurt them";
}

// A map outside Arcane River asks for nothing, which has to read as no
// requirement rather than as a requirement met by nobody -- the difference
// between fighting normally and dealing a tenth of your damage everywhere.
TEST(ComputeCombatParamsTest, AMapAskingForNoForceIsUntouched) {
  GameState plain({}, {}, {}, {{"snail", MakeAttacker("Snail", 15, 200, 1)}},
                  {{"field", TwoSnailMap()}});
  plain.current_map = "field";
  EquipSword(plain);
  EquipSymbol(plain, /*level=*/1);

  GameState arcane({}, {}, {}, {{"snail", MakeAttacker("Snail", 15, 200, 1)}},
                   {{"field", ArcaneMap(30)}});
  arcane.current_map = "field";
  EquipSword(arcane);
  EquipSymbol(arcane, /*level=*/1);

  CombatParams a = ComputeCombatParams(plain);
  CombatParams b = ComputeCombatParams(arcane);
  EXPECT_DOUBLE_EQ(a.types[0].damage_to_player, b.types[0].damage_to_player);
  EXPECT_DOUBLE_EQ(a.attacks[0].damage_per_hit[0],
                   b.attacks[0].damage_per_hit[0]);
}

// Two phases, so a test can watch one turn over into the next.
BossDifficulty NormalTwoPhase() {
  BossDifficulty difficulty;
  difficulty.set_name("Normal");
  difficulty.set_time_limit_seconds(300);
  Spawn* arms = difficulty.add_phases()->add_spawns();
  arms->set_mob("arm");
  arms->set_count(8);
  Spawn* body = difficulty.add_phases()->add_spawns();
  body->set_mob("body");
  body->set_count(1);
  return difficulty;
}

// A boss fight runs in real time however high the character has climbed, and
// against neither clock the farming loop is paced by.
TEST(ComputeBossParamsTest, RunsRealTimeWithNoRespawnAndNoIncomingHits) {
  GameState state({}, {}, {},
                  {{"arm", MakeAttacker("Zakum's Arm", 700000, 2200, 110)},
                   {"body", MakeAttacker("Zakum", 7000000, 9300, 110)}},
                  {});
  EquipSword(state);
  BossDifficulty normal = NormalTwoPhase();

  CombatParams params = ComputeBossParams(state, "zakum", normal, 0);
  ASSERT_TRUE(params.active);
  EXPECT_DOUBLE_EQ(params.respawn_seconds, 0.0);
  EXPECT_DOUBLE_EQ(params.hit_seconds, 0.0);
  ASSERT_EQ(params.types.size(), 1u);
  EXPECT_EQ(params.types[0].mob->name(), "Zakum's Arm");
  EXPECT_EQ(params.types[0].simultaneous, 8);
  // The same swing on the same weapon, but unstretched: the map's is the
  // boss's times the pacing band the character has climbed into.
  MapData field;
  Spawn* spawn = field.add_spawns();
  spawn->set_mob("arm");
  spawn->set_count(1);
  state.maps["field"] = field;
  state.current_map = "field";
  CombatParams mapped = ComputeCombatParams(state);
  double speed = GameSpeedFactor(state.character.proto().level());
  EXPECT_DOUBLE_EQ(mapped.attacks.front().swing_seconds,
                   params.attacks.front().swing_seconds * speed);
  EXPECT_GT(params.attacks.front().damage_per_hit[0], 0.0);
}

// Which Hyper Stat allocation is in play is decided by the activity: a boss
// fight reads the bossing one, a map the farming one.
TEST(ComputeBossParamsTest, TheFightPicksTheAllocationForTheActivity) {
  GameState state({}, {}, {},
                  {{"arm", MakeMob("Zakum's Arm", 700000)},
                   {"body", MakeMob("Zakum", 7000000)},
                   {"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}});
  state.current_map = "field";
  Character grown = state.character.ToProto();
  grown.set_level(200);
  state.character.RestoreFrom(grown, state.equips, state.items);
  EquipSword(state);
  ASSERT_TRUE(state.character.AllocateHyperStat(HYPER_STAT_FIELD_ATTACK,
                                                StatPreset::kBossing, 10));

  BossDifficulty normal = NormalTwoPhase();
  CombatParams boss = ComputeBossParams(state, "zakum", normal, 0);
  CombatParams mapped = ComputeCombatParams(state);
  ASSERT_TRUE(boss.active);
  ASSERT_TRUE(mapped.active);
  EXPECT_GT(boss.attacks.front().damage_per_hit[0],
            mapped.attacks.front().damage_per_hit[0])
      << "30 attack the farming allocation never bought";
}

// The Extreme Green Potion is worth a stage in a boss fight and nothing on a
// map -- and the stage it is worth passes the cap the map holds them to.
TEST(ComputeBossParamsTest, TheGreenPotionPassesTheSpeedCapInAFightAlone) {
  GameState state({}, {}, {},
                  {{"arm", MakeMob("Zakum's Arm", 700000)},
                   {"body", MakeMob("Zakum", 7000000)},
                   {"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}});
  state.current_map = "field";
  Character grown = state.character.ToProto();
  grown.set_level(200);
  state.character.RestoreFrom(grown, state.equips, state.items);
  // Already sitting on the cap, which is where the potion is worth anything.
  EquipSwordAt(state, ATTACK_SPEED_FASTEST_3);

  BossDifficulty normal = NormalTwoPhase();
  double capped = ComputeBossParams(state, "zakum", normal, 0)
                      .attacks.front()
                      .swing_seconds;
  double mapped = ComputeCombatParams(state).attacks.front().swing_seconds;

  ASSERT_TRUE(
      state.character.ToggleConsumable(CONSUMABLE_TYPE_EXTREME_GREEN_POTION));
  EXPECT_LT(ComputeBossParams(state, "zakum", normal, 0)
                .attacks.front()
                .swing_seconds,
            capped);
  EXPECT_DOUBLE_EQ(ComputeCombatParams(state).attacks.front().swing_seconds,
                   mapped)
      << "the map is not a boss fight";
}

// A boss stands its parts a room apart, so a swing reaches half as many of
// them. Rounded up, so nothing loses the part it was aimed at, and only in a
// boss fight -- the same skill sweeps a whole map's worth on the field.
TEST(ComputeBossParamsTest, ASwingReachesHalfAsManyPartsOfABoss) {
  Skill slash;
  slash.set_name("Slash Blast");
  slash.set_kind(SKILL_KIND_ATTACK);
  PlaceIn(slash, JOB_ADVANCEMENT_SWORDMAN);
  slash.set_max_level(20);
  slash.set_max_enemies(5);
  slash.mutable_base()->set_skill_pct(1.83);
  GameState state({}, {}, {},
                  {{"arm", MakeMob("Zakum's Arm", 700000)},
                   {"body", MakeMob("Zakum", 7000000)},
                   {"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}}, {{"slash_blast", slash}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 1);
  ASSERT_TRUE(state.character.LearnSkill(slash, 1));

  CombatParams mapped = ComputeCombatParams(state);
  ASSERT_EQ(mapped.attacks.size(), 2u);
  EXPECT_EQ(mapped.attacks[1].max_enemies, 5);

  BossDifficulty normal = NormalTwoPhase();
  CombatParams boss = ComputeBossParams(state, "zakum", normal, 0);
  ASSERT_EQ(boss.attacks.size(), 2u);
  EXPECT_EQ(boss.attacks[1].max_enemies, 3) << "five rounds up to three";
  EXPECT_EQ(boss.attacks[0].max_enemies, 1) << "the poke still reaches one";
}

TEST(ComputeBossParamsTest, EveryPhaseIsItsOwnEncounter) {
  GameState state({}, {}, {},
                  {{"arm", MakeMob("Zakum's Arm", 700000)},
                   {"body", MakeMob("Zakum", 7000000)}},
                  {});
  EquipSword(state);
  BossDifficulty normal = NormalTwoPhase();

  CombatParams first = ComputeBossParams(state, "zakum", normal, 0);
  CombatParams second = ComputeBossParams(state, "zakum", normal, 1);
  EXPECT_NE(first.encounter, second.encounter);
  EXPECT_EQ(first.encounter, BossEncounterKey("zakum", "Normal", 0));
  ASSERT_EQ(second.types.size(), 1u);
  EXPECT_EQ(second.types[0].mob->name(), "Zakum");
  EXPECT_EQ(second.types[0].simultaneous, 1);
}

TEST(ComputeBossParamsTest, InactivePastTheLastPhaseAndWithoutAWeapon) {
  GameState state({}, {}, {}, {{"arm", MakeMob("Zakum's Arm", 700000)}}, {});
  BossDifficulty normal = NormalTwoPhase();
  EXPECT_FALSE(ComputeBossParams(state, "zakum", normal, 0).active);
  EquipSword(state);
  EXPECT_TRUE(ComputeBossParams(state, "zakum", normal, 0).active);
  EXPECT_FALSE(ComputeBossParams(state, "zakum", normal, 2).active);
  EXPECT_FALSE(ComputeBossParams(state, "zakum", normal, -1).active);
  // Phase 1 names a mob this catalog does not hold, so there is nothing there
  // to fight.
  EXPECT_FALSE(ComputeBossParams(state, "zakum", normal, 1).active);
}

// Throwing Star Barrage's shape: a buff that hands another skill hits of its
// own while it stands -- three more sets of stars, each finding a crowd the
// swing itself never reaches, and each left alone by the shadow.
TEST(ComputeCombatParamsTest, ABuffCanWidenAnotherSkillWithHitsOfItsOwn) {
  Skill quad;
  quad.set_name("Quad Star");
  quad.set_kind(SKILL_KIND_ATTACK);
  PlaceIn(quad, JOB_ADVANCEMENT_SWORDMAN);
  quad.set_max_level(30);
  quad.set_base_delay_ms(660);
  quad.set_max_enemies(1);
  quad.set_lines(4);
  quad.mutable_base()->set_skill_pct(1.00);

  Skill partner;
  partner.set_name("Shadow Partner");
  partner.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(partner, JOB_ADVANCEMENT_SWORDMAN);
  partner.set_max_level(20);
  partner.mutable_base()->set_mirror_line_pct(0.70);

  double kept[2] = {0.0, 0.0};
  for (int pass = 0; pass < 2; ++pass) {
    Skill barrage;
    barrage.set_name("Throwing Star Barrage");
    barrage.set_kind(SKILL_KIND_ACTIVE);
    PlaceIn(barrage, JOB_ADVANCEMENT_SWORDMAN);
    barrage.set_max_level(30);
    barrage.set_base_delay_ms(120);
    barrage.mutable_buff()->set_duration_seconds(30.0);
    SkillBoost* spread = barrage.mutable_buff()->add_boost();
    spread->set_skill_name("Quad Star");
    SwingHit* stars = spread->add_extra_hit();
    stars->set_label("Spread");
    stars->set_casts(3);
    stars->set_lines(4);
    stars->set_max_enemies(4);
    stars->set_skips_mirror(pass == 0);
    stars->mutable_base()->set_skill_pct(3.79);

    GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                    {{"field", TwoSnailMap()}},
                    {{"quad_star", quad},
                     {"shadow_partner", partner},
                     {"barrage", barrage}});
    state.current_map = "field";
    EquipSword(state);
    GrantFirstJobSp(state, 3);
    ASSERT_TRUE(state.character.LearnSkill(quad, 1));
    ASSERT_TRUE(state.character.LearnSkill(partner, 1));
    ASSERT_TRUE(state.character.LearnSkill(barrage, 1));

    CombatParams params = ComputeCombatParams(state);
    ASSERT_EQ(params.buffs.size(), 1u);
    ASSERT_EQ(params.attacks.size(), 2u);
    // Nothing is widened until the barrage stands.
    EXPECT_TRUE(params.attacks[1].wide_hit_damage.empty());
    EXPECT_EQ(params.attacks[1].wide_hit_enemies, 0);

    const std::vector<AttackOption>& under = params.Attacks(1);
    ASSERT_EQ(under.size(), params.attacks.size());
    const AttackOption& widened = under[1];
    ASSERT_FALSE(widened.wide_hit_damage.empty());
    // Three strikes of four stars, banked apart from the swing: they land on
    // four enemies where the swing itself finds one.
    EXPECT_EQ(widened.wide_hit_enemies, 4);
    EXPECT_EQ(widened.wide_hit_groups.size(), 3u);
    // The swing is untouched -- four lines and their shadow, as before.
    EXPECT_NEAR(widened.damage_per_hit[0], params.attacks[1].damage_per_hit[0],
                1e-9);
    kept[pass] = widened.wide_hit_damage[0] / widened.damage_per_hit[0];
  }
  // Twelve stars at 379% against the swing's four lines at 100%, each of those
  // copied by the shadow at 70%.
  EXPECT_NEAR(kept[0], 12 * 3.79 / (4 * 1.70), 1e-9);
  // The same hits with the shadow left on, which is what every other one does.
  EXPECT_NEAR(kept[1] / kept[0], 1.70, 1e-9);
}

// The ladder on a handed-over hit is the GRANTING skill's: the swing it joins
// is read at its own level, and a ladder left on the hit would climb twice.
TEST(ComputeCombatParamsTest, AHandedHitClimbsTheGrantingSkillsLadder) {
  Skill quad;
  quad.set_name("Quad Star");
  quad.set_kind(SKILL_KIND_ATTACK);
  PlaceIn(quad, JOB_ADVANCEMENT_SWORDMAN);
  quad.set_max_level(30);
  quad.set_base_delay_ms(660);
  quad.set_max_enemies(1);
  quad.mutable_base()->set_skill_pct(1.00);

  Skill barrage;
  barrage.set_name("Throwing Star Barrage");
  barrage.set_kind(SKILL_KIND_ACTIVE);
  PlaceIn(barrage, JOB_ADVANCEMENT_SWORDMAN);
  barrage.set_max_level(30);
  barrage.set_base_delay_ms(120);
  barrage.mutable_buff()->set_duration_seconds(30.0);
  SkillBoost* spread = barrage.mutable_buff()->add_boost();
  spread->set_skill_name("Quad Star");
  SwingHit* stars = spread->add_extra_hit();
  stars->set_max_enemies(4);
  stars->mutable_base()->set_skill_pct(1.00);
  stars->mutable_per_level()->set_skill_pct(1.00);

  GameState state({}, {}, {}, {{"snail", MakeMob("Snail", 15)}},
                  {{"field", TwoSnailMap()}},
                  {{"quad_star", quad}, {"barrage", barrage}});
  state.current_map = "field";
  EquipSword(state);
  GrantFirstJobSp(state, 5);
  ASSERT_TRUE(state.character.LearnSkill(quad, 1));
  ASSERT_TRUE(state.character.LearnSkill(barrage, 4));

  CombatParams params = ComputeCombatParams(state);
  const AttackOption& widened = params.Attacks(1)[1];
  ASSERT_FALSE(widened.wide_hit_damage.empty());
  // 400% at the barrage's fourth level, against a swing still worth 100%.
  EXPECT_NEAR(widened.wide_hit_damage[0] / widened.damage_per_hit[0], 4.0,
              1e-9);
}

}  // namespace
}  // namespace ms
