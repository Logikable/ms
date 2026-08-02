#include "src/character/character_stats.h"

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <random>
#include <string>
#include <utility>

#include "src/item/equip_instance.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {
namespace {

// A level-`level` character with `hp` AP-allocated HP and enough 1st-job SP to
// max anything the tests learn.
CharacterInstance MakeCharacter(std::mt19937& rng, int level, int hp,
                                int mp = 0) {
  Character proto;
  proto.set_level(level);
  proto.set_job_stage(1);
  proto.mutable_allocated_stats()->set_hp(hp);
  proto.mutable_allocated_stats()->set_mp(mp);
  (*proto.mutable_sp_by_stage())[1] = 100;
  return CharacterInstance(rng, std::move(proto));
}

// Equips a hat-shaped item (no slot conflicts here -- the weapon slot is the
// only one implemented) carrying `max_hp` and `def`.
void EquipArmor(CharacterInstance& character, int max_hp, int def,
                int max_mp = 0) {
  EquipPrototype armor;
  armor.set_name("Armor");
  armor.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  armor.mutable_base_stats()->set_max_hp(max_hp);
  armor.mutable_base_stats()->set_max_mp(max_mp);
  armor.mutable_base_stats()->set_def(def);
  character.PickUp(std::make_unique<EquipInstance>(armor));
  character.Equip(0);
}

// Iron Body as the wiki states it: DEF +10*L, Max HP +L%, damage taken -L/2%.
Skill IronBody() {
  Skill skill;
  skill.set_name("Iron Body");
  skill.set_kind(SKILL_KIND_PASSIVE);
  skill.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  skill.set_max_level(20);
  skill.mutable_base()->set_def(10);
  skill.mutable_base()->set_max_hp_pct(0.01);
  skill.mutable_base()->set_damage_taken_pct(0.005);
  skill.mutable_per_level()->set_def(10);
  skill.mutable_per_level()->set_max_hp_pct(0.01);
  skill.mutable_per_level()->set_damage_taken_pct(0.005);
  return skill;
}

// Critical Shot: +2% crit rate a level.
Skill CriticalShot() {
  Skill skill;
  skill.set_name("Critical Shot");
  skill.set_kind(SKILL_KIND_PASSIVE);
  skill.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  skill.set_max_level(20);
  skill.mutable_base()->set_crit_rate(0.02);
  skill.mutable_per_level()->set_crit_rate(0.02);
  return skill;
}

// MP Boost as the wiki states it: Max MP +x%, MP +(20+5x) per character level.
Skill MpBoost() {
  Skill skill;
  skill.set_name("MP Boost");
  skill.set_kind(SKILL_KIND_PASSIVE);
  skill.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  skill.set_max_level(20);
  skill.mutable_base()->set_max_mp_pct(0.01);
  skill.mutable_base()->set_max_mp_per_level(25);
  skill.mutable_per_level()->set_max_mp_pct(0.01);
  skill.mutable_per_level()->set_max_mp_per_level(5);
  return skill;
}

// Nimble Body: +1 LUK a level, the rogue's whole passive.
Skill NimbleBody() {
  Skill skill;
  skill.set_name("Nimble Body");
  skill.set_kind(SKILL_KIND_PASSIVE);
  skill.set_job_advancement(JOB_ADVANCEMENT_ROGUE);
  skill.set_max_level(20);
  skill.mutable_base()->set_luk(1);
  skill.mutable_per_level()->set_luk(1);
  return skill;
}

// Archery Mastery: +1 attack speed stage, flat at every level.
Skill ArcheryMastery() {
  Skill skill;
  skill.set_name("Archery Mastery");
  skill.set_kind(SKILL_KIND_PASSIVE);
  skill.set_job_advancement(JOB_ADVANCEMENT_ARCHER);
  skill.set_max_level(15);
  skill.mutable_base()->set_attack_speed(1);
  return skill;
}

// Warrior Mastery, trimmed to the one lever we model: +(5 + L) HP per level.
Skill WarriorMastery() {
  Skill skill;
  skill.set_name("Warrior Mastery");
  skill.set_kind(SKILL_KIND_PASSIVE);
  skill.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  skill.set_max_level(15);
  skill.mutable_base()->set_max_hp_per_level(6);
  skill.mutable_per_level()->set_max_hp_per_level(1);
  return skill;
}

class DerivedStatsTest : public testing::Test {
 protected:
  std::mt19937 rng_{0};
};

TEST_F(DerivedStatsTest, SumsAllocatedAndEquippedWithoutSkills) {
  CharacterInstance c = MakeCharacter(rng_, 15, 50, /*mp=*/20);
  EquipArmor(c, 100, 30, /*max_mp=*/40);

  DerivedStats stats = DerivedStatsFor(c, {});
  EXPECT_EQ(stats.max_hp, 150);
  EXPECT_EQ(stats.max_mp, 60);
  EXPECT_EQ(stats.def, 30);
  EXPECT_DOUBLE_EQ(stats.damage_taken_pct, 0.0);
}

TEST_F(DerivedStatsTest, PercentMpAppliesAfterEveryFlatSource) {
  CharacterInstance c = MakeCharacter(rng_, 15, 0, /*mp=*/50);
  Skill boost = MpBoost();
  std::map<std::string, Skill> skills = {{"mp_boost", boost}};
  ASSERT_TRUE(c.LearnSkill(boost, 20));

  // Flat first: 50 allocated + (25 + 5*19) * 15 levels = 1850, then the
  // skill's own +20% on the whole pile.
  DerivedStats stats = DerivedStatsFor(c, skills);
  EXPECT_EQ(stats.max_mp, 2220);
}

TEST_F(DerivedStatsTest, MpSkillsLeaveHpAlone) {
  CharacterInstance c = MakeCharacter(rng_, 15, 100, /*mp=*/50);
  Skill boost = MpBoost();
  std::map<std::string, Skill> skills = {{"mp_boost", boost}};
  ASSERT_TRUE(c.LearnSkill(boost, 20));

  EXPECT_EQ(DerivedStatsFor(c, skills).max_hp, 100);
}

TEST_F(DerivedStatsTest, UnlearnedPassivesContributeNothing) {
  CharacterInstance c = MakeCharacter(rng_, 15, 50);
  std::map<std::string, Skill> skills = {{"iron_body", IronBody()}};

  DerivedStats stats = DerivedStatsFor(c, skills);
  EXPECT_EQ(stats.max_hp, 50);
  EXPECT_EQ(stats.def, 0);
}

TEST_F(DerivedStatsTest, IronBodyScalesWithItsLearnedLevel) {
  CharacterInstance c = MakeCharacter(rng_, 15, 100);
  Skill iron_body = IronBody();
  std::map<std::string, Skill> skills = {{"iron_body", iron_body}};
  ASSERT_TRUE(c.LearnSkill(iron_body, 20));

  DerivedStats stats = DerivedStatsFor(c, skills);
  EXPECT_EQ(stats.def, 200);     // 10 * 20
  EXPECT_EQ(stats.max_hp, 120);  // 100 * (1 + 20%)
  EXPECT_NEAR(stats.damage_taken_pct, 0.10, 1e-9);
}

TEST_F(DerivedStatsTest, PerLevelHpScalesWithTheCharactersLevel) {
  CharacterInstance c = MakeCharacter(rng_, 15, 50);
  Skill mastery = WarriorMastery();
  std::map<std::string, Skill> skills = {{"warrior_mastery", mastery}};
  ASSERT_TRUE(c.LearnSkill(mastery, 1));

  // Level 1 of the skill grants 6 HP for each of the character's 15 levels.
  DerivedStats stats = DerivedStatsFor(c, skills);
  EXPECT_EQ(stats.max_hp, 50 + 6 * 15);
}

TEST_F(DerivedStatsTest, PercentHpAppliesAfterEveryFlatSource) {
  CharacterInstance c = MakeCharacter(rng_, 15, 50);
  EquipArmor(c, 100, 0);
  Skill iron_body = IronBody();
  Skill mastery = WarriorMastery();
  std::map<std::string, Skill> skills = {{"iron_body", iron_body},
                                         {"warrior_mastery", mastery}};
  ASSERT_TRUE(c.LearnSkill(iron_body, 10));
  ASSERT_TRUE(c.LearnSkill(mastery, 1));

  // Flat first: 50 allocated + 100 equipped + 6 * 15 per-level = 240, then
  // Iron Body's +10% on the whole pile. Applying the percent to any one source
  // alone would land short.
  DerivedStats stats = DerivedStatsFor(c, skills);
  EXPECT_EQ(stats.max_hp, 264);
}

TEST_F(DerivedStatsTest, PercentHpSurvivesItsOwnAccumulation) {
  CharacterInstance c = MakeCharacter(rng_, 15, 50);
  Skill iron_body = IronBody();
  std::map<std::string, Skill> skills = {{"iron_body", iron_body}};
  ASSERT_TRUE(c.LearnSkill(iron_body, 16));

  // 16 levels of +1% sums to a shade under 0.16 in floating point; flooring
  // that raw would report 57 for what is plainly 50 * 1.16.
  DerivedStats stats = DerivedStatsFor(c, skills);
  EXPECT_EQ(stats.max_hp, 58);
}

TEST_F(DerivedStatsTest, CritRateAccumulatesAcrossLearnedLevels) {
  CharacterInstance c = MakeCharacter(rng_, 15, 50);
  Skill critical_shot = CriticalShot();
  std::map<std::string, Skill> skills = {{"critical_shot", critical_shot}};
  ASSERT_TRUE(c.LearnSkill(critical_shot, 20));

  DerivedStats stats = DerivedStatsFor(c, skills);
  EXPECT_NEAR(stats.crit_rate, 0.40, 1e-9);  // 2% * 20
}

TEST_F(DerivedStatsTest, AttackSpeedBonusIsFlatRegardlessOfLevel) {
  CharacterInstance c = MakeCharacter(rng_, 15, 50);
  Skill mastery = ArcheryMastery();
  std::map<std::string, Skill> skills = {{"archery_mastery", mastery}};
  ASSERT_TRUE(c.LearnSkill(mastery, 15));  // maxed

  // The bonus is +1 at every level, so even a maxed skill adds a single stage.
  DerivedStats stats = DerivedStatsFor(c, skills);
  EXPECT_EQ(stats.attack_speed_bonus, 1);
}

TEST_F(DerivedStatsTest, SkillGrantedLukLandsInTheStatLine) {
  CharacterInstance c = MakeCharacter(rng_, 15, 50);
  Skill nimble = NimbleBody();
  std::map<std::string, Skill> skills = {{"nimble_body", nimble}};
  ASSERT_TRUE(c.LearnSkill(nimble, 20));

  DerivedStats stats = DerivedStatsFor(c, skills);
  EXPECT_EQ(stats.skill_stats.luk(), 20);  // 1 a level, maxed
}

TEST_F(DerivedStatsTest, SkillStatsJoinWornStatsInTheTotal) {
  CharacterInstance c = MakeCharacter(rng_, 15, 50);
  EquipArmor(c, /*max_hp=*/0, /*def=*/7);
  Skill nimble = NimbleBody();
  Skill iron_body = IronBody();
  std::map<std::string, Skill> skills = {{"nimble_body", nimble},
                                         {"iron_body", iron_body}};
  ASSERT_TRUE(c.LearnSkill(nimble, 5));
  ASSERT_TRUE(c.LearnSkill(iron_body, 1));

  // The total is what the rest of the game reads: the skill's LUK and the
  // armor's DEF arrive in the same stat line, indistinguishable by then.
  DerivedStats stats = DerivedStatsFor(c, skills);
  EquipStats total = TotalEquipStats(c, stats);
  EXPECT_EQ(total.luk(), 5);
  // DEF is carried in both places and must agree -- 7 worn, 10 from Iron Body.
  EXPECT_EQ(total.def(), 17);
  EXPECT_EQ(total.def(), stats.def);
}

TEST_F(DerivedStatsTest, AttackSkillsAreIgnored) {
  CharacterInstance c = MakeCharacter(rng_, 15, 50);
  Skill slash;
  slash.set_name("Slash Blast");
  slash.set_kind(SKILL_KIND_ATTACK);
  slash.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  slash.set_max_level(20);
  // An attack skill with defensive levers set still contributes none of them.
  slash.mutable_base()->set_def(999);
  slash.mutable_base()->set_max_hp_pct(9.0);
  std::map<std::string, Skill> skills = {{"slash_blast", slash}};
  ASSERT_TRUE(c.LearnSkill(slash, 1));

  DerivedStats stats = DerivedStatsFor(c, skills);
  EXPECT_EQ(stats.max_hp, 50);
  EXPECT_EQ(stats.def, 0);
}

}  // namespace
}  // namespace ms
