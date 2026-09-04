#include "src/character/character_stats.h"

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "src/character/consumables.h"
#include "src/character/inner_ability.h"
#include "src/character/v_matrix.h"
#include "src/item/equip_instance.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/skill.pb.h"
#include "src/testing/data_files.h"
#include "src/testing/prototypes.h"

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

// Advanced Blessing, trimmed to the pools: an outright grant, the same at
// level 1 of the character as at 140.
Skill AdvancedBlessing() {
  Skill skill;
  skill.set_name("Advanced Blessing");
  skill.set_kind(SKILL_KIND_PASSIVE);
  skill.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  skill.set_max_level(10);
  skill.mutable_base()->set_max_hp(525);
  skill.mutable_base()->set_max_mp(525);
  skill.mutable_per_level()->set_max_hp(25);
  skill.mutable_per_level()->set_max_mp(25);
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

// A V Matrix node grants what it states like any other passive: it belongs to
// no book the character advanced through, so nothing is allowed to gate it on
// one.
TEST_F(DerivedStatsTest, ACommonNodeGrantsWhatItStates) {
  CharacterInstance c = MakeCharacter(rng_, 15, 50, /*mp=*/20);
  Skill rope;
  rope.set_name("Rope Lift");
  rope.set_kind(SKILL_KIND_PASSIVE);
  rope.set_job_advancement(JOB_ADVANCEMENT_COMMON);
  rope.set_v_node(V_NODE_KIND_COMMON);
  rope.set_max_level(MaxVNodeLevel(V_NODE_KIND_COMMON));
  rope.mutable_base()->set_str(1);
  rope.mutable_per_level()->set_str(1);
  std::map<std::string, Skill> catalog = {{"rope_lift", rope}};

  EXPECT_EQ(TotalEquipStats(c, DerivedStatsFor(c, catalog)).str(), 0)
      << "unlearned it pays nothing";

  c.AdvanceJob(c.proto().job());  // whatever stage the fixture left them at
  while (c.proto().job_stage() < kFifthJobStage) {
    c.AdvanceJob(c.proto().job());
  }
  c.AddVPoints(VNodeCost(V_NODE_KIND_COMMON, 0, rope.max_level()));
  ASSERT_TRUE(c.LearnSkill(rope, rope.max_level()));
  EXPECT_EQ(TotalEquipStats(c, DerivedStatsFor(c, catalog)).str(), 30)
      << "a point a level, to 30";
}

// A node whose ladder climbs every fifth level rather than every one, which is
// what GMS's Decent nodes pay. The fraction is floored where it is read, so
// the character sees whole points at the levels GMS steps on.
TEST_F(DerivedStatsTest, AFractionalLadderStepsEveryFifthLevel) {
  Skill door;
  door.set_name("Decent Mystic Door");
  door.set_kind(SKILL_KIND_PASSIVE);
  door.set_job_advancement(JOB_ADVANCEMENT_COMMON);
  door.set_v_node(V_NODE_KIND_COMMON);
  door.set_max_level(MaxVNodeLevel(V_NODE_KIND_COMMON));
  door.mutable_base()->set_str(1);
  door.mutable_per_level()->set_str(0.2);
  std::map<std::string, Skill> catalog = {{"decent_mystic_door", door}};

  CharacterInstance c = MakeCharacter(rng_, 15, 50, /*mp=*/20);
  while (c.proto().job_stage() < kFifthJobStage) {
    c.AdvanceJob(c.proto().job());
  }
  c.AddVPoints(VNodeCost(V_NODE_KIND_COMMON, 0, door.max_level()));

  const int kExpected[] = {1, 1, 2, 2, 6};
  const int kLevels[] = {1, 5, 6, 10, 30};
  for (int i = 0; i < 5; ++i) {
    ASSERT_TRUE(c.LearnSkill(door, kLevels[i] - c.skill_level(door)));
    EXPECT_EQ(TotalEquipStats(c, DerivedStatsFor(c, catalog)).str(),
              kExpected[i])
        << "level " << kLevels[i];
  }
}

// --- set bonuses ---

// A four-piece set, tiered at three and four, with levers on both sides of the
// pipeline: flat stats and attack, and a percentage over the HP pool.
std::map<std::string, EquipSet> FrozenSet() {
  const EquipSlot kSlots[] = {EQUIP_SLOT_TOP, EQUIP_SLOT_BOTTOM, EQUIP_SLOT_HAT,
                              EQUIP_SLOT_CAPE};
  const char* kPieces[] = {"Top", "Bottom", "Hat", "Cape"};
  EquipSet set;
  set.set_name(EQUIP_SET_NAME_FROZEN);
  for (int i = 0; i < 4; ++i) {
    EquipSetMember* member = set.add_members();
    member->set_slot(kSlots[i]);
    member->mutable_items()->add_name(std::string("Frozen ") + kPieces[i]);
  }
  EquipSetTier* three = set.add_tiers();
  three->set_pieces(3);
  three->mutable_effect()->set_str(7);
  three->mutable_effect()->set_attack(5);
  EquipSetTier* four = set.add_tiers();
  four->set_pieces(4);
  four->mutable_effect()->set_attack(9);
  four->mutable_effect()->set_max_hp_pct(0.20);
  // The fifth slot names a family rather than an item: a weapon belongs to one
  // class, so the set cannot say which one.
  EquipSetMember* weapon = set.add_members();
  weapon->set_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  weapon->set_family("Frozen Weapon");
  EquipSetTier* five = set.add_tiers();
  five->set_pieces(5);
  five->mutable_effect()->set_attack(11);
  return {{"frozen", set}};
}

// Wears the `index`-th piece of the set, in the slot that piece belongs to.
void WearFrozenPiece(CharacterInstance& character, int index) {
  const EquipSlot kSlots[] = {EQUIP_SLOT_TOP, EQUIP_SLOT_BOTTOM, EQUIP_SLOT_HAT,
                              EQUIP_SLOT_CAPE};
  const char* kNames[] = {"Frozen Top", "Frozen Bottom", "Frozen Hat",
                          "Frozen Cape"};
  EquipPrototype piece;
  piece.set_name(kNames[index]);
  piece.set_equip_slot(kSlots[index]);
  character.PickUp(std::make_unique<EquipInstance>(piece));
  character.Equip(character.inventory().size() - 1);
}

// Wears the first `count` pieces of the set, each in its own slot.
void WearFrozen(CharacterInstance& character, int count) {
  for (int i = 0; i < count; ++i) {
    WearFrozenPiece(character, i);
  }
}

TEST_F(DerivedStatsTest, ASetPaysNothingUntilItsFirstTier) {
  CharacterInstance c = MakeCharacter(rng_, 15, 1000);
  c.UseEquipSets(FrozenSet());
  WearFrozen(c, 2);

  DerivedStats stats = DerivedStatsFor(c, {});
  EXPECT_EQ(stats.skill_stats.str(), 0);
  EXPECT_EQ(stats.skill_stats.attack(), 0);
  EXPECT_EQ(stats.max_hp, 1000);
}

TEST_F(DerivedStatsTest, TheTiersOfASetAddUp) {
  CharacterInstance c = MakeCharacter(rng_, 15, 1000);
  c.UseEquipSets(FrozenSet());

  WearFrozen(c, 3);
  DerivedStats three = DerivedStatsFor(c, {});
  EXPECT_EQ(three.skill_stats.str(), 7);
  EXPECT_EQ(three.skill_stats.attack(), 5);
  EXPECT_EQ(three.max_hp, 1000) << "the HP tier is not reached yet";

  WearFrozenPiece(c, 3);
  DerivedStats four = DerivedStatsFor(c, {});
  EXPECT_EQ(four.skill_stats.str(), 7) << "the three-piece tier still pays";
  EXPECT_EQ(four.skill_stats.attack(), 14) << "5 and 9 together";
  EXPECT_EQ(four.max_hp, 1200);
}

// Taking a piece off takes the tier with it -- the bonus follows what is worn,
// not what was once worn.
TEST_F(DerivedStatsTest, StrippingAPieceEndsTheTier) {
  CharacterInstance c = MakeCharacter(rng_, 15, 1000);
  c.UseEquipSets(FrozenSet());
  WearFrozen(c, 4);
  ASSERT_EQ(DerivedStatsFor(c, {}).skill_stats.attack(), 14);

  c.Unequip(EQUIP_SLOT_CAPE);
  EXPECT_EQ(DerivedStatsFor(c, {}).skill_stats.attack(), 5);
  c.Unequip(EQUIP_SLOT_HAT);
  EXPECT_EQ(DerivedStatsFor(c, {}).skill_stats.attack(), 0);
}

// Any item of the family fills the slot the set names it by, and an item of no
// family fills nothing -- which is what keeps an ordinary weapon out of a set
// it was never part of.
TEST_F(DerivedStatsTest, AFamilyPieceFillsTheSlotTheSetNamesIt) {
  CharacterInstance c = MakeCharacter(rng_, 15, 1000);
  c.UseEquipSets(FrozenSet());
  WearFrozen(c, 4);
  ASSERT_EQ(DerivedStatsFor(c, {}).skill_stats.attack(), 14);

  EquipPrototype plain;
  plain.set_name("Zedbug");
  plain.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  c.PickUp(std::make_unique<EquipInstance>(plain));
  c.Equip(c.inventory().size() - 1);
  EXPECT_EQ(DerivedStatsFor(c, {}).skill_stats.attack(), 14)
      << "a weapon of no family is not a piece of the set";

  EquipPrototype frozen;
  frozen.set_name("Frozen Polearm");
  frozen.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  frozen.set_set_family("Frozen Weapon");
  c.PickUp(std::make_unique<EquipInstance>(frozen));
  c.Equip(c.inventory().size() - 1);
  EXPECT_EQ(DerivedStatsFor(c, {}).skill_stats.attack(), 25) << "5, 9 and 11";
}

// A slot a boss later drops an alternate for names both, and either fills it.
// Never twice over: they share a slot, so only one can be on at a time.
TEST_F(DerivedStatsTest, AnAlternateFillsTheSlotItShares) {
  std::map<std::string, EquipSet> sets = FrozenSet();
  sets.at("frozen").mutable_members(2)->mutable_items()->add_name(
      "Frozen Tiara");
  CharacterInstance c = MakeCharacter(rng_, 15, 1000);
  c.UseEquipSets(sets);
  WearFrozen(c, 2);

  EquipPrototype tiara;
  tiara.set_name("Frozen Tiara");
  tiara.set_equip_slot(EQUIP_SLOT_HAT);
  c.PickUp(std::make_unique<EquipInstance>(tiara));
  c.Equip(c.inventory().size() - 1);
  EXPECT_EQ(DerivedStatsFor(c, {}).skill_stats.attack(), 5)
      << "the alternate fills the hat slot the set names by two items";

  c.Unequip(EQUIP_SLOT_HAT);
  EXPECT_EQ(DerivedStatsFor(c, {}).skill_stats.attack(), 0);
}

// Gear outside the set is gear outside the set, however much of it is worn.
TEST_F(DerivedStatsTest, OtherGearDoesNotCountTowardASet) {
  CharacterInstance c = MakeCharacter(rng_, 15, 1000);
  c.UseEquipSets(FrozenSet());
  WearFrozen(c, 2);
  EquipArmor(c, 100, 30);

  EXPECT_EQ(DerivedStatsFor(c, {}).skill_stats.str(), 0);
}

// A character who was never told about the sets earns nothing from them, which
// is what every test and sim that does not care about them relies on.
TEST_F(DerivedStatsTest, NoCatalogNoBonus) {
  CharacterInstance c = MakeCharacter(rng_, 15, 1000);
  WearFrozen(c, 4);

  EXPECT_EQ(DerivedStatsFor(c, {}).skill_stats.attack(), 0);
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

TEST_F(DerivedStatsTest, AWornPercentageLiftsBothPools) {
  CharacterInstance c = MakeCharacter(rng_, 15, 100);
  EquipPrototype pendant;
  pendant.set_name("Dominator Pendant");
  pendant.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  pendant.mutable_base_stats()->set_max_hp(200);
  pendant.mutable_base_stats()->set_max_mp(100);
  pendant.mutable_base_stats()->set_max_hp_pct(10);
  pendant.mutable_base_stats()->set_max_mp_pct(10);
  c.PickUp(std::make_unique<EquipInstance>(pendant));
  c.Equip(0);

  std::map<std::string, Skill> skills;
  DerivedStats stats = DerivedStatsFor(c, skills);
  // Over the flat HP the pendant also grants, not just the pool underneath it.
  EXPECT_EQ(stats.max_hp, 330);
  EXPECT_EQ(stats.max_mp, 110);
}

// Both shares land on the one pile rather than compounding: 20% over 100 is
// 120, not 121.
TEST_F(DerivedStatsTest, AWornPercentageSumsWithAPassivesRatherThanStacking) {
  CharacterInstance c = MakeCharacter(rng_, 15, 100);
  Skill iron_body = IronBody();
  std::map<std::string, Skill> skills = {{"iron_body", iron_body}};
  ASSERT_TRUE(c.LearnSkill(iron_body, 10));
  EquipPrototype pendant;
  pendant.set_name("Dominator Pendant");
  pendant.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  pendant.mutable_base_stats()->set_max_hp_pct(10);
  c.PickUp(std::make_unique<EquipInstance>(pendant));
  c.Equip(0);

  EXPECT_EQ(DerivedStatsFor(c, skills).max_hp, 120);
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

// Freezing Crush, which is the whole mechanism: a cap, and what one stack of
// it is worth.
Skill FreezingCrush() {
  Skill skill;
  skill.set_name("Freezing Crush");
  skill.set_kind(SKILL_KIND_PASSIVE);
  skill.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  skill.set_max_level(1);
  skill.set_freeze_stack_cap(5);
  skill.mutable_base()->set_crit_dmg_per_freeze_stack(0.01);
  return skill;
}

// Glacial Fury: a buff that deepens somebody else's pile and pays for it.
Skill GlacialFury() {
  Skill skill;
  skill.set_name("Glacial Fury");
  skill.set_kind(SKILL_KIND_ACTIVE);
  skill.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  skill.set_max_level(1);
  skill.mutable_buff()->set_duration_seconds(20.0);
  skill.mutable_buff()->mutable_base()->set_freeze_stack_cap_bonus(8);
  skill.mutable_buff()->mutable_base()->set_magic_attack_per_freeze_stack(5);
  return skill;
}

TEST_F(DerivedStatsTest, GlacialFuryDeepensThePileWhileItStands) {
  CharacterInstance c = MakeCharacter(rng_, 15, 0);
  Skill crush = FreezingCrush();
  Skill fury = GlacialFury();
  std::map<std::string, Skill> skills = {{"freezing_crush", crush},
                                         {"glacial_fury", fury}};
  ASSERT_TRUE(c.LearnSkill(crush, 1));
  ASSERT_TRUE(c.LearnSkill(fury, 1));

  DerivedStats down = DerivedStatsFor(c, skills);
  EXPECT_EQ(down.freeze.cap, 5);
  EXPECT_EQ(down.freeze.matt_per_stack, 0);

  const Skill* up[] = {&fury};
  DerivedStats standing = DerivedStatsFor(c, skills, absl::MakeConstSpan(up));
  EXPECT_EQ(standing.freeze.cap, 13);
  EXPECT_EQ(standing.freeze.matt_per_stack, 5);
}

// The buff deepens a pile; it does not hand one over. A character who never
// learned Freezing Crush holds no stacks for it to raise or to pay for.
TEST_F(DerivedStatsTest, TheCapBonusGrantsNothingWithoutACap) {
  CharacterInstance c = MakeCharacter(rng_, 15, 0);
  Skill fury = GlacialFury();
  std::map<std::string, Skill> skills = {{"glacial_fury", fury}};
  ASSERT_TRUE(c.LearnSkill(fury, 1));

  const Skill* up[] = {&fury};
  DerivedStats stats = DerivedStatsFor(c, skills, absl::MakeConstSpan(up));
  EXPECT_EQ(stats.freeze.cap, 0);
  EXPECT_EQ(stats.freeze.matt_per_stack, 0);
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

// Evasion Boost's shape: 12% dodge at level 1 climbing 2 points a level.
Skill EvasionBoost() {
  Skill skill;
  skill.set_name("Evasion Boost");
  skill.set_kind(SKILL_KIND_PASSIVE);
  skill.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  skill.set_max_level(10);
  skill.mutable_base()->set_dodge_chance(0.12);
  skill.mutable_per_level()->set_dodge_chance(0.02);
  return skill;
}

TEST_F(DerivedStatsTest, DodgeClimbsWithItsLevelAndLeavesReductionAlone) {
  CharacterInstance c = MakeCharacter(rng_, 15, 100);
  Skill boost = EvasionBoost();
  std::map<std::string, Skill> skills = {{"evasion_boost", boost}};
  ASSERT_TRUE(c.LearnSkill(boost, 10));

  // Dodging is not reduction: it cancels whole hits rather than a share of
  // one, and the two reach the fight down separate wires.
  DerivedStats stats = DerivedStatsFor(c, skills);
  EXPECT_NEAR(stats.dodge_chance, 0.30, 1e-9);
  EXPECT_DOUBLE_EQ(stats.damage_taken_pct, 0.0);
}

TEST_F(DerivedStatsTest, TwoDodgesLeaveTheProductStanding) {
  CharacterInstance c = MakeCharacter(rng_, 15, 100);
  Skill first = EvasionBoost();
  Skill second = EvasionBoost();
  second.set_name("Nimble Feet");
  std::map<std::string, Skill> skills = {{"evasion_boost", first},
                                         {"nimble_feet", second}};
  ASSERT_TRUE(c.LearnSkill(first, 10));
  ASSERT_TRUE(c.LearnSkill(second, 10));

  // 30% and 30% summed would be 60%; what actually gets through is 0.7 * 0.7.
  DerivedStats stats = DerivedStatsFor(c, skills);
  EXPECT_NEAR(stats.dodge_chance, 1.0 - 0.70 * 0.70, 1e-9);
}

// Frailty Curse's shape: 11% off the monster's attack at level 1, climbing a
// point a level.
Skill Barrier() {
  Skill skill;
  skill.set_name("Frailty Curse");
  skill.set_kind(SKILL_KIND_PASSIVE);
  skill.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  skill.set_max_level(20);
  skill.mutable_base()->set_enemy_attack_pct(0.11);
  skill.mutable_per_level()->set_enemy_attack_pct(0.01);
  return skill;
}

TEST_F(DerivedStatsTest, TwoBarriersSumAndStopAtBossesWithoutOneToOpenThem) {
  CharacterInstance c = MakeCharacter(rng_, 15, 100);
  Skill curse = Barrier();
  Skill enhance = Barrier();
  enhance.set_name("Frailty Curse - Enhance");
  enhance.set_max_level(1);
  enhance.mutable_base()->set_enemy_attack_pct(0.10);
  enhance.clear_per_level();
  std::map<std::string, Skill> skills = {{"frailty_curse", curse},
                                         {"frailty_curse_enhance", enhance}};
  ASSERT_TRUE(c.LearnSkill(curse, 20));
  ASSERT_TRUE(c.LearnSkill(enhance, 1));

  // Points on one number, so they sum where reduction and dodging combine.
  DerivedStats stats = DerivedStatsFor(c, skills);
  EXPECT_NEAR(stats.enemy_attack_pct, 0.40, 1e-9);
  EXPECT_FALSE(stats.enemy_attack_reaches_boss);
}

TEST_F(DerivedStatsTest, OneSkillOpensTheWholeBarrierOntoBosses) {
  CharacterInstance c = MakeCharacter(rng_, 15, 100);
  Skill curse = Barrier();
  Skill rush = Barrier();
  rush.set_name("Frailty Curse - Boss Rush");
  rush.set_max_level(1);
  rush.clear_base();
  rush.clear_per_level();
  rush.mutable_base()->set_enemy_attack_reaches_boss(true);
  std::map<std::string, Skill> skills = {{"frailty_curse", curse},
                                         {"frailty_curse_boss_rush", rush}};
  ASSERT_TRUE(c.LearnSkill(curse, 20));
  ASSERT_TRUE(c.LearnSkill(rush, 1));

  DerivedStats stats = DerivedStatsFor(c, skills);
  EXPECT_NEAR(stats.enemy_attack_pct, 0.30, 1e-9);
  EXPECT_TRUE(stats.enemy_attack_reaches_boss);
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
  Skill blessing = AdvancedBlessing();
  std::map<std::string, Skill> skills = {{"iron_body", iron_body},
                                         {"warrior_mastery", mastery},
                                         {"advanced_blessing", blessing}};
  ASSERT_TRUE(c.LearnSkill(iron_body, 10));
  ASSERT_TRUE(c.LearnSkill(mastery, 1));
  ASSERT_TRUE(c.LearnSkill(blessing, 10));

  // Flat first: 50 allocated + 100 equipped + 6 * 15 per-level + a flat 750,
  // then Iron Body's +10% on the whole pile. Applying the percent to any one
  // source alone would land short.
  DerivedStats stats = DerivedStatsFor(c, skills);
  EXPECT_EQ(stats.max_hp, 1089);
  // The MP half of the same grant, with no percentage over it.
  EXPECT_EQ(stats.max_mp, 750);
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

// The chance and the damage stay apart all the way through, because the fight
// rolls the one and pays the other.
TEST_F(DerivedStatsTest, FinalAttackKeepsItsChanceAndItsDamageApart) {
  CharacterInstance c = MakeCharacter(rng_, 40, 50);
  Skill final_attack = FinalAttack();
  std::map<std::string, Skill> skills = {{"final_attack", final_attack}};
  ASSERT_TRUE(c.LearnSkill(final_attack, 20));

  // 40% of an extra hit worth 160%. It names no tag, so every swing sets it
  // off -- which is what a Final Attack gated on the weapon in hand wants.
  DerivedStats stats = DerivedStatsFor(c, skills);
  ASSERT_EQ(stats.final_attacks.size(), 1u);
  EXPECT_NEAR(stats.final_attacks[0].chance, 0.40, 1e-9);
  EXPECT_NEAR(stats.final_attacks[0].damage_pct, 1.60, 1e-9);
  EXPECT_EQ(stats.final_attacks[0].required_tag, SKILL_TAG_UNSPECIFIED);
  // A source that says nothing about its strikes lands one, which is every
  // source but the rogue's two marks.
  EXPECT_EQ(stats.final_attacks[0].lines, 1);

  // Three stars for 160% apiece, where the line above is one for 160%. The
  // damage stays per strike -- the count is what changed.
  final_attack.mutable_base()->set_final_attack_lines(3);
  skills["final_attack"] = final_attack;
  stats = DerivedStatsFor(c, skills);
  ASSERT_EQ(stats.final_attacks.size(), 1u);
  EXPECT_NEAR(stats.final_attacks[0].damage_pct, 1.60, 1e-9);
  EXPECT_EQ(stats.final_attacks[0].lines, 3);
}

// A boost aimed at a passive reaches nothing it swings, because it swings
// nothing -- but a passive carrying a Final Attack has that extra hit to
// strengthen, and the Hero's two hypers aim at exactly it.
TEST_F(DerivedStatsTest, ABoostReachesTheFinalAttackOfThePassiveItNames) {
  CharacterInstance c = MakeCharacter(rng_, 140, 50);
  Skill final_attack = FinalAttack();
  Skill hyper;
  hyper.set_name("Advanced Final Attack - Opportunity");
  hyper.set_kind(SKILL_KIND_PASSIVE);
  hyper.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  hyper.set_max_level(1);
  SkillBoost* boost = hyper.add_boost();
  boost->set_skill_name("Final Attack");
  boost->mutable_effect()->set_final_attack_chance(0.15);
  boost->mutable_effect()->set_damage_pct(0.10);
  std::map<std::string, Skill> skills = {{"final_attack", final_attack},
                                         {"hyper", hyper}};
  ASSERT_TRUE(c.LearnSkill(final_attack, 20));
  ASSERT_TRUE(c.LearnSkill(hyper, 1));

  // 40% walks to 55%, and the extra hits alone collect the 10% damage. The
  // multiplier is untouched: what the boost grants is not the skill's own.
  DerivedStats stats = DerivedStatsFor(c, skills);
  ASSERT_EQ(stats.final_attacks.size(), 1u);
  EXPECT_NEAR(stats.final_attacks[0].chance, 0.55, 1e-9);
  EXPECT_NEAR(stats.final_attacks[0].damage_bonus_pct, 0.10, 1e-9);
  EXPECT_NEAR(stats.final_attacks[0].damage_pct, 1.60, 1e-9);
  EXPECT_NEAR(stats.damage_pct, 0.0, 1e-9);
}

// The other damage a boost can hand a Final Attack: points on the strike's own
// multiplier, which is GMS's "Night Lord's Mark Damage: +100% points".
TEST_F(DerivedStatsTest, ABoostCanLiftAFinalAttacksOwnMultiplier) {
  CharacterInstance c = MakeCharacter(rng_, 140, 50);
  Skill final_attack = FinalAttack();
  Skill hyper;
  hyper.set_name("Death Star");
  hyper.set_kind(SKILL_KIND_PASSIVE);
  hyper.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  hyper.set_max_level(1);
  SkillBoost* boost = hyper.add_boost();
  boost->set_skill_name("Final Attack");
  boost->mutable_effect()->set_skill_pct(1.00);
  std::map<std::string, Skill> skills = {{"final_attack", final_attack},
                                         {"hyper", hyper}};
  ASSERT_TRUE(c.LearnSkill(final_attack, 20));
  ASSERT_TRUE(c.LearnSkill(hyper, 1));

  // 160% becomes 260%, and every strike of the hit lands the whole of it.
  DerivedStats stats = DerivedStatsFor(c, skills);
  ASSERT_EQ(stats.final_attacks.size(), 1u);
  EXPECT_NEAR(stats.final_attacks[0].damage_pct, 2.60, 1e-9);
  EXPECT_NEAR(stats.final_attacks[0].damage_bonus_pct, 0.0, 1e-9);

  // Blizzard's shape: a swing carrying a Final Attack of its own. The same
  // points land on the swing, so the strike must not take them a second time.
  Skill swung = final_attack;
  swung.set_kind(SKILL_KIND_ATTACK);
  swung.mutable_base()->set_skill_pct(2.00);
  skills["final_attack"] = swung;
  EXPECT_NEAR(DerivedStatsFor(c, skills).final_attacks[0].damage_pct, 1.60,
              1e-9);
}

// A lever can be handed over backwards. Hurricane - Split Attack pays a second
// arrow for a quarter off what each one lands, and the fold that combines two
// final damage sources has to carry the minus through rather than clamp it.
TEST_F(DerivedStatsTest, ABoostCanTakeALeverAway) {
  CharacterInstance c = MakeCharacter(rng_, 140, 50);
  Skill hyper;
  hyper.set_name("Hurricane - Split Attack");
  hyper.set_kind(SKILL_KIND_PASSIVE);
  hyper.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  hyper.set_max_level(1);
  SkillBoost* boost = hyper.add_boost();
  boost->set_skill_name("Hurricane");
  boost->set_lines(1);
  boost->mutable_effect()->set_final_dmg_pct(-0.25);
  std::map<std::string, Skill> skills = {{"hyper", hyper}};
  ASSERT_TRUE(c.LearnSkill(hyper, 1));

  DerivedStats stats = DerivedStatsFor(c, skills);
  std::map<std::string, SkillBonus>::const_iterator bonus =
      stats.skill_bonus.find("Hurricane");
  ASSERT_NE(bonus, stats.skill_bonus.end());
  EXPECT_NEAR(bonus->second.final_dmg_pct, -0.25, 1e-9);
}

// A boost naming a skill the character does not hold reaches nothing, and a
// Final Attack nobody named keeps what it was written with.
TEST_F(DerivedStatsTest, AFinalAttackKeepsItsOwnChanceWhenNobodyNamesIt) {
  CharacterInstance c = MakeCharacter(rng_, 140, 50);
  Skill final_attack = FinalAttack();
  Skill hyper;
  hyper.set_name("Aimed Elsewhere");
  hyper.set_kind(SKILL_KIND_PASSIVE);
  hyper.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  hyper.set_max_level(1);
  SkillBoost* boost = hyper.add_boost();
  boost->set_skill_name("Some Other Skill");
  boost->mutable_effect()->set_final_attack_chance(0.15);
  std::map<std::string, Skill> skills = {{"final_attack", final_attack},
                                         {"hyper", hyper}};
  ASSERT_TRUE(c.LearnSkill(final_attack, 20));
  ASSERT_TRUE(c.LearnSkill(hyper, 1));

  DerivedStats stats = DerivedStatsFor(c, skills);
  ASSERT_EQ(stats.final_attacks.size(), 1u);
  EXPECT_NEAR(stats.final_attacks[0].chance, 0.40, 1e-9);
}

// A burn on a PASSIVE follows the character onto every swing; one on the
// attack that leaves it stays with that attack, where the swing is priced.
TEST_F(DerivedStatsTest, OnlyAPassivesBurnFollowsTheCharacter) {
  CharacterInstance c = MakeCharacter(rng_, 40, 50);
  Skill venom;
  venom.set_name("Venom");
  venom.set_kind(SKILL_KIND_PASSIVE);
  venom.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  venom.set_max_level(10);
  venom.mutable_dot()->set_interval_seconds(1.0);
  venom.mutable_dot()->set_duration_seconds(6.0);
  Skill raid = venom;
  raid.set_name("Sudden Raid");
  raid.set_kind(SKILL_KIND_ATTACK);
  raid.set_base_delay_ms(900);
  std::map<std::string, Skill> skills = {{"venom", venom},
                                         {"sudden_raid", raid}};
  ASSERT_TRUE(c.LearnSkill(venom, 7));
  ASSERT_TRUE(c.LearnSkill(raid, 3));

  DerivedStats stats = DerivedStatsFor(c, skills);
  ASSERT_EQ(stats.dots.size(), 1u);
  EXPECT_EQ(stats.dots[0].level, 7);
  EXPECT_DOUBLE_EQ(stats.dots[0].dot.duration_seconds(), 6.0);
}

TEST_F(DerivedStatsTest, NoFinalAttackIsWorthNothing) {
  CharacterInstance c = MakeCharacter(rng_, 40, 50);
  std::map<std::string, Skill> skills = {{"iron_body", IronBody()}};
  EXPECT_TRUE(DerivedStatsFor(c, skills).final_attacks.empty());
}

// Every Final Attack keeps its own entry, the Hunter's matching pair included:
// two of them are two independent rolls, and one merged entry would have to
// settle on a chance and a damage that neither source has.
TEST_F(DerivedStatsTest, EveryFinalAttackKeepsItsOwnEntry) {
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
  ASSERT_EQ(stats.final_attacks.size(), 3u);
  for (int i = 0; i < 3; ++i) {
    EXPECT_NEAR(stats.final_attacks[i].chance, 0.40, 1e-9);
    EXPECT_NEAR(stats.final_attacks[i].damage_pct, 1.60, 1e-9);
  }
  EXPECT_EQ(stats.final_attacks[0].required_tag, SKILL_TAG_UNSPECIFIED);
  EXPECT_EQ(stats.final_attacks[1].required_tag, SKILL_TAG_FIRE);
  EXPECT_EQ(stats.final_attacks[2].required_tag, SKILL_TAG_UNSPECIFIED);
}

// Advanced Final Attack's shape: it states the WHOLE of the Final Attack it
// replaces rather than a delta, so the pair must never both pay.
Skill AdvancedFinalAttack() {
  Skill skill;
  skill.set_name("Advanced Final Attack");
  skill.set_kind(SKILL_KIND_PASSIVE);
  skill.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  skill.set_max_level(30);
  skill.set_supersedes_skill_name("Final Attack");
  skill.mutable_base()->set_final_attack_chance(0.60);
  skill.mutable_base()->set_final_attack_pct(5.10);
  return skill;
}

TEST_F(DerivedStatsTest, AnAdvancedSkillStopsTheOneItSupersedes) {
  CharacterInstance c = MakeCharacter(rng_, 100, 50);
  Skill final_attack = FinalAttack();
  Skill advanced = AdvancedFinalAttack();
  std::map<std::string, Skill> skills = {{"final_attack", final_attack},
                                         {"advanced", advanced}};
  ASSERT_TRUE(c.LearnSkill(final_attack, 20));
  ASSERT_TRUE(c.LearnSkill(advanced, 1));

  // 60% of 5.10 and nothing else: the Fighter's own is gone rather than
  // joined by a second roll.
  DerivedStats stats = DerivedStatsFor(c, skills);
  ASSERT_EQ(stats.final_attacks.size(), 1u);
  EXPECT_NEAR(stats.final_attacks[0].chance, 0.60, 1e-9);
  EXPECT_NEAR(stats.final_attacks[0].damage_pct, 5.10, 1e-9);
}

// Three ways a supersede does not take, each the same rule: a skill granting
// nothing replaces nothing. Unlearned, another branch's book, or the gear it
// demands missing -- and the skill it names goes on paying.
TEST_F(DerivedStatsTest, ASkillGrantingNothingSupersedesNothing) {
  Skill final_attack = FinalAttack();
  Skill advanced = AdvancedFinalAttack();
  advanced.add_required_equip_type(EQUIP_TYPE_TWO_HANDED_SWORD);

  for (int which = 0; which < 3; ++which) {
    CharacterInstance c = MakeCharacter(rng_, 100, 50);
    EquipWeapon(c,
                which == 2 ? EQUIP_TYPE_POLEARM : EQUIP_TYPE_TWO_HANDED_SWORD);
    ASSERT_TRUE(c.LearnSkill(final_attack, 20));
    if (which != 0) {
      ASSERT_TRUE(c.LearnSkill(advanced, 1));
    }
    // Learned under this character's own book, then handed to the catalog as
    // another branch's -- the only way to hold a level in a book you do not
    // have, and what a shared display name really does.
    Skill theirs = advanced;
    if (which == 1) {
      theirs.set_job_advancement(JOB_ADVANCEMENT_FIGHTER);
    }
    std::map<std::string, Skill> skills = {{"final_attack", final_attack},
                                           {"advanced", theirs}};

    // Summed rather than read off one source, so a failing case reports rather
    // than aborting the two behind it -- and so 3.70 (both paid) and 3.06 (the
    // wrong one paid) are told apart from the 0.64 that is right.
    double total = 0.0;
    for (const FinalAttackSource& source :
         DerivedStatsFor(c, skills).final_attacks) {
      total += source.chance * source.damage_pct;
    }
    EXPECT_NEAR(total, 0.64, 1e-9) << "case " << which;
  }
}

// --- Exclusive groups ---

// The Sharp Eyes pair, which is what a group is for: a Decent one that beats
// three points spent in the real skill and loses to a maxed one, and a stat it
// pays beside them that no member of the group competes for.
Skill SharpEyes() {
  Skill skill;
  skill.set_name("Sharp Eyes");
  skill.set_kind(SKILL_KIND_PASSIVE);
  skill.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  skill.set_max_level(20);
  skill.set_exclusive_group("Sharp Eyes");
  skill.mutable_base()->set_crit_rate(0.01);
  skill.mutable_per_level()->set_crit_rate(0.01);
  *skill.mutable_ally_base() = skill.base();
  *skill.mutable_ally_per_level() = skill.per_level();
  return skill;
}

Skill DecentSharpEyes() {
  Skill skill;
  skill.set_name("Decent Sharp Eyes");
  skill.set_kind(SKILL_KIND_PASSIVE);
  skill.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  skill.set_max_level(30);
  skill.set_exclusive_group("Sharp Eyes");
  skill.mutable_base()->set_crit_rate(0.10);
  skill.mutable_base()->set_luk(6);
  return skill;
}

TEST_F(DerivedStatsTest, AGroupPaysItsBestSourceOfEachLever) {
  Skill sharp = SharpEyes();
  Skill decent = DecentSharpEyes();
  std::map<std::string, Skill> skills = {{"sharp_eyes", sharp},
                                         {"decent", decent}};
  CharacterInstance c = MakeCharacter(rng_, 100, 0);
  ASSERT_TRUE(c.LearnSkill(decent, 1));
  ASSERT_TRUE(c.LearnSkill(sharp, 3));

  // The Decent's, the three points in the real skill paying nothing -- and its
  // LUK stands whichever of the two wins, the group holding no other source
  // of it.
  DerivedStats stats = DerivedStatsFor(c, skills);
  EXPECT_NEAR(stats.crit_rate, 0.10, 1e-9);
  EXPECT_EQ(stats.skill_stats.luk(), 6);

  ASSERT_TRUE(c.LearnSkill(sharp, 17));
  stats = DerivedStatsFor(c, skills);
  EXPECT_NEAR(stats.crit_rate, 0.20, 1e-9);
  EXPECT_EQ(stats.skill_stats.luk(), 6);
}

// Two members paying the same amount are one payment, not two -- and a skill
// in no group goes on summing with everything, which is every other skill in
// the catalog.
TEST_F(DerivedStatsTest, ATiedGroupPaysOnceAndTheUngroupedStillSum) {
  Skill sharp = SharpEyes();
  Skill decent = DecentSharpEyes();
  decent.mutable_base()->set_crit_rate(0.20);
  Skill loose = DecentSharpEyes();
  loose.set_name("Critical Shot");
  loose.clear_exclusive_group();
  loose.mutable_base()->set_crit_rate(0.05);
  loose.mutable_base()->clear_luk();
  std::map<std::string, Skill> skills = {
      {"sharp_eyes", sharp}, {"decent", decent}, {"loose", loose}};
  CharacterInstance c = MakeCharacter(rng_, 100, 0);
  ASSERT_TRUE(c.LearnSkill(sharp, 20));
  ASSERT_TRUE(c.LearnSkill(decent, 1));
  ASSERT_TRUE(c.LearnSkill(loose, 1));

  EXPECT_NEAR(DerivedStatsFor(c, skills).crit_rate, 0.25, 1e-9);
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
TEST_F(DerivedStatsTest, DefPercentLeavesBaseDefAlone) {
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

TEST_F(DerivedStatsTest, TwoDefPercentsMultiplyRatherThanSum) {
  CharacterInstance c = MakeStatCharacter(rng_, 100, 0, 0, 0);
  Skill phoenix = IronBody();
  phoenix.mutable_base()->clear_def();
  phoenix.mutable_per_level()->clear_def();
  phoenix.mutable_base()->set_def_pct(0.30);
  phoenix.mutable_per_level()->clear_def_pct();
  Skill reckless = phoenix;
  reckless.set_name("Reckless Hunt");
  reckless.mutable_base()->set_def_pct(-0.25);
  std::map<std::string, Skill> skills = {{"phoenix", phoenix},
                                         {"reckless", reckless}};
  ASSERT_TRUE(c.LearnSkill(phoenix, 1));
  ASSERT_TRUE(c.LearnSkill(reckless, 1));

  // Summed the pair would be +5% and leave 157 DEF. Multiplied they leave
  // 1.30 * 0.75 of the 150 the character's STR bought.
  EXPECT_EQ(DerivedStatsFor(c, skills).def, 146);
}

TEST_F(DerivedStatsTest, ADefPercentCanTakeDefenceAway) {
  CharacterInstance c = MakeStatCharacter(rng_, 100, 0, 0, 0);
  Skill reckless = IronBody();
  reckless.mutable_base()->clear_def();
  reckless.mutable_per_level()->clear_def();
  reckless.set_max_level(10);
  reckless.mutable_base()->set_def_pct(-0.07);
  reckless.mutable_per_level()->set_def_pct(-0.02);
  std::map<std::string, Skill> skills = {{"reckless", reckless}};
  ASSERT_TRUE(c.LearnSkill(reckless, 10));

  // Reckless Hunt's whole bargain: a quarter of the armour given up. The base
  // the percentage is charged against is untouched.
  DerivedStats derived = DerivedStatsFor(c, skills);
  EXPECT_EQ(derived.base_def, 150);
  EXPECT_EQ(derived.def, 112);
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

// The 4th job's rule: a skill marked for it takes granted levels PAST its
// master level, two of them, which is where the levels its own page never
// describes live. They are worth what the ladder says, like any other level.
TEST_F(DerivedStatsTest, BonusLevelsCarryAMarkedSkillPastItsMasterLevel) {
  CharacterInstance c = MakeCharacter(rng_, 100, 0);
  Skill iron_body = IronBody();
  iron_body.set_exceeds_master_level(true);
  Skill orders = CombatOrders();
  std::map<std::string, Skill> skills = {{"iron_body", iron_body},
                                         {"combat_orders", orders}};
  ASSERT_TRUE(c.LearnSkill(iron_body, 20));  // its master level, bought out
  ASSERT_TRUE(c.LearnSkill(orders, 10));     // two levels to hand out

  int bonus = BonusSkillLevels(c, skills);
  ASSERT_EQ(bonus, 2);
  EXPECT_EQ(EffectiveSkillLevel(c, iron_body, bonus), 22);
  EXPECT_EQ(DerivedStatsFor(c, skills).def, 220);
  EXPECT_EQ(EffectiveSkillLevel(c, iron_body, 5), 22)
      << "two past the master level however many are going";
}

// The bonus levels a group hands out are one grant too, read where every other
// skill reads its level rather than through the passive fold.
TEST_F(DerivedStatsTest, BonusLevelsDoNotStackInsideAGroup) {
  Skill orders = CombatOrders();
  orders.set_exclusive_group("Combat Orders");
  Skill decent = CombatOrders();
  decent.set_name("Decent Combat Orders");
  decent.set_exclusive_group("Combat Orders");
  decent.set_max_level(30);
  decent.clear_per_level();
  std::map<std::string, Skill> skills = {{"combat_orders", orders},
                                         {"decent", decent}};
  CharacterInstance c = MakeCharacter(rng_, 100, 0);
  ASSERT_TRUE(c.LearnSkill(decent, 30));
  EXPECT_EQ(BonusSkillLevels(c, skills), 1);

  ASSERT_TRUE(c.LearnSkill(orders, 10));
  EXPECT_EQ(BonusSkillLevels(c, skills), 2) << "the White Knight's alone";
}

// The same four rules read off a bare level, which is what the skill page
// holds when it asks what one more point would buy.
TEST_F(DerivedStatsTest, LevelWithBonusNeedsNoCharacter) {
  Skill iron_body = IronBody();
  Skill marked = IronBody();
  marked.set_exceeds_master_level(true);

  EXPECT_EQ(LevelWithBonus(iron_body, 5, 2), 7);
  EXPECT_EQ(LevelWithBonus(iron_body, 0, 2), 0) << "unlearned stays unlearned";
  EXPECT_EQ(LevelWithBonus(iron_body, 19, 2), 20) << "held to the master level";
  EXPECT_EQ(LevelWithBonus(marked, 19, 2), 21) << "two past it when marked";
  EXPECT_EQ(LevelWithBonus(CombatOrders(), 5, 2), 5)
      << "the skill handing out the levels does not take them";
}

// Two rules the bonus has to hold at once for a skill NOT marked for the 4th
// job's: it never carries the skill past its master level, and it never raises
// the skill handing it out.
TEST_F(DerivedStatsTest, BonusLevelsStopAtTheTopAndSkipTheirOwn) {
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

// GMS hangs permanent grants off active skills and marks them "[Passive
// Effects: ...]" -- Phoenix is a summon that also raises DEF for good. So the
// kind decides what the skill does in a fight, not whether its levers are read.
TEST_F(DerivedStatsTest, AnAttackSkillsPermanentGrantsStillLand) {
  CharacterInstance c = MakeCharacter(rng_, 15, 50);
  Skill phoenix;
  phoenix.set_name("Phoenix");
  phoenix.set_kind(SKILL_KIND_AUTO_ATTACK);
  phoenix.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  phoenix.set_max_level(10);
  phoenix.mutable_base()->set_skill_pct(2.28);  // what it hits for
  phoenix.mutable_base()->set_def(30);          // and what it grants for good
  phoenix.mutable_base()->set_max_hp_pct(0.10);
  std::map<std::string, Skill> skills = {{"phoenix", phoenix}};
  ASSERT_TRUE(c.LearnSkill(phoenix, 1));

  DerivedStats stats = DerivedStatsFor(c, skills);
  EXPECT_EQ(stats.max_hp, 55);
  EXPECT_EQ(stats.def, 30);
  // Its damage is the fight's business and stays out of the stat line.
  EXPECT_DOUBLE_EQ(stats.damage_pct, 0.0);
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

// Advanced Combo's whole trick: it widens the ring the Fighter's Combo Attack
// prices, without replacing the skill doing the pricing. So the ATT per orb
// goes on being paid, against more orbs than the skill granting it names.
TEST_F(DerivedStatsTest, AWiderRingIsStillPricedByTheSkillThatNamedIt) {
  CharacterInstance c = MakeCharacter(rng_, 100, 0);
  Skill combo;
  combo.set_name("Combo Attack");
  combo.set_kind(SKILL_KIND_PASSIVE);
  combo.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  combo.set_max_level(1);
  combo.set_combo_orbs(5);
  combo.mutable_base()->set_attack_per_combo_orb(2);
  Skill advanced;
  advanced.set_name("Advanced Combo");
  advanced.set_kind(SKILL_KIND_PASSIVE);
  advanced.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  advanced.set_max_level(20);
  advanced.set_combo_orbs(5);
  advanced.set_combo_orbs_per_level(0.26316);
  std::map<std::string, Skill> skills = {{"combo_attack", combo},
                                         {"advanced_combo", advanced}};
  ASSERT_TRUE(c.LearnSkill(combo, 1));

  // Five orbs at 2 ATT apiece, before Advanced Combo is opened at all.
  EXPECT_EQ(DerivedStatsFor(c, skills).skill_stats.attack(), 10);
  ASSERT_TRUE(c.LearnSkill(advanced, 20));
  EXPECT_EQ(DerivedStatsFor(c, skills).skill_stats.attack(), 20);
}

// The Hero's two: boss damage and DEF, each worth the ring times the bargain.
TEST_F(DerivedStatsTest, BossDamageAndDefArePricedPerOrb) {
  CharacterInstance c = MakeCharacter(rng_, 140, 0);
  Skill combo;
  combo.set_name("Advanced Combo");
  combo.set_kind(SKILL_KIND_PASSIVE);
  combo.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  combo.set_max_level(1);
  combo.set_combo_orbs(10);
  Skill rush;
  rush.set_name("Boss Rush");
  rush.set_kind(SKILL_KIND_PASSIVE);
  rush.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  rush.set_max_level(1);
  rush.mutable_base()->set_boss_pct_per_combo_orb(0.02);
  rush.mutable_base()->set_def_per_combo_orb(100);
  std::map<std::string, Skill> skills = {{"advanced_combo", combo},
                                         {"boss_rush", rush}};
  ASSERT_TRUE(c.LearnSkill(combo, 1));
  ASSERT_TRUE(c.LearnSkill(rush, 1));

  DerivedStats stats = DerivedStatsFor(c, skills);
  EXPECT_NEAR(stats.boss_pct, 0.20, 1e-9);
  EXPECT_EQ(stats.skill_stats.def(), 1000);
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
  stats.ied = 0.25;
  PassiveOffense passives = PassiveOffenseFor(stats);
  EXPECT_DOUBLE_EQ(passives.damage_pct, 0.15);
  EXPECT_DOUBLE_EQ(passives.final_dmg_pct, 0.25);
  EXPECT_DOUBLE_EQ(passives.crit_dmg, 0.05);
  EXPECT_DOUBLE_EQ(passives.ied, 0.25);
}

// Marksmanship's shape: 6% of the monster's DEF ignored at level 1, climbing a
// point a level.
Skill Marksmanship() {
  Skill skill;
  skill.set_name("Marksmanship");
  skill.set_kind(SKILL_KIND_PASSIVE);
  skill.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  skill.set_max_level(20);
  skill.mutable_base()->set_ied_pct(0.06);
  skill.mutable_per_level()->set_ied_pct(0.01);
  return skill;
}

// Marksmanship's other half: attack raised by a percentage rather than a
// count, climbing a point a level from 6%.
Skill AttackPercent() {
  Skill skill;
  skill.set_name("Marksmanship");
  skill.set_kind(SKILL_KIND_PASSIVE);
  skill.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  skill.set_max_level(20);
  skill.mutable_base()->set_attack_pct(0.06);
  skill.mutable_per_level()->set_attack_pct(0.01);
  return skill;
}

TEST_F(DerivedStatsTest, AttackPercentScalesWornAndGrantedAlike) {
  CharacterInstance c = MakeCharacter(rng_, 15, 100);
  Skill marks = AttackPercent();
  Skill grant;
  grant.set_name("Soul Arrow");
  grant.set_kind(SKILL_KIND_PASSIVE);
  grant.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  grant.set_max_level(1);
  grant.mutable_base()->set_attack(20);
  std::map<std::string, Skill> skills = {{"marksmanship", marks},
                                         {"soul_arrow", grant}};
  ASSERT_TRUE(c.LearnSkill(marks, 20));
  ASSERT_TRUE(c.LearnSkill(grant, 1));

  EquipPrototype bow;
  bow.set_name("Bow");
  bow.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  bow.mutable_base_stats()->set_attack(80);
  c.PickUp(std::make_unique<EquipInstance>(bow));
  c.Equip(0);

  // 80 worn and 20 granted make 100, and the 25% lands over the pair of them
  // rather than over either alone.
  DerivedStats stats = DerivedStatsFor(c, skills);
  EXPECT_NEAR(stats.attack_pct, 0.25, 1e-9);
  EXPECT_EQ(TotalEquipStats(c, stats).attack(), 125);
}

TEST_F(DerivedStatsTest, AttackPercentScalesMagicAttackToo) {
  CharacterInstance c = MakeCharacter(rng_, 15, 100);
  Skill marks = AttackPercent();
  std::map<std::string, Skill> skills = {{"marksmanship", marks}};
  ASSERT_TRUE(c.LearnSkill(marks, 20));

  EquipPrototype staff;
  staff.set_name("Staff");
  staff.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  staff.mutable_base_stats()->set_magic_attack(80);
  c.PickUp(std::make_unique<EquipInstance>(staff));
  c.Equip(0);

  // A magician swings on magic attack, so a percentage of what you swing on
  // has to reach it too.
  EXPECT_EQ(TotalEquipStats(c, DerivedStatsFor(c, skills)).magic_attack(), 100);
}

// Speed Mirage's passive half: a skill that makes ONE other skill hit harder,
// named rather than tagged.
Skill SpeedMirage() {
  Skill skill;
  skill.set_name("Speed Mirage");
  skill.set_kind(SKILL_KIND_PASSIVE);
  skill.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  skill.set_max_level(20);
  SkillBoost* boost = skill.add_boost();
  boost->set_skill_name("Wind Arrow");
  boost->mutable_effect()->set_skill_pct(0.51);
  boost->mutable_effect_per_level()->set_skill_pct(0.01);
  return skill;
}

TEST_F(DerivedStatsTest, ABoostReachesOnlyTheSkillItNames) {
  CharacterInstance c = MakeCharacter(rng_, 15, 100);
  Skill mirage = SpeedMirage();
  std::map<std::string, Skill> skills = {{"speed_mirage", mirage}};
  ASSERT_TRUE(c.LearnSkill(mirage, 20));

  DerivedStats stats = DerivedStatsFor(c, skills);
  ASSERT_EQ(stats.skill_bonus.size(), 1u);
  EXPECT_NEAR(stats.skill_bonus.at("Wind Arrow").skill_pct, 0.70, 1e-9);
  EXPECT_EQ(stats.skill_bonus.count("Piercing Arrow"), 0u);
  // It is not plain damage: everything else the character swings is untouched.
  EXPECT_DOUBLE_EQ(stats.damage_pct, 0.0);
}

// The other damage a boost can grant: a share of the character's own, which
// only the named skill collects. See SkillBoost::effect.
TEST_F(DerivedStatsTest, ABoostsPlainDamageStaysWithItsSkill) {
  CharacterInstance c = MakeCharacter(rng_, 15, 100);
  Skill reinforce = SpeedMirage();
  reinforce.set_name("Wind Arrow - Reinforce");
  reinforce.set_max_level(1);
  SkillBoost* boost = reinforce.mutable_boost(0);
  boost->clear_effect_per_level();
  boost->mutable_effect()->clear_skill_pct();
  boost->mutable_effect()->set_damage_pct(1.50);
  std::map<std::string, Skill> skills = {{"reinforce", reinforce}};
  ASSERT_TRUE(c.LearnSkill(reinforce, 1));

  DerivedStats stats = DerivedStatsFor(c, skills);
  EXPECT_NEAR(stats.skill_bonus.at("Wind Arrow").damage_pct, 1.50, 1e-9);
  EXPECT_DOUBLE_EQ(stats.skill_bonus.at("Wind Arrow").skill_pct, 0.0);
  EXPECT_DOUBLE_EQ(stats.damage_pct, 0.0);
}

TEST_F(DerivedStatsTest, TwoBoostsOnOneSkillSum) {
  CharacterInstance c = MakeCharacter(rng_, 15, 100);
  Skill first = SpeedMirage();
  Skill second = SpeedMirage();
  second.set_name("Silhouette Mirage");
  // The lever aimed at the mark the skill leaves sums the same way, and
  // climbs with the granting skill's level as the one above it does.
  for (Skill* skill : {&first, &second}) {
    skill->mutable_boost(0)->set_dot_skill_pct(0.11);
    skill->mutable_boost(0)->set_dot_skill_pct_per_level(0.01);
    skill->mutable_boost(0)->set_dot_duration_seconds(2.0);
  }
  std::map<std::string, Skill> skills = {{"speed_mirage", first},
                                         {"silhouette", second}};
  ASSERT_TRUE(c.LearnSkill(first, 20));
  ASSERT_TRUE(c.LearnSkill(second, 20));

  const SkillBonus& bonus =
      DerivedStatsFor(c, skills).skill_bonus.at("Wind Arrow");
  EXPECT_NEAR(bonus.skill_pct, 1.40, 1e-9);
  EXPECT_NEAR(bonus.dot_skill_pct, 0.60, 1e-9);
  EXPECT_NEAR(bonus.dot_duration_seconds, 4.0, 1e-9);
}

TEST_F(DerivedStatsTest, IgnoredDefenceClimbsWithItsLevel) {
  CharacterInstance c = MakeCharacter(rng_, 15, 100);
  Skill marks = Marksmanship();
  std::map<std::string, Skill> skills = {{"marksmanship", marks}};
  ASSERT_TRUE(c.LearnSkill(marks, 20));

  EXPECT_NEAR(DerivedStatsFor(c, skills).ied, 0.25, 1e-9);
}

TEST_F(DerivedStatsTest, TwoSourcesOfIgnoredDefenceCombineInReverse) {
  CharacterInstance c = MakeCharacter(rng_, 15, 100);
  Skill first = Marksmanship();
  Skill second = Marksmanship();
  second.set_name("Sharp Eyes");
  second.mutable_base()->set_ied_pct(0.40);
  second.clear_per_level();
  std::map<std::string, Skill> skills = {{"marksmanship", first},
                                         {"sharp_eyes", second}};
  ASSERT_TRUE(c.LearnSkill(first, 20));
  ASSERT_TRUE(c.LearnSkill(second, 1));

  // 25% and 40% summed would be 65%; what is left of the armour is 0.75 * 0.60.
  EXPECT_NEAR(DerivedStatsFor(c, skills).ied, 1.0 - 0.75 * 0.60, 1e-9);
}

// An ATTACK's own ignored defence, boss damage and final damage belong to its
// swing, so they never reach the character's stat line -- OffenseStatsFor
// reads them off the skill being swung instead. Gungnir's Descent is the
// shape: 30% ignored while it lands, nothing at all for the spear thrust after
// it. Mist Eruption's final damage is the same promise about a different
// lever.
TEST_F(DerivedStatsTest, AnAttacksOwnSwingLeversStayOffTheStatLine) {
  CharacterInstance c = MakeCharacter(rng_, 15, 100);
  Skill gungnir = Marksmanship();
  gungnir.set_name("Gungnir's Descent");
  gungnir.set_kind(SKILL_KIND_ATTACK);
  gungnir.mutable_base()->set_boss_pct(0.30);
  gungnir.mutable_base()->set_final_dmg_pct(0.20);
  std::map<std::string, Skill> skills = {{"gungnirs_descent", gungnir}};
  ASSERT_TRUE(c.LearnSkill(gungnir, 20));

  DerivedStats stats = DerivedStatsFor(c, skills);
  EXPECT_DOUBLE_EQ(stats.ied, 0.0);
  EXPECT_DOUBLE_EQ(stats.boss_pct, 0.0);
  EXPECT_DOUBLE_EQ(stats.final_dmg_pct, 0.0);

  // The same levers on a passive fold in as they always have.
  Skill passive = gungnir;
  passive.set_kind(SKILL_KIND_PASSIVE);
  DerivedStats folded = DerivedStatsFor(c, {{"gungnirs_descent", passive}});
  EXPECT_NEAR(folded.ied, 0.25, 1e-9);
  EXPECT_NEAR(folded.boss_pct, 0.30, 1e-9);
  EXPECT_NEAR(folded.final_dmg_pct, 0.20, 1e-9);
}

// The half an attack states apart is the half it keeps, whatever the lever:
// Cruel Stab's final damage follows the Shadower onto Assassinate, where the
// 50% Assassinate states for itself does not follow them back.
TEST_F(DerivedStatsTest, AnAttacksKeptHalfReachesTheStatLine) {
  CharacterInstance c = MakeCharacter(rng_, 15, 100);
  Skill stab = Marksmanship();
  stab.set_name("Cruel Stab");
  stab.set_kind(SKILL_KIND_ATTACK);
  stab.mutable_base()->set_final_dmg_pct(0.50);
  stab.mutable_passive()->set_final_dmg_pct(0.05);
  stab.mutable_passive_per_level()->set_final_dmg_pct(0.01);
  std::map<std::string, Skill> skills = {{"cruel_stab", stab}};
  ASSERT_TRUE(c.LearnSkill(stab, 20));

  DerivedStats stats = DerivedStatsFor(c, skills);
  EXPECT_NEAR(stats.final_dmg_pct, 0.24, 1e-9);
}

// The two halves together are the whole effect: what one takes the other
// leaves, and neither invents a lever. The skill page heads them apart, so a
// lever falling through both cracks would go unstated as well as unpaid.
TEST_F(DerivedStatsTest, TheSwingLeversAndTheRestPartitionAnEffect) {
  SkillEffect effect;
  effect.set_ied_pct(0.40);
  effect.set_boss_pct(0.30);
  effect.set_crit_rate(0.20);
  effect.set_final_dmg_pct(0.10);
  effect.set_hp_recover_pct(0.08);
  effect.set_attack(20);
  effect.set_damage_pct(0.05);

  SkillEffect swing = SwingLeversOf(effect);
  EXPECT_DOUBLE_EQ(swing.ied_pct(), 0.40);
  EXPECT_DOUBLE_EQ(swing.boss_pct(), 0.30);
  EXPECT_DOUBLE_EQ(swing.crit_rate(), 0.20);
  EXPECT_DOUBLE_EQ(swing.final_dmg_pct(), 0.10);
  EXPECT_DOUBLE_EQ(swing.hp_recover_pct(), 0.08);
  EXPECT_EQ(swing.attack(), 0);
  EXPECT_DOUBLE_EQ(swing.damage_pct(), 0.0);

  SkillEffect kept = WithoutSwingLevers(effect);
  EXPECT_DOUBLE_EQ(kept.ied_pct(), 0.0);
  EXPECT_DOUBLE_EQ(kept.boss_pct(), 0.0);
  EXPECT_DOUBLE_EQ(kept.crit_rate(), 0.0);
  EXPECT_DOUBLE_EQ(kept.final_dmg_pct(), 0.0);
  EXPECT_DOUBLE_EQ(kept.hp_recover_pct(), 0.0);
  EXPECT_EQ(kept.attack(), 20);
  EXPECT_DOUBLE_EQ(kept.damage_pct(), 0.05);
}

// Pick Pocket knocks the meso loose and Meso Explosion throws it, so neither
// is worth anything without the other -- and Meso Mastery's points land on a
// line of the throw, whichever order the catalog folds the three in.
TEST_F(DerivedStatsTest, MesoExplosionPairsWithPickPocket) {
  CharacterInstance c = MakeCharacter(rng_, 15, 100);
  Skill pocket;
  pocket.set_name("Pick Pocket");
  pocket.set_kind(SKILL_KIND_PASSIVE);
  pocket.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  pocket.set_max_level(10);
  pocket.mutable_base()->set_meso_drop_chance(0.12);
  pocket.mutable_per_level()->set_meso_drop_chance(0.02);

  Skill explosion;
  explosion.set_name("Meso Explosion");
  explosion.set_kind(SKILL_KIND_PASSIVE);
  explosion.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  explosion.set_max_level(20);
  explosion.set_lines(2);
  explosion.mutable_base()->set_meso_hit_pct(0.43);
  explosion.mutable_per_level()->set_meso_hit_pct(0.03);

  Skill mastery;
  mastery.set_name("Meso Mastery");
  mastery.set_kind(SKILL_KIND_PASSIVE);
  mastery.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  mastery.set_max_level(10);
  SkillBoost* mastery_boost = mastery.add_boost();
  mastery_boost->set_skill_name("Meso Explosion");
  mastery_boost->mutable_effect()->set_skill_pct(0.02);
  mastery_boost->mutable_effect_per_level()->set_skill_pct(0.02);
  mastery.mutable_base()->set_meso_pct(0.02);
  mastery.mutable_per_level()->set_meso_pct(0.02);

  // The explosion alone is worth nothing: there is no meso to throw.
  std::map<std::string, Skill> lonely = {{"explosion", explosion}};
  ASSERT_TRUE(c.LearnSkill(explosion, 20));
  EXPECT_TRUE(DerivedStatsFor(c, lonely).final_attacks.empty());

  std::map<std::string, Skill> skills = {
      {"pocket", pocket}, {"explosion", explosion}, {"mastery", mastery}};
  ASSERT_TRUE(c.LearnSkill(pocket, 10));
  ASSERT_TRUE(c.LearnSkill(mastery, 10));
  DerivedStats derived = DerivedStatsFor(c, skills);

  ASSERT_EQ(derived.final_attacks.size(), 1u);
  // 30% a line to knock one loose, and one throws two lines of 100% + Meso
  // Mastery's 20 points.
  EXPECT_NEAR(derived.final_attacks[0].chance, 0.30, 1e-9);
  EXPECT_NEAR(derived.final_attacks[0].damage_pct, 2 * 1.20, 1e-9);
  EXPECT_TRUE(derived.final_attacks[0].per_line);
  EXPECT_NEAR(derived.meso_pct, 0.20, 1e-9);
  // Nothing has branded the coins, so they hit a boss for what the character
  // does.
  EXPECT_NEAR(derived.final_attacks[0].boss_pct, 0.0, 1e-9);

  // Blood Money brands them. Its boss damage lands on the throw and not on the
  // Shadower, so the stat line is untouched.
  Skill money;
  money.set_name("Blood Money");
  money.set_kind(SKILL_KIND_PASSIVE);
  money.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  money.set_max_level(20);
  SkillBoost* money_boost = money.add_boost();
  money_boost->set_skill_name("Meso Explosion");
  money_boost->mutable_effect()->set_boss_pct(0.11);
  money_boost->mutable_effect_per_level()->set_boss_pct(0.01);
  skills["money"] = money;
  ASSERT_TRUE(c.LearnSkill(money, 20));
  DerivedStats branded = DerivedStatsFor(c, skills);

  ASSERT_EQ(branded.final_attacks.size(), 1u);
  EXPECT_NEAR(branded.final_attacks[0].boss_pct, 0.30, 1e-9);
  EXPECT_NEAR(branded.boss_pct, 0.0, 1e-9);
}

// Boss damage sums across the passives granting it, like plain damage and
// unlike IED, which each swing keeps to itself.
TEST_F(DerivedStatsTest, BossDamageSumsAcrossPassives) {
  CharacterInstance c = MakeCharacter(rng_, 15, 100);
  Skill spirit;
  spirit.set_name("Spirit of the Star");
  spirit.set_kind(SKILL_KIND_PASSIVE);
  spirit.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  spirit.set_max_level(10);
  spirit.mutable_base()->set_boss_pct(0.01);
  spirit.mutable_per_level()->set_boss_pct(0.01);
  Skill other = spirit;
  other.set_name("Something Else");
  other.set_max_level(5);
  other.mutable_base()->set_boss_pct(0.05);
  other.mutable_per_level()->set_boss_pct(0.0);
  std::map<std::string, Skill> skills = {{"spirit", spirit}, {"other", other}};

  ASSERT_TRUE(c.LearnSkill(spirit, 10));
  ASSERT_TRUE(c.LearnSkill(other, 1));
  EXPECT_NEAR(DerivedStatsFor(c, skills).boss_pct, 0.15, 1e-9);
}

// Holy Fountain states a pulse and a wait, and both halves move with the
// level. They reach the fight apart, so it can pour on the clock.
TEST_F(DerivedStatsTest, AFountainKeepsItsPulseAndItsInterval) {
  CharacterInstance c = MakeCharacter(rng_, 15, 100);
  Skill fountain;
  fountain.set_name("Holy Fountain");
  fountain.set_kind(SKILL_KIND_PASSIVE);
  fountain.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  fountain.set_max_level(10);
  fountain.mutable_base()->set_regen_pct(0.13);
  fountain.mutable_per_level()->set_regen_pct(0.03);
  fountain.mutable_base()->set_regen_interval_seconds(7.5);
  fountain.mutable_per_level()->set_regen_interval_seconds(-0.5);
  std::map<std::string, Skill> skills = {{"holy_fountain", fountain}};

  ASSERT_TRUE(c.LearnSkill(fountain, 1));
  std::vector<RegenPulse> pulses = DerivedStatsFor(c, skills).regen_pulses;
  ASSERT_EQ(pulses.size(), 1);
  EXPECT_NEAR(pulses[0].pct, 0.13, 1e-9);
  EXPECT_NEAR(pulses[0].interval_seconds, 7.5, 1e-9);

  ASSERT_TRUE(c.LearnSkill(fountain, 9));  // up to its master level
  pulses = DerivedStatsFor(c, skills).regen_pulses;
  ASSERT_EQ(pulses.size(), 1);
  EXPECT_NEAR(pulses[0].pct, 0.40, 1e-9);
  EXPECT_NEAR(pulses[0].interval_seconds, 3.0, 1e-9);
}

// Holy Water's shape: the same pulse again for every whole step of INT the
// character carries, so 2500 doubles it and 5000 trebles it. Charged against
// the whole of their INT -- what a skill grants counts with what AP bought.
TEST_F(DerivedStatsTest, AFountainCanPourHarderForACleverCharacter) {
  Skill water;
  water.set_name("Holy Water");
  water.set_kind(SKILL_KIND_PASSIVE);
  water.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  water.set_max_level(10);
  water.mutable_base()->set_regen_pct(0.005);
  water.mutable_per_level()->set_regen_pct(0.005);
  water.mutable_base()->set_regen_interval_seconds(10.0);
  water.mutable_base()->set_regen_int_step(2500);
  Skill wisdom;
  wisdom.set_name("High Wisdom");
  wisdom.set_kind(SKILL_KIND_PASSIVE);
  wisdom.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  wisdom.set_max_level(10);
  wisdom.mutable_base()->set_int_(1000);
  std::map<std::string, Skill> skills = {{"holy_water", water},
                                         {"high_wisdom", wisdom}};

  CharacterInstance dim = MakeStatCharacter(rng_, 0, 0, 2499, 0);
  ASSERT_TRUE(dim.LearnSkill(water, 10));
  std::vector<RegenPulse> pulses = DerivedStatsFor(dim, skills).regen_pulses;
  ASSERT_EQ(pulses.size(), 1);
  EXPECT_NEAR(pulses[0].pct, 0.05, 1e-9);

  // The helping grows and the clock does not, so a clever character is healed
  // in bigger pulses rather than more frequent ones.
  CharacterInstance clever = MakeStatCharacter(rng_, 0, 0, 5000, 0);
  ASSERT_TRUE(clever.LearnSkill(water, 10));
  pulses = DerivedStatsFor(clever, skills).regen_pulses;
  ASSERT_EQ(pulses.size(), 1);
  EXPECT_NEAR(pulses[0].pct, 3.0 * 0.05, 1e-9);
  EXPECT_NEAR(pulses[0].interval_seconds, 10.0, 1e-9);

  // The last 1000 points come from a skill rather than from AP, and buy the
  // same helping: a fold reading the allocation alone would stop at two.
  CharacterInstance granted = MakeStatCharacter(rng_, 0, 0, 4000, 0);
  ASSERT_TRUE(granted.LearnSkill(water, 10));
  ASSERT_TRUE(granted.LearnSkill(wisdom, 1));
  pulses = DerivedStatsFor(granted, skills).regen_pulses;
  ASSERT_EQ(pulses.size(), 1);
  EXPECT_NEAR(pulses[0].pct, 3.0 * 0.05, 1e-9);
}

// A Bishop carries three, on three different clocks. They stay apart rather
// than summing into one, since no single interval could describe them.
TEST_F(DerivedStatsTest, TwoFountainsKeepTheirOwnClocks) {
  CharacterInstance c = MakeCharacter(rng_, 15, 100);
  Skill fountain;
  fountain.set_name("Holy Fountain");
  fountain.set_kind(SKILL_KIND_PASSIVE);
  fountain.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  fountain.set_max_level(10);
  fountain.mutable_base()->set_regen_pct(0.13);
  fountain.mutable_base()->set_regen_interval_seconds(7.5);
  Skill infinity;
  infinity.set_name("Infinity");
  infinity.set_kind(SKILL_KIND_PASSIVE);
  infinity.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  infinity.set_max_level(10);
  infinity.mutable_base()->set_regen_pct(0.10);
  infinity.mutable_base()->set_regen_interval_seconds(5.0);
  std::map<std::string, Skill> skills = {{"holy_fountain", fountain},
                                         {"infinity", infinity}};

  ASSERT_TRUE(c.LearnSkill(fountain, 1));
  ASSERT_TRUE(c.LearnSkill(infinity, 1));
  std::vector<RegenPulse> pulses = DerivedStatsFor(c, skills).regen_pulses;
  ASSERT_EQ(pulses.size(), 2);
  EXPECT_NEAR(pulses[0].interval_seconds, 7.5, 1e-9);
  EXPECT_NEAR(pulses[1].interval_seconds, 5.0, 1e-9);
}

// A fountain with no wait between its pulses says nothing about when to pour,
// so it grants nothing.
TEST_F(DerivedStatsTest, AFountainWithNoIntervalGrantsNothing) {
  CharacterInstance c = MakeCharacter(rng_, 15, 100);
  Skill fountain;
  fountain.set_name("Holy Fountain");
  fountain.set_kind(SKILL_KIND_PASSIVE);
  fountain.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  fountain.set_max_level(10);
  fountain.mutable_base()->set_regen_pct(0.13);
  std::map<std::string, Skill> skills = {{"holy_fountain", fountain}};
  ASSERT_TRUE(c.LearnSkill(fountain, 1));

  EXPECT_TRUE(DerivedStatsFor(c, skills).regen_pulses.empty());
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
  std::map<std::string, Skill> skills = LoadTestData<Skill>("skills");
  std::map<std::string, EquipPrototype> equips =
      LoadTestData<EquipPrototype>("equip");
  const Skill& mastery = skills.at("bandit_shield_mastery");

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
  without.at("bandit_shield_mastery").mutable_base()->clear_def_pct();
  without.at("bandit_shield_mastery").mutable_per_level()->clear_def_pct();
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

// --- Maple Warrior ---

// Maple Warrior's shape: a share of what AP bought, granted back as flat
// stat. 1% at level 1 climbing to 15% at 30, which is GMS's ceil(L/2)%.
Skill MapleWarrior() {
  Skill skill;
  skill.set_name("Maple Warrior");
  skill.set_kind(SKILL_KIND_PASSIVE);
  skill.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  skill.set_max_level(30);
  skill.mutable_base()->set_ap_stat_pct(0.01);
  skill.mutable_per_level()->set_ap_stat_pct(0.00483);
  return skill;
}

// A character who spent AP, and a ring that grants the same stat, so the test
// can tell what the share is charged against.
CharacterInstance MapleWarriorCharacter(std::mt19937& rng) {
  Character proto;
  proto.set_level(140);
  proto.set_job(JOB_SWORDMAN);
  proto.set_job_stage(1);
  proto.mutable_allocated_stats()->set_str(1000);
  proto.mutable_allocated_stats()->set_dex(100);
  (*proto.mutable_sp_by_stage())[1] = 100;
  return CharacterInstance(rng, std::move(proto));
}

TEST_F(DerivedStatsTest, MapleWarriorGrantsAShareOfWhatApBought) {
  CharacterInstance c = MapleWarriorCharacter(rng_);
  Skill mw = MapleWarrior();
  std::map<std::string, Skill> skills = {{"maple_warrior", mw}};
  ASSERT_TRUE(c.LearnSkill(mw, 30));

  // A ring's 500 STR is not part of what AP bought, so the 15% is 150 and not
  // 225 -- and what the skill grants is a grant like the ring's.
  EquipPrototype ring;
  ring.set_name("Ring");
  ring.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  ring.mutable_base_stats()->set_str(500);
  c.PickUp(std::make_unique<EquipInstance>(ring));
  c.Equip(0);

  DerivedStats stats = DerivedStatsFor(c, skills);
  EXPECT_EQ(stats.skill_stats.str(), 150);
  EXPECT_EQ(stats.skill_stats.dex(), 15);
  EXPECT_EQ(TotalEquipStats(c, stats).str(), 650);
  // 1.5 DEF per STR and 0.4 per DEX, over everything the character has.
  EXPECT_EQ(stats.base_def, static_cast<int>(1.5 * 1650 + 0.4 * 115));
}

// The share is rounded down per stat, which is what GMS does with it: 100 DEX
// at 1% is one point, and 100 at 15% is fifteen rather than a fraction of the
// pair summed.
TEST_F(DerivedStatsTest, MapleWarriorRoundsEachStatDown) {
  CharacterInstance c = MapleWarriorCharacter(rng_);
  Skill mw = MapleWarrior();
  std::map<std::string, Skill> skills = {{"maple_warrior", mw}};
  ASSERT_TRUE(c.LearnSkill(mw, 1));

  DerivedStats stats = DerivedStatsFor(c, skills);
  EXPECT_EQ(stats.skill_stats.str(), 10);
  EXPECT_EQ(stats.skill_stats.dex(), 1);
  // Nothing was spent on either of these, so neither grants anything.
  EXPECT_EQ(stats.skill_stats.int_(), 0);
  EXPECT_EQ(stats.skill_stats.luk(), 0);
}

// --- Hyper Stats ---

// A character at the level cap, with the whole pool to spend.
CharacterInstance HyperStatCharacter(std::mt19937& rng) {
  Character proto;
  proto.set_level(200);
  proto.set_job(JOB_SWORDMAN);
  proto.set_job_stage(1);
  proto.mutable_allocated_stats()->set_str(1000);
  proto.mutable_allocated_stats()->set_hp(10000);
  (*proto.mutable_sp_by_stage())[1] = 100;
  return CharacterInstance(rng, std::move(proto));
}

TEST_F(DerivedStatsTest, HyperStatsReachEveryLeverTheyName) {
  CharacterInstance c = HyperStatCharacter(rng_);
  const StatPreset farming = StatPreset::kFarming;
  ASSERT_TRUE(c.AllocateHyperStat(HYPER_STAT_FIELD_STR, farming, 10));
  ASSERT_TRUE(c.AllocateHyperStat(HYPER_STAT_FIELD_MAX_HP, farming, 5));
  ASSERT_TRUE(c.AllocateHyperStat(HYPER_STAT_FIELD_CRIT_RATE, farming, 5));
  ASSERT_TRUE(c.AllocateHyperStat(HYPER_STAT_FIELD_ATTACK, farming, 3));

  DerivedStats stats = DerivedStatsFor(c, {});
  // 300 from the Hyper Stat and 30 from the Inner Ability lines every
  // character past 160 is holding.
  EXPECT_EQ(stats.skill_stats.str(), 330);
  EXPECT_EQ(stats.skill_stats.attack(), 9);
  EXPECT_EQ(stats.skill_stats.magic_attack(), 9)
      << "one stat pays both attacks";
  EXPECT_DOUBLE_EQ(stats.crit_rate, 0.05);
  EXPECT_EQ(stats.max_hp, 11000) << "+10% over a 10,000 pool";
}

TEST_F(DerivedStatsTest, HyperDamageLeversSplitBossFromNormal) {
  CharacterInstance c = HyperStatCharacter(rng_);
  const StatPreset farming = StatPreset::kFarming;
  ASSERT_TRUE(c.AllocateHyperStat(HYPER_STAT_FIELD_DAMAGE, farming, 5));
  ASSERT_TRUE(c.AllocateHyperStat(HYPER_STAT_FIELD_BOSS_DAMAGE, farming, 5));
  ASSERT_TRUE(c.AllocateHyperStat(HYPER_STAT_FIELD_NORMAL_DAMAGE, farming, 5));
  ASSERT_TRUE(c.AllocateHyperStat(HYPER_STAT_FIELD_IED, farming, 5));
  ASSERT_TRUE(c.AllocateHyperStat(HYPER_STAT_FIELD_EXP, farming, 5));

  DerivedStats stats = DerivedStatsFor(c, {});
  EXPECT_DOUBLE_EQ(stats.damage_pct, 0.15);
  EXPECT_DOUBLE_EQ(stats.boss_pct, 0.15);
  EXPECT_DOUBLE_EQ(stats.normal_pct, 0.15);
  EXPECT_DOUBLE_EQ(stats.ied, 0.15);
  EXPECT_DOUBLE_EQ(stats.exp_pct, 0.025);
}

// The four stats are final stat: Maple Warrior takes its share of the
// allocation and nothing else, so the Hyper Stat's 300 is not lifted by it.
TEST_F(DerivedStatsTest, MapleWarriorLeavesTheHyperStatAlone) {
  CharacterInstance c = HyperStatCharacter(rng_);
  Skill mw = MapleWarrior();
  std::map<std::string, Skill> skills = {{"maple_warrior", mw}};
  ASSERT_TRUE(c.LearnSkill(mw, 30));
  ASSERT_TRUE(
      c.AllocateHyperStat(HYPER_STAT_FIELD_STR, StatPreset::kFarming, 10));

  DerivedStats stats = DerivedStatsFor(c, skills);
  EXPECT_EQ(stats.skill_stats.str(), 150 + 300 + 30)
      << "the default Inner Ability lines are final stat too";
}

// --- Inner Ability ---

// A character holding `lines` in the named preset, at a level that pays them.
CharacterInstance AbilityCharacter(std::mt19937& rng, AbilityRank rank,
                                   const std::vector<AbilityLine>& lines,
                                   StatPreset preset = StatPreset::kFarming,
                                   int level = 160) {
  Character proto;
  proto.set_level(level);
  proto.set_job(JOB_SWORDMAN);
  proto.set_job_stage(1);
  proto.mutable_allocated_stats()->set_str(1000);
  proto.mutable_allocated_stats()->set_hp(10000);
  (*proto.mutable_sp_by_stage())[1] = 100;
  AbilityPreset& held = PresetOf(*proto.mutable_inner_ability(), preset);
  held.set_rank(rank);
  for (const AbilityLine& line : lines) {
    *held.add_lines() = line;
  }
  return CharacterInstance(rng, std::move(proto));
}

AbilityLine Line(AbilityLineType type, AbilityRank rank) {
  AbilityLine line;
  line.set_type(type);
  line.set_rank(rank);
  return line;
}

// The lines every character is handed pay +10 all stat apiece, and nothing
// below the unlock level pays at all.
TEST_F(DerivedStatsTest, TheDefaultLinesPayFromLevel160) {
  CharacterInstance below = AbilityCharacter(
      rng_, ABILITY_RANK_RARE, {}, StatPreset::kFarming, /*level=*/159);
  EXPECT_EQ(DerivedStatsFor(below, {}).skill_stats.str(), 0);

  CharacterInstance c = AbilityCharacter(rng_, ABILITY_RANK_RARE, {});
  DerivedStats stats = DerivedStatsFor(c, {});
  EXPECT_EQ(stats.skill_stats.str(), 30) << "three Rare All Stats lines";
  EXPECT_EQ(stats.skill_stats.dex(), 30);
  EXPECT_EQ(stats.skill_stats.int_(), 30);
  EXPECT_EQ(stats.skill_stats.luk(), 30);
}

TEST_F(DerivedStatsTest, AbilityLinesReachEveryLeverTheyName) {
  CharacterInstance c = AbilityCharacter(
      rng_, ABILITY_RANK_LEGENDARY,
      {Line(ABILITY_LINE_TYPE_STR, ABILITY_RANK_LEGENDARY),
       Line(ABILITY_LINE_TYPE_ATTACK, ABILITY_RANK_LEGENDARY),
       Line(ABILITY_LINE_TYPE_CRIT_RATE, ABILITY_RANK_UNIQUE)});

  DerivedStats stats = DerivedStatsFor(c, {});
  EXPECT_EQ(stats.skill_stats.str(), 40);
  EXPECT_EQ(stats.skill_stats.attack(), 30);
  EXPECT_EQ(stats.skill_stats.magic_attack(), 0)
      << "the two attacks are separate lines here";
  EXPECT_DOUBLE_EQ(stats.crit_rate, 0.20);
}

TEST_F(DerivedStatsTest, AbilityPercentLinesLandWhereTheyBelong) {
  CharacterInstance c = AbilityCharacter(
      rng_, ABILITY_RANK_LEGENDARY,
      {Line(ABILITY_LINE_TYPE_BOSS_DAMAGE, ABILITY_RANK_LEGENDARY),
       Line(ABILITY_LINE_TYPE_MESO, ABILITY_RANK_LEGENDARY),
       Line(ABILITY_LINE_TYPE_ITEM_DROP, ABILITY_RANK_UNIQUE)});

  DerivedStats stats = DerivedStatsFor(c, {});
  EXPECT_DOUBLE_EQ(stats.boss_pct, 0.20);
  EXPECT_DOUBLE_EQ(stats.meso_pct, 0.20);
  EXPECT_DOUBLE_EQ(stats.item_drop_pct, 0.15);
}

TEST_F(DerivedStatsTest, AbilityHpAndSpeedLinesLandWhereTheyBelong) {
  CharacterInstance c = AbilityCharacter(
      rng_, ABILITY_RANK_LEGENDARY,
      {Line(ABILITY_LINE_TYPE_MAX_HP, ABILITY_RANK_LEGENDARY),
       Line(ABILITY_LINE_TYPE_MAX_HP_PCT, ABILITY_RANK_LEGENDARY),
       Line(ABILITY_LINE_TYPE_ATTACK_SPEED, ABILITY_RANK_LEGENDARY)});

  DerivedStats stats = DerivedStatsFor(c, {});
  EXPECT_EQ(stats.max_hp, static_cast<int>((10000 + 600) * 1.20));
  EXPECT_EQ(stats.attack_speed_bonus, 1);
}

TEST_F(DerivedStatsTest, AbilityBuffDurationAndNormalDamageLand) {
  CharacterInstance c = AbilityCharacter(
      rng_, ABILITY_RANK_LEGENDARY,
      {Line(ABILITY_LINE_TYPE_BUFF_DURATION, ABILITY_RANK_LEGENDARY),
       Line(ABILITY_LINE_TYPE_NORMAL_DAMAGE, ABILITY_RANK_LEGENDARY),
       Line(ABILITY_LINE_TYPE_MAGIC_ATTACK, ABILITY_RANK_UNIQUE)});

  DerivedStats stats = DerivedStatsFor(c, {});
  EXPECT_DOUBLE_EQ(stats.buff_duration_pct, 0.50);
  EXPECT_DOUBLE_EQ(stats.normal_pct, 0.10);
  EXPECT_EQ(stats.skill_stats.magic_attack(), 21);
  EXPECT_EQ(stats.skill_stats.attack(), 0);
}

// All Stats pays every one of the four, which is what makes one line of it
// worth four of a single stat's.
TEST_F(DerivedStatsTest, AllStatsPaysAllFour) {
  CharacterInstance c = AbilityCharacter(
      rng_, ABILITY_RANK_LEGENDARY,
      {Line(ABILITY_LINE_TYPE_ALL_STATS, ABILITY_RANK_LEGENDARY),
       Line(ABILITY_LINE_TYPE_STR, ABILITY_RANK_UNIQUE),
       Line(ABILITY_LINE_TYPE_MESO, ABILITY_RANK_EPIC)});

  DerivedStats stats = DerivedStatsFor(c, {});
  EXPECT_EQ(stats.skill_stats.str(), 70);
  EXPECT_EQ(stats.skill_stats.luk(), 40);
}

// The stats an ability grants are final stat, exactly as a Hyper Stat's are:
// Maple Warrior's share is charged against the allocation alone.
TEST_F(DerivedStatsTest, MapleWarriorLeavesTheAbilityAlone) {
  CharacterInstance c =
      AbilityCharacter(rng_, ABILITY_RANK_LEGENDARY,
                       {Line(ABILITY_LINE_TYPE_STR, ABILITY_RANK_LEGENDARY),
                        Line(ABILITY_LINE_TYPE_MESO, ABILITY_RANK_EPIC),
                        Line(ABILITY_LINE_TYPE_ITEM_DROP, ABILITY_RANK_EPIC)});
  Skill mw = MapleWarrior();
  std::map<std::string, Skill> skills = {{"maple_warrior", mw}};
  ASSERT_TRUE(c.LearnSkill(mw, 30));

  DerivedStats stats = DerivedStatsFor(c, skills);
  EXPECT_EQ(stats.skill_stats.str(), 150 + 40);
}

// The preset read is the one the caller asks for, the same as the Hyper Stat
// allocation beside it.
TEST_F(DerivedStatsTest, TheBossingAbilityIsReadOnlyWhenAskedFor) {
  CharacterInstance c = AbilityCharacter(
      rng_, ABILITY_RANK_LEGENDARY,
      {Line(ABILITY_LINE_TYPE_BOSS_DAMAGE, ABILITY_RANK_LEGENDARY),
       Line(ABILITY_LINE_TYPE_MESO, ABILITY_RANK_EPIC),
       Line(ABILITY_LINE_TYPE_ITEM_DROP, ABILITY_RANK_EPIC)},
      StatPreset::kBossing);

  EXPECT_DOUBLE_EQ(DerivedStatsFor(c, {}).boss_pct, 0.0);
  EXPECT_DOUBLE_EQ(
      DerivedStatsFor(c, {}, {}, {}, StatPreset::kBossing).boss_pct, 0.20);
}

// The allocation read is the one the caller asks for.
TEST_F(DerivedStatsTest, TheBossingAllocationIsReadOnlyWhenAskedFor) {
  CharacterInstance c = HyperStatCharacter(rng_);
  ASSERT_TRUE(c.AllocateHyperStat(HYPER_STAT_FIELD_BOSS_DAMAGE,
                                  StatPreset::kBossing, 10));

  EXPECT_DOUBLE_EQ(DerivedStatsFor(c, {}).boss_pct, 0.0);
  EXPECT_DOUBLE_EQ(
      DerivedStatsFor(c, {}, {}, {}, StatPreset::kBossing).boss_pct, 0.35);
}

// Arcane Force from the Hyper Stat meets what the symbols carry.
TEST_F(DerivedStatsTest, HyperArcaneForceAddsToTheSymbols) {
  CharacterInstance c = HyperStatCharacter(rng_);
  EXPECT_EQ(c.arcane_force(), 0);
  ASSERT_TRUE(c.AllocateHyperStat(HYPER_STAT_FIELD_ARCANE_FORCE,
                                  StatPreset::kFarming, 10));
  EXPECT_EQ(c.arcane_force(), 50);
  EXPECT_EQ(c.arcane_force(StatPreset::kBossing), 0);
}

// --- Final Pact ---

// Final Pact's shape: a wait between revivals that SHORTENS as the skill is
// levelled, 1103 seconds down to 900 at 30.
Skill FinalPact() {
  Skill skill;
  skill.set_name("Final Pact");
  skill.set_kind(SKILL_KIND_PASSIVE);
  skill.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  skill.set_max_level(30);
  skill.mutable_base()->set_revive_cooldown_seconds(1103);
  skill.mutable_per_level()->set_revive_cooldown_seconds(-7);
  return skill;
}

TEST_F(DerivedStatsTest, APactShortensItsOwnWaitAsItIsLevelled) {
  CharacterInstance c = MakeCharacter(rng_, 100, 100);
  Skill pact = FinalPact();
  std::map<std::string, Skill> skills = {{"final_pact", pact}};
  EXPECT_DOUBLE_EQ(DerivedStatsFor(c, skills).revive_cooldown_seconds, 0.0);

  ASSERT_TRUE(c.LearnSkill(pact, 30));
  EXPECT_DOUBLE_EQ(DerivedStatsFor(c, skills).revive_cooldown_seconds, 900.0);
}

// Two pacts are not one long one: what a character wants to know is how soon
// the next revival comes, so the shorter wait stands.
TEST_F(DerivedStatsTest, TwoPactsLeaveTheShorterWaitStanding) {
  CharacterInstance c = MakeCharacter(rng_, 100, 100);
  Skill pact = FinalPact();
  Skill lesser = FinalPact();
  lesser.set_name("Lesser Pact");
  lesser.mutable_base()->set_revive_cooldown_seconds(300);
  lesser.clear_per_level();
  std::map<std::string, Skill> skills = {{"final_pact", pact},
                                         {"lesser_pact", lesser}};
  ASSERT_TRUE(c.LearnSkill(pact, 30));
  ASSERT_TRUE(c.LearnSkill(lesser, 1));

  EXPECT_DOUBLE_EQ(DerivedStatsFor(c, skills).revive_cooldown_seconds, 300.0);
}

// The cut sums where the wait itself does not, and is taken off whichever
// pact is left standing. A character holding only the cut is revived by
// nothing, so the wait stays at nothing.
TEST_F(DerivedStatsTest, TheReviveCutComesOffTheShortestPact) {
  CharacterInstance c = MakeCharacter(rng_, 100, 100);
  Skill pact = FinalPact();
  Skill hyper = FinalPact();
  hyper.set_name("Final Pact - Reduce Cooldown");
  hyper.set_max_level(1);
  hyper.clear_base();
  hyper.clear_per_level();
  hyper.mutable_base()->set_revive_cooldown_cut_seconds(150);
  std::map<std::string, Skill> skills = {{"final_pact", pact},
                                         {"reduce", hyper}};
  ASSERT_TRUE(c.LearnSkill(hyper, 1));
  EXPECT_DOUBLE_EQ(DerivedStatsFor(c, skills).revive_cooldown_seconds, 0.0);

  ASSERT_TRUE(c.LearnSkill(pact, 30));
  EXPECT_DOUBLE_EQ(DerivedStatsFor(c, skills).revive_cooldown_seconds, 750.0);
}

// --- timed buffs ---

// Dark Resonance's shape: ignored defence for good, and more of it for a
// while at a time.
Skill DarkResonance() {
  Skill skill;
  skill.set_name("Dark Resonance");
  skill.set_kind(SKILL_KIND_ACTIVE);
  skill.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  skill.set_max_level(30);
  skill.set_cooldown_seconds(70.0);
  skill.mutable_base()->set_ied_pct(0.01);
  skill.mutable_per_level()->set_ied_pct(0.01);
  Buff* buff = skill.mutable_buff();
  buff->set_duration_seconds(15.0);
  buff->set_duration_seconds_per_level(0.5);
  buff->set_cooldown_reduction_seconds(0.35);
  buff->mutable_base()->set_ied_pct(0.10);
  buff->mutable_per_level()->set_final_dmg_pct(0.002);
  return skill;
}

// The whole reason a buff cannot be a multiplier over a finished damage
// number: what it grants meets what the character already has the way two
// skills meet, and two sources of ignored defence combine rather than sum.
TEST_F(DerivedStatsTest, ABuffCombinesWithThePermanentHalf) {
  CharacterInstance c = MakeCharacter(rng_, 100, 100);
  Skill resonance = DarkResonance();
  std::map<std::string, Skill> skills = {{"dark_resonance", resonance}};
  ASSERT_TRUE(c.LearnSkill(resonance, 30));

  EXPECT_NEAR(DerivedStatsFor(c, skills).ied, 0.30, 1e-9);
  const Skill* up[] = {&skills.at("dark_resonance")};
  DerivedStats buffed = DerivedStatsFor(c, skills, absl::MakeConstSpan(up));
  // 30% and 10%, which leave 63% of the monster's DEF between them.
  EXPECT_NEAR(buffed.ied, 0.37, 1e-9);
  EXPECT_NEAR(buffed.final_dmg_pct, 0.058, 1e-9);
}

TEST_F(DerivedStatsTest, OnlyALearnedBuffIsOneTheCharacterCanPutUp) {
  CharacterInstance c = MakeCharacter(rng_, 100, 100);
  Skill resonance = DarkResonance();
  Skill plain = PhysicalTraining();
  std::map<std::string, Skill> skills = {{"dark_resonance", resonance},
                                         {"physical_training", plain}};
  ASSERT_TRUE(c.LearnSkill(plain, 5));
  EXPECT_TRUE(BuffSkillsFor(c, skills).empty());

  ASSERT_TRUE(c.LearnSkill(resonance, 1));
  std::vector<const Skill*> buffs = BuffSkillsFor(c, skills);
  ASSERT_EQ(buffs.size(), 1u);
  EXPECT_EQ(buffs[0]->name(), "Dark Resonance");
}

// Drop rate arrives in two currencies -- whole percents on an equip, a
// fraction on a passive -- and has to come out as one number.
TEST_F(DerivedStatsTest, DropRateSumsWornAndGranted) {
  std::mt19937 rng(1);
  CharacterInstance c = MakeCharacter(rng, 10, 0);
  EXPECT_NEAR(DerivedStatsFor(c, {}).item_drop_pct, 0.0, 1e-9);

  EquipPrototype charm;
  charm.set_name("Lucky Charm");
  charm.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  charm.mutable_base_stats()->set_item_drop_rate(20);
  c.PickUp(std::make_unique<EquipInstance>(charm));
  ASSERT_TRUE(c.Equip(c.inventory().size() - 1));
  EXPECT_NEAR(DerivedStatsFor(c, {}).item_drop_pct, 0.20, 1e-9);

  Skill greed;
  greed.set_name("Greed");
  greed.set_kind(SKILL_KIND_PASSIVE);
  greed.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  greed.set_max_level(10);
  greed.mutable_base()->set_item_drop_pct(0.01);
  greed.mutable_per_level()->set_item_drop_pct(0.01);
  std::map<std::string, Skill> skills = {{"greed", greed}};
  ASSERT_TRUE(c.LearnSkill(greed, 10));
  EXPECT_NEAR(DerivedStatsFor(c, skills).item_drop_pct, 0.30, 1e-9);
}

// Meso arrives in the same two currencies, but the worn half is capped on its
// own and the granted half is not.
TEST_F(DerivedStatsTest, MesoCapsWhatIsWornAndThenTheWholeSum) {
  std::mt19937 rng(1);
  CharacterInstance c = MakeCharacter(rng, 10, 0);
  EXPECT_NEAR(MesoBonus(DerivedStatsFor(c, {})), 0.0, 1e-9);

  EquipPrototype coin;
  coin.set_name("Coin Charm");
  coin.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  coin.mutable_base_stats()->set_meso_rate(150);
  c.PickUp(std::make_unique<EquipInstance>(coin));
  ASSERT_TRUE(c.Equip(c.inventory().size() - 1));
  DerivedStats worn = DerivedStatsFor(c, {});
  EXPECT_NEAR(worn.equip_meso_pct, 1.50, 1e-9);
  EXPECT_NEAR(MesoBonus(worn), kEquipMesoSoftCap, 1e-9);

  // A skill's share is past the worn cap, not under it.
  Skill greed;
  greed.set_name("Greed");
  greed.set_kind(SKILL_KIND_PASSIVE);
  greed.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  greed.set_max_level(10);
  greed.mutable_base()->set_meso_pct(0.05);
  greed.mutable_per_level()->set_meso_pct(0.05);
  std::map<std::string, Skill> skills = {{"greed", greed}};
  ASSERT_TRUE(c.LearnSkill(greed, 10));
  EXPECT_NEAR(MesoBonus(DerivedStatsFor(c, skills)), 1.50, 1e-9);

  // And the hard cap holds whatever the two come to.
  DerivedStats piled = DerivedStatsFor(c, skills);
  piled.meso_pct = 10.0;
  EXPECT_NEAR(MesoBonus(piled), kMesoHardCap, 1e-9);
}

// The Wealth Acquisition Potion is three levers at once, and the share it adds
// sits past the equipment cap rather than under it.
TEST_F(DerivedStatsTest, TheWealthPotionAddsAShareADropRateAndAMultiplier) {
  std::mt19937 rng(1);
  CharacterInstance c = MakeCharacter(rng, kConsumableUnlockLevel, 0);
  EquipPrototype coin;
  coin.set_name("Coin Charm");
  coin.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  coin.mutable_base_stats()->set_meso_rate(100);
  c.PickUp(std::make_unique<EquipInstance>(coin));
  ASSERT_TRUE(c.Equip(c.inventory().size() - 1));

  DerivedStats before = DerivedStatsFor(c, {});
  EXPECT_NEAR(MesoBonus(before), 1.00, 1e-9);
  EXPECT_NEAR(before.meso_final_mult, 1.0, 1e-9);
  EXPECT_NEAR(before.item_drop_pct, 0.0, 1e-9);

  ASSERT_TRUE(c.ToggleConsumable(CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION));
  DerivedStats after = DerivedStatsFor(c, {});
  // Past the cap, not under it: a character already sitting on +100% from
  // gear earns 2.2 x 1.2 == 2.64 times what a bare one does.
  EXPECT_NEAR(MesoBonus(after), 1.20, 1e-9);
  EXPECT_NEAR(after.meso_final_mult, 1.20, 1e-9);
  EXPECT_NEAR(after.item_drop_pct, 0.20, 1e-9);

  // A boss fight pays none of it: the pot is farming's, and the bossing
  // preset is what a fight -- and the Boss stats tab -- reads.
  DerivedStats bossing = DerivedStatsFor(c, {}, {}, {}, StatPreset::kBossing);
  EXPECT_NEAR(MesoBonus(bossing), 1.00, 1e-9);
  EXPECT_NEAR(bossing.meso_final_mult, 1.0, 1e-9);
  EXPECT_NEAR(bossing.item_drop_pct, 0.0, 1e-9);
}

// The Extreme Green Potion is the mirror of it: a stage in a boss fight, and
// nothing at all on a map.
TEST_F(DerivedStatsTest, TheGreenPotionIsAStageInABossFightAlone) {
  std::mt19937 rng(1);
  CharacterInstance c = MakeCharacter(rng, 190, 0);
  ASSERT_TRUE(c.ToggleConsumable(CONSUMABLE_TYPE_EXTREME_GREEN_POTION));

  EXPECT_EQ(DerivedStatsFor(c, {}).uncapped_attack_speed_bonus, 0);
  EXPECT_EQ(DerivedStatsFor(c, {}, {}, {}, StatPreset::kBossing)
                .uncapped_attack_speed_bonus,
            kGreenPotionAttackSpeed);
}

// --- the party ---

// Bless's shape: what the caster keeps and what they hold over the party are
// the same thing, which is the common case.
Skill Bless() {
  Skill skill;
  skill.set_name("Bless");
  skill.set_kind(SKILL_KIND_PASSIVE);
  skill.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  skill.set_max_level(10);
  skill.mutable_base()->set_attack(6);
  skill.mutable_per_level()->set_attack(1);
  *skill.mutable_ally_base() = skill.base();
  *skill.mutable_ally_per_level() = skill.per_level();
  return skill;
}

// A party of one caster, built for the span DerivedStatsFor takes.
std::vector<CharacterInstance> PartyOf(CharacterInstance ally) {
  std::vector<CharacterInstance> party;
  party.push_back(std::move(ally));
  return party;
}

TEST_F(DerivedStatsTest, AllyGrantReachesOnlyWhoeverLacksTheSkill) {
  Skill bless = Bless();
  std::map<std::string, Skill> skills = {{"bless", bless}};
  CharacterInstance caster = MakeCharacter(rng_, 100, 0);
  ASSERT_TRUE(caster.LearnSkill(bless, 10));
  CharacterInstance plain = MakeCharacter(rng_, 100, 0);

  std::vector<CharacterInstance> party = PartyOf(std::move(caster));
  EXPECT_EQ(DerivedStatsFor(plain, skills).skill_stats.attack(), 0);
  EXPECT_EQ(DerivedStatsFor(plain, skills, {}, party).skill_stats.attack(), 15);

  // A second caster keeps their own rather than taking both: a buff does not
  // stack with itself.
  CharacterInstance other = MakeCharacter(rng_, 100, 0);
  ASSERT_TRUE(other.LearnSkill(bless, 10));
  EXPECT_EQ(DerivedStatsFor(other, skills, {}, party).skill_stats.attack(), 15);
}

// Smokescreen's shape: the party half lives inside the BUFF, so it reaches an
// ally as a window of their own rather than folding in as a passive. What
// DerivedStatsFor is asked here is the negative -- nothing permanent -- and
// AllyBuffsFor is what hands the fight the window.
Skill Smokescreen() {
  Skill skill;
  skill.set_name("Smokescreen");
  skill.set_kind(SKILL_KIND_ACTIVE);
  skill.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  skill.set_max_level(10);
  skill.set_cooldown_seconds(120.0);
  Buff& buff = *skill.mutable_buff();
  buff.set_duration_seconds(30.0);
  buff.mutable_base()->set_damage_taken_pct(0.01);
  buff.mutable_per_level()->set_damage_taken_pct(0.01);
  buff.mutable_ally_base()->set_damage_taken_pct(0.01);
  buff.mutable_ally_per_level()->set_damage_taken_pct(0.01);
  return skill;
}

TEST_F(DerivedStatsTest, ABuffsPartyHalfIsAWindowRatherThanAPassive) {
  Skill smoke = Smokescreen();
  std::map<std::string, Skill> skills = {{"smokescreen", smoke}};
  CharacterInstance caster = MakeCharacter(rng_, 100, 0);
  ASSERT_TRUE(caster.LearnSkill(smoke, 10));
  CharacterInstance plain = MakeCharacter(rng_, 100, 0);
  std::vector<CharacterInstance> party = PartyOf(std::move(caster));

  // Nothing permanent, either way round: the caster's own half is a buff too.
  EXPECT_DOUBLE_EQ(DerivedStatsFor(plain, skills, {}, party).damage_taken_pct,
                   0.0);
  std::vector<AllyGrant> buffs = AllyBuffsFor(plain, skills, party);
  ASSERT_EQ(buffs.size(), 1u);
  EXPECT_EQ(buffs[0].skill->name(), "Smokescreen");
  EXPECT_EQ(buffs[0].level, 10);
}

// The two rules that thin a permanent grant thin a buff's window the same
// way: a Shadower takes nothing from the Shadower beside them, and nobody
// takes anything from an ally who never learned it.
TEST_F(DerivedStatsTest, APartyBuffReachesOnlyWhoeverLacksTheSkill) {
  Skill smoke = Smokescreen();
  std::map<std::string, Skill> skills = {{"smokescreen", smoke}};
  CharacterInstance plain = MakeCharacter(rng_, 100, 0);
  CharacterInstance holder = MakeCharacter(rng_, 100, 0);
  ASSERT_TRUE(holder.LearnSkill(smoke, 10));
  CharacterInstance caster = MakeCharacter(rng_, 100, 0);
  ASSERT_TRUE(caster.LearnSkill(smoke, 10));
  std::vector<CharacterInstance> party = PartyOf(std::move(caster));

  EXPECT_TRUE(AllyBuffsFor(plain, skills, {}).empty());
  // An ally who never learned it holds nothing over anybody.
  EXPECT_TRUE(AllyBuffsFor(plain, skills, PartyOf(MakeCharacter(rng_, 100, 0)))
                  .empty());
  EXPECT_TRUE(AllyBuffsFor(holder, skills, party).empty());
  EXPECT_EQ(AllyBuffsFor(plain, skills, party).size(), 1u);
}

// Angel Ray's shape: an attack's own recovery leaves with the swing, but the
// share it hands the party is a passive and has to survive that stripping.
TEST_F(DerivedStatsTest, AnAttacksAllyHalfPaysAsAPassive) {
  Skill ray = Bless();
  ray.set_name("Angel Ray");
  ray.set_kind(SKILL_KIND_ATTACK);
  ray.mutable_base()->set_hp_recover_pct(0.08);
  ray.mutable_ally_base()->set_hp_recover_pct(0.08);
  ray.clear_per_level();
  ray.clear_ally_per_level();
  std::map<std::string, Skill> skills = {{"ray", ray}};
  CharacterInstance caster = MakeCharacter(rng_, 100, 0);
  ASSERT_TRUE(caster.LearnSkill(ray, 10));
  CharacterInstance plain = MakeCharacter(rng_, 100, 0);

  EXPECT_DOUBLE_EQ(DerivedStatsFor(caster, skills).hp_recover_pct, 0.0);
  std::vector<CharacterInstance> party = PartyOf(std::move(caster));
  EXPECT_DOUBLE_EQ(DerivedStatsFor(plain, skills, {}, party).hp_recover_pct,
                   0.08);
}

// Hex of the Evil Eye's shape: the party gets half of what the caster does.
TEST_F(DerivedStatsTest, AllyHalfNeedNotMatchTheCastersOwn) {
  Skill hex = Bless();
  hex.set_name("Hex of the Evil Eye");
  hex.mutable_ally_base()->set_attack(3);
  hex.mutable_ally_per_level()->set_attack(0);
  std::map<std::string, Skill> skills = {{"hex", hex}};
  CharacterInstance caster = MakeCharacter(rng_, 100, 0);
  ASSERT_TRUE(caster.LearnSkill(hex, 10));
  CharacterInstance plain = MakeCharacter(rng_, 100, 0);

  EXPECT_EQ(DerivedStatsFor(caster, skills).skill_stats.attack(), 15);
  std::vector<CharacterInstance> party = PartyOf(std::move(caster));
  EXPECT_EQ(DerivedStatsFor(plain, skills, {}, party).skill_stats.attack(), 3);
}

// Parashock Guard's shape: the caster is paid for shielding somebody, so alone
// they are paid nothing.
TEST_F(DerivedStatsTest, RequiresPartyGrantsNothingAlone) {
  Skill guard = Bless();
  guard.set_name("Parashock Guard");
  guard.set_requires_party(true);
  std::map<std::string, Skill> skills = {{"guard", guard}};
  CharacterInstance caster = MakeCharacter(rng_, 100, 0);
  ASSERT_TRUE(caster.LearnSkill(guard, 10));

  EXPECT_EQ(DerivedStatsFor(caster, skills).skill_stats.attack(), 0);
  std::vector<CharacterInstance> party = PartyOf(MakeCharacter(rng_, 100, 0));
  EXPECT_EQ(DerivedStatsFor(caster, skills, {}, party).skill_stats.attack(),
            15);
}

// What one ally supersedes, the whole party loses -- a Bishop's Advanced
// Blessing puts out the Cleric's Bless standing beside it.
TEST_F(DerivedStatsTest, AnAllysSupersessionReachesTheWholeParty) {
  Skill bless = Bless();
  Skill advanced = Bless();
  advanced.set_name("Advanced Blessing");
  advanced.set_supersedes_skill_name("Bless");
  advanced.mutable_base()->set_attack(21);
  advanced.mutable_ally_base()->set_attack(21);
  std::map<std::string, Skill> skills = {{"bless", bless},
                                         {"advanced", advanced}};
  CharacterInstance cleric = MakeCharacter(rng_, 100, 0);
  ASSERT_TRUE(cleric.LearnSkill(bless, 10));
  CharacterInstance bishop = MakeCharacter(rng_, 100, 0);
  ASSERT_TRUE(bishop.LearnSkill(advanced, 10));

  std::vector<CharacterInstance> party;
  party.push_back(std::move(cleric));
  party.push_back(std::move(bishop));
  CharacterInstance plain = MakeCharacter(rng_, 100, 0);
  EXPECT_EQ(DerivedStatsFor(plain, skills, {}, party).skill_stats.attack(), 30)
      << "the Bishop's alone, the Cleric's put out";
}

// An ally hands out what their own book left standing: a Bishop's Advanced
// Blessing rather than the Bless underneath it, however many levels they
// bought in the older skill.
TEST_F(DerivedStatsTest, AnAllyHandsOutOnlyWhatTheirOwnBookLeftStanding) {
  Skill bless = Bless();
  Skill advanced = Bless();
  advanced.set_name("Advanced Blessing");
  advanced.set_supersedes_skill_name("Bless");
  advanced.mutable_base()->set_attack(21);
  advanced.mutable_ally_base()->set_attack(21);
  std::map<std::string, Skill> skills = {{"bless", bless},
                                         {"advanced", advanced}};
  CharacterInstance bishop = MakeCharacter(rng_, 100, 0);
  ASSERT_TRUE(bishop.LearnSkill(bless, 10));
  ASSERT_TRUE(bishop.LearnSkill(advanced, 10));

  std::vector<CharacterInstance> party = PartyOf(std::move(bishop));
  CharacterInstance plain = MakeCharacter(rng_, 100, 0);
  EXPECT_EQ(DerivedStatsFor(plain, skills, {}, party).skill_stats.attack(), 30)
      << "the Advanced Blessing alone, not both";
}

// A group is settled over the party too: an archer's Sharp Eyes and the Decent
// one the character bought are two sources of one lever, wherever they stand.
TEST_F(DerivedStatsTest, AnAllysGrantJoinsTheGroupRatherThanAddingToIt) {
  Skill sharp = SharpEyes();
  Skill decent = DecentSharpEyes();
  std::map<std::string, Skill> skills = {{"sharp_eyes", sharp},
                                         {"decent", decent}};
  CharacterInstance archer = MakeCharacter(rng_, 100, 0);
  ASSERT_TRUE(archer.LearnSkill(sharp, 20));
  std::vector<CharacterInstance> party = PartyOf(std::move(archer));

  CharacterInstance c = MakeCharacter(rng_, 100, 0);
  ASSERT_TRUE(c.LearnSkill(decent, 1));
  DerivedStats stats = DerivedStatsFor(c, skills, {}, party);
  EXPECT_NEAR(stats.crit_rate, 0.20, 1e-9) << "the archer's alone";
  EXPECT_EQ(stats.skill_stats.luk(), 6) << "the Decent's own half stands";
}

// --- Toggle skills ---

// The switch, and the form it raises in Bless's place. Both are written onto
// the Swordman's book so one character can hold the pair.
Skill Toggle() {
  Skill skill;
  skill.set_name("Righteously Indignant");
  skill.set_kind(SKILL_KIND_ACTIVE);
  skill.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  skill.set_max_level(1);
  skill.set_toggle(true);
  skill.mutable_base()->set_magic_attack(50);
  return skill;
}

Skill VengeanceForm() {
  Skill skill = Bless();
  skill.set_name("Angelic Wrath");
  skill.set_replaces_skill_name("Bless");
  skill.set_toggle_skill_name("Righteously Indignant");
  skill.mutable_base()->set_attack(100);
  skill.clear_per_level();
  skill.clear_ally_base();
  skill.clear_ally_per_level();
  return skill;
}

class ToggleTest : public DerivedStatsTest {};

TEST_F(ToggleTest, TheSwitchDecidesWhichFormPays) {
  std::map<std::string, Skill> skills = {
      {"bless", Bless()}, {"toggle", Toggle()}, {"form", VengeanceForm()}};
  CharacterInstance c = MakeCharacter(rng_, 100, 0);
  ASSERT_TRUE(c.LearnSkill(Bless(), 10));
  ASSERT_TRUE(c.LearnSkill(Toggle()));

  EXPECT_EQ(DerivedStatsFor(c, skills).skill_stats.attack(), 15)
      << "switched off, the Benevolence skill is the one standing";
  ASSERT_TRUE(c.ToggleSkill(Toggle()));
  EXPECT_EQ(DerivedStatsFor(c, skills).skill_stats.attack(), 100)
      << "and switched on, only the form";
}

// The grant on the switch itself is not gated on it: GMS marks such a grant
// "[Passive Effects]", which is permanent once the skill is learned.
TEST_F(ToggleTest, WhatTheSwitchItselfGrantsIsPermanent) {
  std::map<std::string, Skill> skills = {{"toggle", Toggle()}};
  CharacterInstance c = MakeCharacter(rng_, 100, 0);
  ASSERT_TRUE(c.LearnSkill(Toggle()));
  EXPECT_EQ(DerivedStatsFor(c, skills).skill_stats.magic_attack(), 50);
  ASSERT_TRUE(c.ToggleSkill(Toggle()));
  EXPECT_EQ(DerivedStatsFor(c, skills).skill_stats.magic_attack(), 50);
}

// A character who never bought the switch has it off, so every form in the
// catalog sleeps for them -- and the ally half of one never reaches a party.
TEST_F(ToggleTest, AFormSleepsForWhoeverLacksTheSwitch) {
  std::map<std::string, Skill> skills = {{"bless", Bless()},
                                         {"form", VengeanceForm()}};
  CharacterInstance c = MakeCharacter(rng_, 100, 0);
  ASSERT_TRUE(c.LearnSkill(Bless(), 10));
  EXPECT_EQ(DerivedStatsFor(c, skills).skill_stats.attack(), 15);
}

// Blessed Ensemble is not a buff: it pays for the company kept, so two allies
// holding it both pay.
TEST_F(DerivedStatsTest, AStackingGrantPaysOncePerAlly) {
  Skill ensemble;
  ensemble.set_name("Blessed Ensemble");
  ensemble.set_kind(SKILL_KIND_PASSIVE);
  ensemble.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  ensemble.set_max_level(10);
  ensemble.set_ally_effect_stacks(true);
  ensemble.mutable_ally_base()->set_exp_pct(0.02);
  ensemble.mutable_ally_per_level()->set_exp_pct(0.02);
  std::map<std::string, Skill> skills = {{"ensemble", ensemble}};

  std::vector<CharacterInstance> party;
  for (int i = 0; i < 2; ++i) {
    CharacterInstance cleric = MakeCharacter(rng_, 100, 0);
    ASSERT_TRUE(cleric.LearnSkill(ensemble, 10));
    party.push_back(std::move(cleric));
  }
  CharacterInstance third = MakeCharacter(rng_, 100, 0);
  ASSERT_TRUE(third.LearnSkill(ensemble, 10));
  EXPECT_NEAR(DerivedStatsFor(third, skills, {}, party).exp_pct, 0.40, 1e-9)
      << "their own pays them nothing; the two beside them pay 20% each";
}

// A stacking grant answers to neither rule that thins the rest: the Bishop
// replacing their own Ensemble with Harmony still pays, and so does the Cleric
// beside them whose Ensemble the Bishop's book never touched.
TEST_F(DerivedStatsTest, AStackingGrantIsNotThinnedBySupersession) {
  Skill ensemble;
  ensemble.set_name("Blessed Ensemble");
  ensemble.set_kind(SKILL_KIND_PASSIVE);
  ensemble.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  ensemble.set_max_level(10);
  ensemble.set_ally_effect_stacks(true);
  ensemble.mutable_ally_base()->set_exp_pct(0.20);
  Skill harmony = ensemble;
  harmony.set_name("Blessed Harmony");
  harmony.set_supersedes_skill_name("Blessed Ensemble");
  std::map<std::string, Skill> skills = {{"ensemble", ensemble},
                                         {"harmony", harmony}};

  CharacterInstance bishop = MakeCharacter(rng_, 100, 0);
  ASSERT_TRUE(bishop.LearnSkill(ensemble, 1));
  ASSERT_TRUE(bishop.LearnSkill(harmony, 1));
  CharacterInstance cleric = MakeCharacter(rng_, 100, 0);
  ASSERT_TRUE(cleric.LearnSkill(ensemble, 1));
  std::vector<CharacterInstance> party;
  party.push_back(std::move(bishop));
  party.push_back(std::move(cleric));

  CharacterInstance plain = MakeCharacter(rng_, 100, 0);
  EXPECT_NEAR(DerivedStatsFor(plain, skills, {}, party).exp_pct, 0.40, 1e-9)
      << "the Bishop pays once, not twice, and the Cleric still pays";
}

// Two allies with the same buff are one buff, at the better of the two levels.
TEST_F(DerivedStatsTest, TheBetterOfTwoAlliesGrantsStands) {
  Skill bless = Bless();
  std::map<std::string, Skill> skills = {{"bless", bless}};
  std::vector<CharacterInstance> party;
  for (int level : {4, 10}) {
    CharacterInstance cleric = MakeCharacter(rng_, 100, 0);
    ASSERT_TRUE(cleric.LearnSkill(bless, level));
    party.push_back(std::move(cleric));
  }
  CharacterInstance plain = MakeCharacter(rng_, 100, 0);
  EXPECT_EQ(DerivedStatsFor(plain, skills, {}, party).skill_stats.attack(), 15);
}

// Combat Orders reaches the party the same way, and the ally's own copy is
// what sets how many levels they hand out.
TEST_F(DerivedStatsTest, AnAllysCombatOrdersRaisesTheSkillsItReaches) {
  Skill iron_body = IronBody();
  Skill orders = CombatOrders();
  *orders.mutable_ally_base() = orders.base();
  *orders.mutable_ally_per_level() = orders.per_level();
  std::map<std::string, Skill> skills = {{"iron_body", iron_body},
                                         {"combat_orders", orders}};
  CharacterInstance knight = MakeCharacter(rng_, 100, 0);
  ASSERT_TRUE(knight.LearnSkill(orders, 10));
  std::vector<CharacterInstance> party = PartyOf(std::move(knight));

  CharacterInstance plain = MakeCharacter(rng_, 100, 0);
  ASSERT_TRUE(plain.LearnSkill(iron_body, 18));
  EXPECT_EQ(BonusSkillLevels(plain, skills, party), 2);
  EXPECT_EQ(DerivedStatsFor(plain, skills, {}, party).def, 200);

  // A White Knight keeps their own rather than adding the party's.
  CharacterInstance other = MakeCharacter(rng_, 100, 0);
  ASSERT_TRUE(other.LearnSkill(orders, 1));
  EXPECT_EQ(BonusSkillLevels(other, skills, party), 1);
}

// --- potential ---

// Equips a ring carrying `lines`, all at `rank`, on an item of `item_level`.
void EquipPotentialRing(CharacterInstance& character, int item_level,
                        PotentialRank rank,
                        const std::vector<PotentialLineType>& lines) {
  EquipPrototype ring;
  ring.set_name("Potted Ring");
  // A slot of its own, so a test can wear a plain stat item beside it.
  ring.set_equip_slot(EQUIP_SLOT_HAT);
  ring.set_required_level(item_level);
  Equip state;
  Potential* potential = state.mutable_main_potential();
  potential->set_rank(rank);
  for (PotentialLineType type : lines) {
    PotentialLine* line = potential->add_lines();
    line->set_type(type);
    line->set_rank(rank);
  }
  character.PickUp(std::make_unique<EquipInstance>(ring, state));
  character.Equip(character.inventory().size() - 1);
}

TEST(PotentialStatsTest, FlatLinesLandLikeAWornStat) {
  std::mt19937 rng(1);
  CharacterInstance c = MakeStatCharacter(rng, 100, 0, 0, 0);
  EquipPotentialRing(c, 100, POTENTIAL_RANK_RARE,
                     {POTENTIAL_LINE_TYPE_STR, POTENTIAL_LINE_TYPE_ALL_STATS});
  DerivedStats stats = DerivedStatsFor(c, {});
  // +12 STR from the STR line and +5 from All Stats, on a level 100 item.
  EXPECT_EQ(stats.skill_stats.str(), 17);
  EXPECT_EQ(stats.skill_stats.dex(), 5);
}

// GMS's rule, and deliberately not Maple Warrior's: a potential's %stat reads
// the AP pool and everything worn, its own flat lines included.
TEST(PotentialStatsTest, PercentStatReadsTheApPoolAndTheGear) {
  std::mt19937 rng(1);
  CharacterInstance c = MakeStatCharacter(rng, 1000, 0, 0, 0);
  EquipStrRing(c, 100);
  EquipPotentialRing(c, 100, POTENTIAL_RANK_LEGENDARY,
                     {POTENTIAL_LINE_TYPE_STR_PCT});
  DerivedStats stats = DerivedStatsFor(c, {});
  // 12% of 1000 AP plus the ring's 100.
  EXPECT_EQ(stats.skill_stats.str(), 132);
}

// The rule [[final-stats]] rests on: a symbol's stat is final, so a %stat
// line may not multiply it, even though it is worn like anything else.
TEST(PotentialStatsTest, PercentStatSkipsWhatASymbolGrants) {
  std::mt19937 rng(1);
  CharacterInstance c = MakeStatCharacter(rng, 1000, 0, 0, 0);
  EquipPrototype symbol;
  symbol.set_name("Symbol");
  symbol.set_equip_slot(EQUIP_SLOT_SYMBOL_VANISHING_JOURNEY);
  symbol.mutable_arcane_symbol()->set_area_level(200);
  c.PickUp(std::make_unique<EquipInstance>(symbol));
  c.Equip(c.inventory().size() - 1);
  ASSERT_GT(c.symbol_stats().str(), 0);
  EquipPotentialRing(c, 100, POTENTIAL_RANK_LEGENDARY,
                     {POTENTIAL_LINE_TYPE_STR_PCT});
  DerivedStats stats = DerivedStatsFor(c, {});
  EXPECT_EQ(stats.skill_stats.str(), 120);
}

TEST(PotentialStatsTest, AllStatsPercentPaysEveryStat) {
  std::mt19937 rng(1);
  CharacterInstance c = MakeStatCharacter(rng, 1000, 1000, 1000, 1000);
  EquipPotentialRing(c, 100, POTENTIAL_RANK_LEGENDARY,
                     {POTENTIAL_LINE_TYPE_ALL_STATS_PCT});
  DerivedStats stats = DerivedStatsFor(c, {});
  // All Stats % pays a rank down: 9% at Legendary on a level 100 item.
  EXPECT_EQ(stats.skill_stats.str(), 90);
  EXPECT_EQ(stats.skill_stats.dex(), 90);
  EXPECT_EQ(stats.skill_stats.int_(), 90);
  EXPECT_EQ(stats.skill_stats.luk(), 90);
}

// The two attacks are apart, which they are nowhere else: a %ATT line on a
// staff is worth nothing, and a skill granting attack_pct still pays both.
TEST(PotentialStatsTest, AttackAndMagicAttackPercentAreSeparate) {
  std::mt19937 rng(1);
  CharacterInstance c = MakeStatCharacter(rng, 0, 0, 0, 0);
  EquipPotentialRing(c, 100, POTENTIAL_RANK_LEGENDARY,
                     {POTENTIAL_LINE_TYPE_ATTACK_PCT});
  DerivedStats stats = DerivedStatsFor(c, {});
  EXPECT_DOUBLE_EQ(stats.attack_pct, 0.12);
  EXPECT_DOUBLE_EQ(stats.magic_attack_pct, 0.0);
}

TEST(PotentialStatsTest, TheDamageLeversLandWhereTheirOwnSourcesDo) {
  std::mt19937 rng(1);
  CharacterInstance c = MakeStatCharacter(rng, 0, 0, 0, 0);
  EquipPotentialRing(
      c, 150, POTENTIAL_RANK_LEGENDARY,
      {POTENTIAL_LINE_TYPE_DAMAGE_PCT, POTENTIAL_LINE_TYPE_BOSS_DAMAGE_40,
       POTENTIAL_LINE_TYPE_CRIT_DAMAGE_PCT});
  DerivedStats stats = DerivedStatsFor(c, {});
  EXPECT_DOUBLE_EQ(stats.damage_pct, 0.12);
  EXPECT_DOUBLE_EQ(stats.boss_pct, 0.40);
  EXPECT_DOUBLE_EQ(stats.crit_dmg, 0.08);
}

// Two ignored-defence lines meet in reverse, the way every pair of them does:
// 35% and 40% leave 39% of the defence standing, not 25%.
TEST(PotentialStatsTest, TwoIgnoredDefenceLinesMeetInReverse) {
  std::mt19937 rng(1);
  CharacterInstance c = MakeStatCharacter(rng, 0, 0, 0, 0);
  EquipPotentialRing(c, 150, POTENTIAL_RANK_LEGENDARY,
                     {POTENTIAL_LINE_TYPE_IGNORE_DEFENSE_35,
                      POTENTIAL_LINE_TYPE_IGNORE_DEFENSE_40});
  DerivedStats stats = DerivedStatsFor(c, {});
  EXPECT_NEAR(stats.ied, 1.0 - 0.65 * 0.60, 1e-9);
}

TEST(PotentialStatsTest, MesoTakesTheWornCapAndDropDoesNot) {
  std::mt19937 rng(1);
  CharacterInstance c = MakeStatCharacter(rng, 0, 0, 0, 0);
  EquipPotentialRing(
      c, 150, POTENTIAL_RANK_LEGENDARY,
      {POTENTIAL_LINE_TYPE_MESO_RATE, POTENTIAL_LINE_TYPE_ITEM_DROP_RATE});
  DerivedStats stats = DerivedStatsFor(c, {});
  EXPECT_DOUBLE_EQ(stats.equip_meso_pct, 0.20);
  EXPECT_DOUBLE_EQ(stats.meso_pct, 0.0);
  EXPECT_DOUBLE_EQ(stats.item_drop_pct, 0.20);
}

// The pile a %stat line multiplies is read back out of the finished stats, so
// a caller pricing a potential the character is not wearing gets the same
// answer the fold would give -- and the worn one's own share is taken off
// first, rather than being multiplied a second time.
TEST(PotentialStatsTest, StatGrantPricesAPotentialTheCharacterIsNotWearing) {
  std::mt19937 rng(1);
  CharacterInstance c = MakeStatCharacter(rng, 1000, 0, 0, 0);
  EquipPotentialRing(c, 150, POTENTIAL_RANK_UNIQUE,
                     {POTENTIAL_LINE_TYPE_STR_PCT});
  DerivedStats stats = DerivedStatsFor(c, {});
  // 9% of the 1000 AP, and nothing else is worn.
  EXPECT_EQ(stats.potential_stats.str(), 90);
  EXPECT_EQ(PotentialStatGrant(c, stats, c.potential_totals()).str(), 90);

  // A Legendary line on the same item pays 12% of the same 1000, not of 1090.
  PotentialTotals better;
  better.str_pct = 0.12;
  EXPECT_EQ(PotentialStatGrant(c, stats, better).str(), 120);

  // Nothing at all takes the whole worn share back off.
  EXPECT_EQ(PotentialStatGrant(c, stats, PotentialTotals()).str(), 0);
}

TEST(PotentialStatsTest, ACooldownLineReachesTheCharacter) {
  std::mt19937 rng(1);
  CharacterInstance c = MakeStatCharacter(rng, 0, 0, 0, 0);
  EquipPotentialRing(
      c, 150, POTENTIAL_RANK_LEGENDARY,
      {POTENTIAL_LINE_TYPE_COOLDOWN_1, POTENTIAL_LINE_TYPE_COOLDOWN_2});
  DerivedStats stats = DerivedStatsFor(c, {});
  EXPECT_DOUBLE_EQ(stats.cooldown_reduction_seconds, 3.0);
}

}  // namespace
}  // namespace ms
