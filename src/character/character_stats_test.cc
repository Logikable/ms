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
  proto.set_job(JOB_SWORDMAN);
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

// A character holding the four primary stats outright. These tests care what
// the character holds, not how many AP it took to get there.
CharacterInstance MakeStatCharacter(std::mt19937& rng, int str, int dex,
                                    int int_, int luk) {
  Character proto;
  proto.set_level(1);
  proto.set_job(JOB_SWORDMAN);
  proto.set_job_stage(1);
  AllocatedStats* stats = proto.mutable_allocated_stats();
  stats->set_str(str);
  stats->set_dex(dex);
  stats->set_int_(int_);
  stats->set_luk(luk);
  return CharacterInstance(rng, std::move(proto));
}

// A ring-shaped item carrying STR, for asking whether worn stats count.
void EquipStrRing(CharacterInstance& character, int str) {
  EquipPrototype ring;
  ring.set_name("Ring");
  ring.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  ring.mutable_base_stats()->set_str(str);
  character.PickUp(std::make_unique<EquipInstance>(ring));
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

// Nimble Body's shape -- +1 LUK a level -- filed under the warrior's book.
// These tests are about how a lever folds, not about whose book it came from,
// and a skill of another job is one this character cannot learn at all.
Skill NimbleBody() {
  Skill skill;
  skill.set_name("Nimble Body");
  skill.set_kind(SKILL_KIND_PASSIVE);
  skill.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  skill.set_max_level(20);
  skill.mutable_base()->set_luk(1);
  skill.mutable_per_level()->set_luk(1);
  return skill;
}

// Physical Training as the wiki states it: +6 STR and +6 DEX a level.
Skill PhysicalTraining() {
  Skill skill;
  skill.set_name("Physical Training");
  skill.set_kind(SKILL_KIND_PASSIVE);
  skill.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  skill.set_max_level(5);
  skill.mutable_base()->set_str(6);
  skill.mutable_base()->set_dex(6);
  skill.mutable_per_level()->set_str(6);
  skill.mutable_per_level()->set_dex(6);
  return skill;
}

// Weapon Mastery as the wiki states it: mastery 10 + 4*L percent.
Skill WeaponMastery() {
  Skill skill;
  skill.set_name("Weapon Mastery");
  skill.set_kind(SKILL_KIND_PASSIVE);
  skill.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  skill.set_max_level(10);
  skill.mutable_base()->set_mastery(0.14);
  skill.mutable_per_level()->set_mastery(0.04);
  return skill;
}

// Final Attack as the wiki states it for a Spearman: a 2*L% chance of an
// extra hit worth 2 lines of (60+L)%.
Skill FinalAttack() {
  Skill skill;
  skill.set_name("Final Attack");
  skill.set_kind(SKILL_KIND_PASSIVE);
  skill.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  skill.set_max_level(20);
  skill.mutable_base()->set_final_attack_chance(0.02);
  skill.mutable_base()->set_final_attack_pct(1.22);
  skill.mutable_per_level()->set_final_attack_chance(0.02);
  skill.mutable_per_level()->set_final_attack_pct(0.02);
  return skill;
}

// Archery Mastery's shape: +1 attack speed stage, flat at every level. Filed
// under the warrior's book for the reason Nimble Body is.
Skill ArcheryMastery() {
  Skill skill;
  skill.set_name("Archery Mastery");
  skill.set_kind(SKILL_KIND_PASSIVE);
  skill.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
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

TEST_F(DerivedStatsTest, SkillGrantedStrAndDexLandInTheStatLine) {
  CharacterInstance c = MakeCharacter(rng_, 40, 50);
  Skill training = PhysicalTraining();
  std::map<std::string, Skill> skills = {{"physical_training", training}};
  ASSERT_TRUE(c.LearnSkill(training, 5));

  DerivedStats stats = DerivedStatsFor(c, skills);
  EXPECT_EQ(stats.skill_stats.str(), 30);  // 6 a level, maxed
  EXPECT_EQ(stats.skill_stats.dex(), 30);
}

// A skill's STR is worth exactly as much base DEF as an AP-spent point, which
// is the whole reason the base is computed off the totals rather than the
// allocation.
TEST_F(DerivedStatsTest, SkillGrantedStrBuysBaseDefLikeAnyOtherStr) {
  CharacterInstance c = MakeCharacter(rng_, 40, 50);
  Skill training = PhysicalTraining();
  std::map<std::string, Skill> skills = {{"physical_training", training}};
  int bare = DerivedStatsFor(c, skills).def;
  ASSERT_TRUE(c.LearnSkill(training, 5));

  // 30 STR at 1.5 DEF apiece and 30 DEX at 0.4.
  EXPECT_EQ(DerivedStatsFor(c, skills).def, bare + 45 + 12);
}

TEST_F(DerivedStatsTest, WeaponMasteryReachesTheDerivedStats) {
  CharacterInstance c = MakeCharacter(rng_, 40, 50);
  Skill mastery = WeaponMastery();
  std::map<std::string, Skill> skills = {{"weapon_mastery", mastery}};
  ASSERT_TRUE(c.LearnSkill(mastery, 10));

  EXPECT_DOUBLE_EQ(DerivedStatsFor(c, skills).mastery, 0.50);  // 10 + 4*10 %
}

// Two masteries are not twice as steady a swing -- they are the better of the
// two. Every other lever here sums.
TEST_F(DerivedStatsTest, MasteriesTakeTheBestRatherThanTheSum) {
  CharacterInstance c = MakeCharacter(rng_, 40, 50);
  Skill mastery = WeaponMastery();
  Skill other = WeaponMastery();
  other.set_name("Other Mastery");
  std::map<std::string, Skill> skills = {{"weapon_mastery", mastery},
                                         {"other_mastery", other}};
  ASSERT_TRUE(c.LearnSkill(mastery, 10));
  ASSERT_TRUE(c.LearnSkill(other, 4));

  EXPECT_DOUBLE_EQ(DerivedStatsFor(c, skills).mastery, 0.50);
}

TEST_F(DerivedStatsTest, NoMasterySkillKeepsTheBaseline) {
  CharacterInstance c = MakeCharacter(rng_, 40, 50);
  std::map<std::string, Skill> skills = {{"iron_body", IronBody()}};
  EXPECT_DOUBLE_EQ(DerivedStatsFor(c, skills).mastery, 0.0);
}

// The chance and the damage are separate on the skill because that is what
// the player is shown, but only their product can reach an expected-value
// damage chain.
TEST_F(DerivedStatsTest, FinalAttackCollapsesToWhatASwingIsWorth) {
  CharacterInstance c = MakeCharacter(rng_, 40, 50);
  Skill final_attack = FinalAttack();
  std::map<std::string, Skill> skills = {{"final_attack", final_attack}};
  ASSERT_TRUE(c.LearnSkill(final_attack, 20));

  // 40% of an extra hit worth 160%.
  EXPECT_NEAR(DerivedStatsFor(c, skills).final_attack_pct, 0.64, 1e-9);
}

TEST_F(DerivedStatsTest, NoFinalAttackIsWorthNothing) {
  CharacterInstance c = MakeCharacter(rng_, 40, 50);
  std::map<std::string, Skill> skills = {{"iron_body", IronBody()}};
  EXPECT_DOUBLE_EQ(DerivedStatsFor(c, skills).final_attack_pct, 0.0);
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
  // 7 worn, 10 from Iron Body. This is DEF as a stat line carries it, which is
  // not the DEF the character has -- stats.def adds the primary-stat base on
  // top, and here that is the 2 the skill's own 5 LUK is worth.
  EXPECT_EQ(total.def(), 17);
  EXPECT_EQ(stats.def, 19);
}

// --- base DEF from the primary stats ---

// Every character carries DEF before they wear anything: 1.5 a point of STR,
// 0.4 a point of DEX and of LUK. A level-1 character in rags is not at zero.
TEST_F(DerivedStatsTest, PrimaryStatsCarryDefWithNothingWorn) {
  CharacterInstance c = MakeStatCharacter(rng_, 13, 4, 4, 4);

  // 1.5*13 + 0.4*(4+4) = 19.5 + 3.2 = 22.7, floored.
  EXPECT_EQ(DerivedStatsFor(c, {}).def, 22);
}

// INT buys no DEF at all, which is what separates it from the other three.
TEST_F(DerivedStatsTest, IntBuysNoDef) {
  CharacterInstance c = MakeStatCharacter(rng_, 0, 0, 500, 0);

  EXPECT_EQ(DerivedStatsFor(c, {}).def, 0);
}

// STR is worth nearly four times what DEX and LUK are, so the same AP spent
// three ways does not buy the same bulk.
TEST_F(DerivedStatsTest, StrIsWorthMoreDefThanDexOrLuk) {
  CharacterInstance strong = MakeStatCharacter(rng_, 100, 0, 0, 0);
  CharacterInstance quick = MakeStatCharacter(rng_, 0, 100, 0, 0);
  CharacterInstance lucky = MakeStatCharacter(rng_, 0, 0, 0, 100);

  EXPECT_EQ(DerivedStatsFor(strong, {}).def, 150);
  EXPECT_EQ(DerivedStatsFor(quick, {}).def, 40);
  EXPECT_EQ(DerivedStatsFor(lucky, {}).def, 40);
}

// The base stacks with armour rather than replacing it or being replaced.
TEST_F(DerivedStatsTest, BaseDefAddsToWornDef) {
  CharacterInstance c = MakeStatCharacter(rng_, 100, 0, 0, 0);
  EquipArmor(c, /*max_hp=*/0, /*def=*/30);

  EXPECT_EQ(DerivedStatsFor(c, {}).def, 180);
}

// A stat granted by gear is worth exactly what an allocated one is, so the
// formula has to read the total rather than the AP spend.
TEST_F(DerivedStatsTest, StatsFromGearBuyDefToo) {
  CharacterInstance allocated = MakeStatCharacter(rng_, 100, 0, 0, 0);
  CharacterInstance worn = MakeStatCharacter(rng_, 0, 0, 0, 0);
  EquipStrRing(worn, /*str=*/100);

  EXPECT_EQ(DerivedStatsFor(worn, {}).def, DerivedStatsFor(allocated, {}).def);
}

// And so is one granted by a passive. Nimble Body's LUK reaches DEF the same
// way a ring's would, which is what reading skill_stats back buys.
TEST_F(DerivedStatsTest, StatsFromPassivesBuyDefToo) {
  CharacterInstance c = MakeCharacter(rng_, 15, 50);
  Skill nimble = NimbleBody();
  std::map<std::string, Skill> skills = {{"nimble_body", nimble}};
  ASSERT_TRUE(c.LearnSkill(nimble, 20));

  // 20 LUK from the skill, at 0.4 DEF a point.
  EXPECT_EQ(DerivedStatsFor(c, skills).def, 8);
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

// Fighter, Page and Spearman share four skill names between them, and learned
// levels are keyed by display name -- so one catalog holds several entries
// answering to a single learned level. Only the character's own book may fold
// in, or the other branch's copy doubles it.
TEST_F(DerivedStatsTest, AnotherBranchsCopyOfASharedNameIsIgnored) {
  Character proto;
  proto.set_level(60);
  proto.set_job(JOB_SPEARMAN);
  proto.set_job_stage(2);
  (*proto.mutable_skill_levels())["Physical Training"] = 5;
  CharacterInstance c(rng_, std::move(proto));

  Skill mine = PhysicalTraining();
  mine.set_job_advancement(JOB_ADVANCEMENT_SPEARMAN);
  Skill theirs = mine;
  theirs.set_job_advancement(JOB_ADVANCEMENT_FIGHTER);
  std::map<std::string, Skill> skills = {{"spearman_physical_training", mine},
                                         {"fighter_physical_training", theirs}};

  // 30 STR, not 60: the Fighter's entry is a different job's book.
  EXPECT_EQ(DerivedStatsFor(c, skills).skill_stats.str(), 30);
}

// And the character's own book still folds in, which is the other half of the
// same check.
TEST_F(DerivedStatsTest, TheCharactersOwnBookStillCounts) {
  Character proto;
  proto.set_level(60);
  proto.set_job(JOB_SPEARMAN);
  proto.set_job_stage(2);
  (*proto.mutable_skill_levels())["Physical Training"] = 5;
  CharacterInstance c(rng_, std::move(proto));

  Skill mine = PhysicalTraining();
  mine.set_job_advancement(JOB_ADVANCEMENT_SPEARMAN);
  std::map<std::string, Skill> skills = {{"spearman_physical_training", mine}};
  EXPECT_EQ(DerivedStatsFor(c, skills).skill_stats.str(), 30);
}

}  // namespace
}  // namespace ms
