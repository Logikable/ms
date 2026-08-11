#include "src/character/character_stats.h"

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <random>
#include <string>
#include <utility>

#include "src/item/equip_instance.h"
#include "src/proto_loader.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/skill.pb.h"
#include "tools/cpp/runfiles/runfiles.h"

namespace ms {
namespace {

using bazel::tools::cpp::runfiles::Runfiles;

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

// Puts a weapon of `type` in the character's hand, for the skills that ask
// what they are being swung with.
void EquipWeapon(CharacterInstance& character, EquipType type) {
  EquipPrototype weapon;
  weapon.set_name("Weapon");
  weapon.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  weapon.set_equip_type(type);
  character.PickUp(std::make_unique<EquipInstance>(weapon));
  character.Equip(character.inventory().size() - 1);
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
  (*proto.mutable_sp_by_stage())[1] = 100;
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

// Magic Guard as the data states it: 22% of a hit to MP, 7% more a level.
Skill MagicGuard() {
  Skill skill;
  skill.set_name("Magic Guard");
  skill.set_kind(SKILL_KIND_PASSIVE);
  skill.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  skill.set_max_level(10);
  skill.mutable_base()->set_damage_to_mp_pct(0.22);
  skill.mutable_per_level()->set_damage_to_mp_pct(0.07);
  return skill;
}

TEST_F(DerivedStatsTest, MagicGuardCancelsTheDamageItSendsToMp) {
  CharacterInstance c = MakeCharacter(rng_, 15, 100);
  Skill magic_guard = MagicGuard();
  std::map<std::string, Skill> skills = {{"magic_guard", magic_guard}};
  ASSERT_TRUE(c.LearnSkill(magic_guard, 10));

  // Nothing tracks MP, so the 85% it diverts is 85% the character never takes.
  DerivedStats stats = DerivedStatsFor(c, skills);
  EXPECT_NEAR(stats.damage_taken_pct, 0.85, 1e-9);
}

TEST_F(DerivedStatsTest, TwoReductionsMultiplyRatherThanSum) {
  CharacterInstance c = MakeCharacter(rng_, 15, 100);
  Skill magic_guard = MagicGuard();
  Skill iron_body = IronBody();
  std::map<std::string, Skill> skills = {{"magic_guard", magic_guard},
                                         {"iron_body", iron_body}};
  ASSERT_TRUE(c.LearnSkill(magic_guard, 10));
  ASSERT_TRUE(c.LearnSkill(iron_body, 20));

  // 0.85 and 0.10 sum past nothing left to cancel; multiplied they leave
  // 0.15 * 0.90 of the hit standing.
  DerivedStats stats = DerivedStatsFor(c, skills);
  EXPECT_NEAR(stats.damage_taken_pct, 1.0 - 0.15 * 0.90, 1e-9);
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

  // 40% of an extra hit worth 160%. It names no tag, so every swing sets it
  // off -- which is what a Final Attack gated on the weapon in hand wants.
  DerivedStats stats = DerivedStatsFor(c, skills);
  ASSERT_EQ(stats.final_attacks.size(), 1u);
  EXPECT_NEAR(stats.final_attacks[0].pct, 0.64, 1e-9);
  EXPECT_EQ(stats.final_attacks[0].required_tag, SKILL_TAG_UNSPECIFIED);
}

TEST_F(DerivedStatsTest, NoFinalAttackIsWorthNothing) {
  CharacterInstance c = MakeCharacter(rng_, 40, 50);
  std::map<std::string, Skill> skills = {{"iron_body", IronBody()}};
  EXPECT_TRUE(DerivedStatsFor(c, skills).final_attacks.empty());
}

// Two arrow skills of the same shape are one extra arrow, which is how the
// Hunter's pair have always read. A skill that follows only some swings has to
// stay a source of its own, or it would follow all of them.
TEST_F(DerivedStatsTest, OnlyFinalAttacksFollowingTheSameSwingsMerge) {
  CharacterInstance c = MakeCharacter(rng_, 40, 50);
  Skill first = FinalAttack();
  Skill second = FinalAttack();
  second.set_name("Quiver Cartridge");
  Skill ignite = FinalAttack();
  ignite.set_name("Ignite");
  ignite.set_follows_skill_tag(SKILL_TAG_FIRE);
  std::map<std::string, Skill> skills = {
      {"first", first}, {"second", second}, {"ignite", ignite}};
  ASSERT_TRUE(c.LearnSkill(first, 20));
  ASSERT_TRUE(c.LearnSkill(second, 20));
  ASSERT_TRUE(c.LearnSkill(ignite, 20));

  DerivedStats stats = DerivedStatsFor(c, skills);
  ASSERT_EQ(stats.final_attacks.size(), 2u);
  EXPECT_NEAR(stats.final_attacks[0].pct, 1.28, 1e-9);  // the two that agree
  EXPECT_EQ(stats.final_attacks[0].required_tag, SKILL_TAG_UNSPECIFIED);
  EXPECT_NEAR(stats.final_attacks[1].pct, 0.64, 1e-9);
  EXPECT_EQ(stats.final_attacks[1].required_tag, SKILL_TAG_FIRE);
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

// The percentage takes the whole pile -- what the stats buy, what is worn and
// what a skill grants flat -- and leaves the base half of the pair alone,
// which is what the stats page reads to show the two.
TEST_F(DerivedStatsTest, DefPercentTakesTheWholePileAndLeavesBaseDefAlone) {
  CharacterInstance c = MakeStatCharacter(rng_, 100, 0, 0, 0);
  EquipArmor(c, /*max_hp=*/0, /*def=*/30);
  Skill mastery = IronBody();
  mastery.mutable_base()->set_def_pct(0.5);
  mastery.mutable_per_level()->set_def_pct(0.0);
  std::map<std::string, Skill> skills = {{"iron_body", mastery}};
  ASSERT_TRUE(c.LearnSkill(mastery, 1));

  // 150 from STR, 30 worn, 10 from the skill, half as much again over the lot.
  DerivedStats derived = DerivedStatsFor(c, skills);
  EXPECT_EQ(derived.base_def, 150);
  EXPECT_EQ(derived.def, 285);
}

// Combat Orders as the White Knight's book states it: one level for most of
// the ladder and two at the top, which is a step the per-level shape can only
// walk by carrying a fraction.
Skill CombatOrders() {
  Skill skill;
  skill.set_name("Combat Orders");
  skill.set_kind(SKILL_KIND_PASSIVE);
  skill.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  skill.set_max_level(10);
  skill.mutable_base()->set_skill_level_bonus(1.0);
  skill.mutable_per_level()->set_skill_level_bonus(0.11111111111);
  return skill;
}

TEST_F(DerivedStatsTest, BonusLevelsClimbInWholeStepsAndStopAtTwo) {
  CharacterInstance c = MakeCharacter(rng_, 100, 0);
  Skill orders = CombatOrders();
  std::map<std::string, Skill> skills = {{"combat_orders", orders}};

  EXPECT_EQ(BonusSkillLevels(c, skills), 0);
  ASSERT_TRUE(c.LearnSkill(orders, 1));
  EXPECT_EQ(BonusSkillLevels(c, skills), 1);
  ASSERT_TRUE(c.LearnSkill(orders, 8));
  EXPECT_EQ(BonusSkillLevels(c, skills), 1);
  ASSERT_TRUE(c.LearnSkill(orders, 1));
  EXPECT_EQ(BonusSkillLevels(c, skills), 2);
}

// The granted level is real everywhere a skill is read: Iron Body learned to
// 18 is worth its 20th level with two granted on top.
TEST_F(DerivedStatsTest, BonusLevelsRaiseWhatAPassiveGrants) {
  CharacterInstance c = MakeCharacter(rng_, 100, 0);
  Skill iron_body = IronBody();
  Skill orders = CombatOrders();
  std::map<std::string, Skill> skills = {{"iron_body", iron_body},
                                         {"combat_orders", orders}};
  ASSERT_TRUE(c.LearnSkill(iron_body, 18));
  EXPECT_EQ(DerivedStatsFor(c, skills).def, 180);

  ASSERT_TRUE(c.LearnSkill(orders, 10));
  EXPECT_EQ(DerivedStatsFor(c, skills).def, 200);
}

// Two rules the bonus has to hold at once: it never carries a skill past its
// master level, and it never raises the skill handing it out.
TEST_F(DerivedStatsTest, BonusLevelsStopAtTheMasterLevelAndSkipTheirOwnSkill) {
  CharacterInstance c = MakeCharacter(rng_, 100, 0);
  Skill iron_body = IronBody();
  Skill orders = CombatOrders();
  std::map<std::string, Skill> skills = {{"iron_body", iron_body},
                                         {"combat_orders", orders}};
  ASSERT_TRUE(c.LearnSkill(iron_body, 20));
  ASSERT_TRUE(c.LearnSkill(orders, 5));

  int bonus = BonusSkillLevels(c, skills);
  EXPECT_EQ(EffectiveSkillLevel(c, iron_body, bonus), 20);
  // Room to be raised, and still not raised: it is the skill handing out the
  // levels.
  EXPECT_EQ(EffectiveSkillLevel(c, orders, bonus), 5);
}

// A skill nobody has bought is not one the bonus teaches.
TEST_F(DerivedStatsTest, BonusLevelsLeaveAnUnlearnedSkillUnlearned) {
  CharacterInstance c = MakeCharacter(rng_, 100, 0);
  Skill iron_body = IronBody();
  Skill orders = CombatOrders();
  std::map<std::string, Skill> skills = {{"iron_body", iron_body},
                                         {"combat_orders", orders}};
  ASSERT_TRUE(c.LearnSkill(orders, 10));

  EXPECT_EQ(EffectiveSkillLevel(c, iron_body, BonusSkillLevels(c, skills)), 0);
  EXPECT_EQ(DerivedStatsFor(c, skills).def, 0);
}

// Another branch's Combat Orders is not this character's, so it hands out
// nothing -- the same rule that keeps a Page's Weapon Mastery off a Fighter.
TEST_F(DerivedStatsTest, BonusLevelsComeOnlyFromTheCharactersOwnBook) {
  CharacterInstance c = MakeCharacter(rng_, 100, 0);
  Skill iron_body = IronBody();
  Skill orders = CombatOrders();
  std::map<std::string, Skill> skills = {{"iron_body", iron_body},
                                         {"combat_orders", orders}};
  ASSERT_TRUE(c.LearnSkill(iron_body, 18));
  ASSERT_TRUE(c.LearnSkill(orders, 10));
  // Learned, then moved into a book this swordman does not hold. The level
  // stays -- it is keyed by display name -- and stops counting.
  skills["combat_orders"].set_job_advancement(JOB_ADVANCEMENT_MAGICIAN);

  EXPECT_EQ(BonusSkillLevels(c, skills), 0);
  EXPECT_EQ(DerivedStatsFor(c, skills).def, 180);
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

// Spirit Blade's two new levers: attack in the same shape a weapon grants it,
// and the share of a hit that goes back into whatever landed it.
TEST_F(DerivedStatsTest, AttackAndReflectionFoldIn) {
  CharacterInstance c = MakeCharacter(rng_, 60, 0);
  Skill blade;
  blade.set_name("Spirit Blade");
  blade.set_kind(SKILL_KIND_PASSIVE);
  blade.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  blade.set_max_level(20);
  blade.mutable_base()->set_attack(11);
  blade.mutable_base()->set_damage_reflect_pct(1.2);
  blade.mutable_per_level()->set_attack(1);
  blade.mutable_per_level()->set_damage_reflect_pct(0.2);
  std::map<std::string, Skill> skills = {{"spirit_blade", blade}};
  ASSERT_TRUE(c.LearnSkill(blade, 20));

  DerivedStats stats = DerivedStatsFor(c, skills);
  EXPECT_EQ(stats.skill_stats.attack(), 30);
  EXPECT_DOUBLE_EQ(stats.damage_reflect_pct, 5.0);
}

// Combo Attack states its attack per orb and how many orbs it hands out. The
// orbs are taken as full, so what the character carries is the product.
TEST_F(DerivedStatsTest, ComboOrbsAreWorthTheirAttackApiece) {
  CharacterInstance c = MakeCharacter(rng_, 60, 0);
  Skill combo;
  combo.set_name("Combo Attack");
  combo.set_kind(SKILL_KIND_PASSIVE);
  combo.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  combo.set_max_level(1);
  combo.set_combo_orbs(5);
  combo.mutable_base()->set_attack_per_combo_orb(2);
  std::map<std::string, Skill> skills = {{"combo_attack", combo}};
  ASSERT_TRUE(c.LearnSkill(combo, 1));

  EXPECT_EQ(DerivedStatsFor(c, skills).skill_stats.attack(), 10);
}

// The skill pricing the orbs is not the skill handing them out, so the count
// has to reach across the book: Combo Synergy states final damage per orb and
// Combo Attack alone says there are five. Priced against the ring rather than
// against nothing, the pair is worth 5 x 5% -- and against the same ring, the
// attack per orb still lands.
TEST_F(DerivedStatsTest, OneSkillPricesTheOrbsAnotherHandsOut) {
  CharacterInstance c = MakeCharacter(rng_, 60, 0);
  Skill combo;
  combo.set_name("Combo Attack");
  combo.set_kind(SKILL_KIND_PASSIVE);
  combo.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  combo.set_max_level(1);
  combo.set_combo_orbs(5);
  combo.mutable_base()->set_attack_per_combo_orb(2);
  Skill synergy;
  synergy.set_name("Combo Synergy");
  synergy.set_kind(SKILL_KIND_PASSIVE);
  synergy.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  synergy.set_max_level(20);
  synergy.mutable_base()->set_final_dmg_pct_per_combo_orb(0.0025);
  synergy.mutable_per_level()->set_final_dmg_pct_per_combo_orb(0.0025);
  std::map<std::string, Skill> skills = {{"combo_attack", combo},
                                         {"combo_synergy", synergy}};
  ASSERT_TRUE(c.LearnSkill(combo, 1));
  ASSERT_TRUE(c.LearnSkill(synergy, 20));

  DerivedStats stats = DerivedStatsFor(c, skills);
  EXPECT_EQ(stats.skill_stats.attack(), 10);
  EXPECT_NEAR(stats.final_dmg_pct, 0.25, 1e-9);
}

// A character carries one ring of orbs however many skills describe it, so two
// skills naming a count leave the larger, not the pair added up. This is the
// rule that matters when a later skill raises the maximum -- summed, it would
// stack on the old count instead of replacing it.
TEST_F(DerivedStatsTest, TwoOrbCountsLeaveTheLargerRing) {
  CharacterInstance c = MakeCharacter(rng_, 60, 0);
  std::map<std::string, Skill> skills;
  int counts[] = {5, 8};
  for (int i = 0; i < 2; ++i) {
    Skill skill;
    skill.set_name("Combo " + std::to_string(i));
    skill.set_kind(SKILL_KIND_PASSIVE);
    skill.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
    skill.set_max_level(1);
    skill.set_combo_orbs(counts[i]);
    skills.insert({"combo_" + std::to_string(i), skill});
    ASSERT_TRUE(c.LearnSkill(skill, 1));
  }
  Skill priced;
  priced.set_name("Combo Attack");
  priced.set_kind(SKILL_KIND_PASSIVE);
  priced.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  priced.set_max_level(1);
  priced.mutable_base()->set_attack_per_combo_orb(2);
  skills.insert({"combo_attack", priced});
  ASSERT_TRUE(c.LearnSkill(priced, 1));

  EXPECT_EQ(DerivedStatsFor(c, skills).skill_stats.attack(), 16);
}

// A ring nobody hands out is no ring: the bargain is priced against nothing
// and the character is left with the plain final damage they bought.
TEST_F(DerivedStatsTest, PerOrbFinalDamageIsWorthNothingWithoutOrbs) {
  CharacterInstance c = MakeCharacter(rng_, 60, 0);
  Skill synergy;
  synergy.set_name("Combo Synergy");
  synergy.set_kind(SKILL_KIND_PASSIVE);
  synergy.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  synergy.set_max_level(20);
  synergy.mutable_base()->set_final_dmg_pct_per_combo_orb(0.05);
  synergy.mutable_base()->set_final_dmg_pct(0.10);
  std::map<std::string, Skill> skills = {{"combo_synergy", synergy}};
  ASSERT_TRUE(c.LearnSkill(synergy, 1));

  EXPECT_NEAR(DerivedStatsFor(c, skills).final_dmg_pct, 0.10, 1e-9);
}

// The two damage levers differ only once a second source exists: % damage
// sums, final damage multiplies. Two skills of 10% each come to 20% and 21%.
TEST_F(DerivedStatsTest, DamagePercentSumsAndFinalDamageMultiplies) {
  CharacterInstance c = MakeCharacter(rng_, 60, 0);
  std::map<std::string, Skill> skills;
  for (const std::string& name : {"first", "second"}) {
    Skill skill;
    skill.set_name(name);
    skill.set_kind(SKILL_KIND_PASSIVE);
    skill.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
    skill.set_max_level(1);
    skill.mutable_base()->set_damage_pct(0.10);
    skill.mutable_base()->set_final_dmg_pct(0.10);
    ASSERT_TRUE(c.LearnSkill(skill, 1));
    skills[name] = skill;
  }

  DerivedStats stats = DerivedStatsFor(c, skills);
  EXPECT_DOUBLE_EQ(stats.damage_pct, 0.20);
  // Not DOUBLE_EQ: 1.1 * 1.1 - 1 lands a few ulps off the 0.21 it means.
  EXPECT_NEAR(stats.final_dmg_pct, 0.21, 1e-9);
}

// Both levers have to reach the damage chain, and PassiveOffenseFor is the
// only thing carrying them across.
TEST_F(DerivedStatsTest, TheDamageLeversReachTheOffenseStats) {
  DerivedStats stats;
  stats.damage_pct = 0.15;
  stats.final_dmg_pct = 0.25;
  stats.crit_dmg = 0.05;
  PassiveOffense passives = PassiveOffenseFor(stats);
  EXPECT_DOUBLE_EQ(passives.damage_pct, 0.15);
  EXPECT_DOUBLE_EQ(passives.final_dmg_pct, 0.25);
  EXPECT_DOUBLE_EQ(passives.crit_dmg, 0.05);
}

// High Wisdom grants the magician's own stat. It reaches the stat line like
// any other stat, and buys no DEF -- which is the rule for INT wherever it
// comes from.
TEST_F(DerivedStatsTest, SkillGrantedIntLandsInTheStatLineAndBuysNoDef) {
  CharacterInstance c = MakeCharacter(rng_, 60, 0);
  Skill wisdom;
  wisdom.set_name("High Wisdom");
  wisdom.set_kind(SKILL_KIND_PASSIVE);
  wisdom.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  wisdom.set_max_level(5);
  wisdom.mutable_base()->set_int_(8);
  wisdom.mutable_per_level()->set_int_(8);
  std::map<std::string, Skill> skills = {{"high_wisdom", wisdom}};
  ASSERT_TRUE(c.LearnSkill(wisdom, 5));

  DerivedStats stats = DerivedStatsFor(c, skills);
  EXPECT_EQ(stats.skill_stats.int_(), 40);
  EXPECT_EQ(TotalEquipStats(c, stats).int_(), 40);
  EXPECT_EQ(stats.def, 0);
}

// Freezing Crush's pair: critical damage rides beside crit rate, and magic
// attack lands in the stat line exactly as a staff's would -- which is the
// only way a magician's skills reach their own damage.
TEST_F(DerivedStatsTest, MagicAttackAndCritDamageFoldIn) {
  CharacterInstance c = MakeCharacter(rng_, 60, 0);
  Skill crush;
  crush.set_name("Freezing Crush");
  crush.set_kind(SKILL_KIND_PASSIVE);
  crush.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  crush.set_max_level(10);
  crush.mutable_base()->set_crit_dmg(0.005);
  crush.mutable_base()->set_magic_attack(3);
  crush.mutable_per_level()->set_crit_dmg(0.005);
  crush.mutable_per_level()->set_magic_attack(3);
  std::map<std::string, Skill> skills = {{"freezing_crush", crush}};
  ASSERT_TRUE(c.LearnSkill(crush, 10));

  DerivedStats stats = DerivedStatsFor(c, skills);
  EXPECT_NEAR(stats.crit_dmg, 0.05, 1e-9);
  EXPECT_EQ(stats.skill_stats.magic_attack(), 30);
  EXPECT_EQ(TotalEquipStats(c, stats).magic_attack(), 30);
}

// Weapon Mastery masters a spear and a polearm alike, but only a spear swings
// faster for it. The skill keeps working with either; only the bonus lapses.
TEST_F(DerivedStatsTest, AWeaponBonusLandsOnlyForItsOwnWeapons) {
  CharacterInstance c = MakeCharacter(rng_, 60, 0);
  Skill mastery;
  mastery.set_name("Weapon Mastery");
  mastery.set_kind(SKILL_KIND_PASSIVE);
  mastery.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  mastery.set_max_level(10);
  mastery.mutable_base()->set_mastery(0.5);
  mastery.add_required_equip_type(EQUIP_TYPE_SPEAR);
  mastery.add_required_equip_type(EQUIP_TYPE_POLEARM);
  WeaponBonus* bonus = mastery.add_weapon_bonus();
  bonus->add_required_equip_type(EQUIP_TYPE_SPEAR);
  bonus->mutable_effect()->set_attack_speed(1);
  bonus->mutable_effect()->set_damage_pct(0.05);
  std::map<std::string, Skill> skills = {{"weapon_mastery", mastery}};
  ASSERT_TRUE(c.LearnSkill(mastery, 1));

  EquipWeapon(c, EQUIP_TYPE_POLEARM);
  DerivedStats polearm = DerivedStatsFor(c, skills);
  EXPECT_DOUBLE_EQ(polearm.mastery, 0.5);
  EXPECT_EQ(polearm.attack_speed_bonus, 0);
  EXPECT_DOUBLE_EQ(polearm.damage_pct, 0.0);

  c.Unequip(EQUIP_SLOT_PRIMARY_WEAPON);
  EquipWeapon(c, EQUIP_TYPE_SPEAR);
  DerivedStats spear = DerivedStatsFor(c, skills);
  EXPECT_DOUBLE_EQ(spear.mastery, 0.5);
  EXPECT_EQ(spear.attack_speed_bonus, 1);
  EXPECT_DOUBLE_EQ(spear.damage_pct, 0.05);
}

// A bonus is flat: it says the same thing at level 1 as at max, so a skill
// levelled up must not multiply it up with everything else.
TEST_F(DerivedStatsTest, AWeaponBonusDoesNotGrowWithTheSkill) {
  CharacterInstance c = MakeCharacter(rng_, 60, 0);
  Skill mastery;
  mastery.set_name("Weapon Mastery");
  mastery.set_kind(SKILL_KIND_PASSIVE);
  mastery.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  mastery.set_max_level(10);
  mastery.mutable_per_level()->set_str(1);
  WeaponBonus* bonus = mastery.add_weapon_bonus();
  bonus->add_required_equip_type(EQUIP_TYPE_SPEAR);
  bonus->mutable_effect()->set_damage_pct(0.05);
  std::map<std::string, Skill> skills = {{"weapon_mastery", mastery}};
  ASSERT_TRUE(c.LearnSkill(mastery, 10));
  EquipWeapon(c, EQUIP_TYPE_SPEAR);

  DerivedStats stats = DerivedStatsFor(c, skills);
  ASSERT_EQ(stats.skill_stats.str(), 9);  // the skill really is at level 10
  EXPECT_DOUBLE_EQ(stats.damage_pct, 0.05);
}

// Final Attack demands a sword or an axe. A learned skill whose weapon is not
// in hand grants nothing, and grants it all again once it is.
TEST_F(DerivedStatsTest, APassiveLapsesWithoutTheWeaponItNames) {
  CharacterInstance c = MakeCharacter(rng_, 60, 0);
  Skill training = PhysicalTraining();
  training.add_required_equip_type(EQUIP_TYPE_TWO_HANDED_AXE);
  std::map<std::string, Skill> skills = {{"physical_training", training}};
  ASSERT_TRUE(c.LearnSkill(training, 5));
  EquipWeapon(c, EQUIP_TYPE_ONE_HANDED_SWORD);

  EXPECT_EQ(DerivedStatsFor(c, skills).skill_stats.str(), 0);

  c.Unequip(EQUIP_SLOT_PRIMARY_WEAPON);
  EquipWeapon(c, EQUIP_TYPE_TWO_HANDED_AXE);
  EXPECT_EQ(DerivedStatsFor(c, skills).skill_stats.str(), 30);
}

// The shipped Shield Mastery on a shipped Bandit holding a shipped scabbard.
// The synthetic case below pins the rule; this pins that the rule reaches the
// one skill written against it, through the real data on both sides.
TEST_F(DerivedStatsTest, ABanditsShieldMasteryWaitsForTheScabbard) {
  std::string err;
  std::unique_ptr<Runfiles> runfiles(Runfiles::CreateForTest(&err));
  ASSERT_NE(runfiles, nullptr) << err;
  std::map<std::string, Skill> skills =
      LoadTextProtoDir<Skill>(runfiles->Rlocation("ms/data/skills"));
  std::map<std::string, EquipPrototype> equips =
      LoadTextProtoDir<EquipPrototype>(runfiles->Rlocation("ms/data/equip"));
  const Skill& mastery = skills.at("shield_mastery");

  Character proto;
  proto.set_level(60);
  proto.set_job(JOB_BANDIT);
  proto.set_job_stage(2);
  (*proto.mutable_sp_by_stage())[2] = 100;
  CharacterInstance c(rng_, std::move(proto));
  ASSERT_TRUE(c.LearnSkill(mastery, mastery.max_level()));

  // Learned and holding nothing: the skill grants none of its three levers.
  DerivedStats bare = DerivedStatsFor(c, skills);
  EXPECT_EQ(bare.skill_stats.attack(), 0);
  EXPECT_DOUBLE_EQ(bare.damage_taken_pct, 0.0);
  EXPECT_EQ(bare.def, bare.base_def);

  c.PickUp(std::make_unique<EquipInstance>(equips.at("hidden_shadow")));
  ASSERT_TRUE(c.Equip(c.inventory().size() - 1));
  ASSERT_TRUE(c.has_secondary());

  // GMS's own figures at level 10: +20 attack and 60% of damage turned aside.
  DerivedStats armed = DerivedStatsFor(c, skills);
  EXPECT_EQ(armed.skill_stats.attack(), 20);
  EXPECT_DOUBLE_EQ(armed.damage_taken_pct, 0.6);

  // And DEF at 2.1 times what the same character carries without the lever,
  // which is the only way to ask it of a character wearing real gear.
  std::map<std::string, Skill> without = skills;
  without.at("shield_mastery").mutable_base()->clear_def_pct();
  without.at("shield_mastery").mutable_per_level()->clear_def_pct();
  EXPECT_EQ(armed.def, static_cast<int>(DerivedStatsFor(c, without).def * 2.1));
}

// Shield Mastery asks for a filled off hand rather than a weapon type. The
// synthetic passive keeps the rule under test on its own, apart from whatever
// the shipped skill happens to grant.
TEST_F(DerivedStatsTest, APassiveLapsesWithoutTheSecondaryItNames) {
  CharacterInstance c = MakeCharacter(rng_, 60, 0);
  Skill training = PhysicalTraining();
  training.set_requires_secondary(true);
  std::map<std::string, Skill> skills = {{"physical_training", training}};
  ASSERT_TRUE(c.LearnSkill(training, 5));
  EquipWeapon(c, EQUIP_TYPE_DAGGER);

  ASSERT_FALSE(c.has_secondary());
  EXPECT_EQ(DerivedStatsFor(c, skills).skill_stats.str(), 0);

  EquipPrototype scabbard;
  scabbard.set_name("Dagger Scabbard");
  scabbard.set_equip_slot(EQUIP_SLOT_SECONDARY);
  c.PickUp(std::make_unique<EquipInstance>(scabbard));
  ASSERT_TRUE(c.Equip(c.inventory().size() - 1));
  ASSERT_TRUE(c.has_secondary());
  EXPECT_EQ(DerivedStatsFor(c, skills).skill_stats.str(), 30);
}

}  // namespace
}  // namespace ms
