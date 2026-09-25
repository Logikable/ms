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
#include "src/character/skill_placement.h"
#include "src/character/v_matrix.h"
#include "src/item/equip_instance.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/skill.pb.h"
#include "src/testing/data_files.h"
#include "src/testing/prototypes.h"

namespace ms {
namespace {

// A level-`level` character with `hp` HP from AP and enough 1st-job SP to max
// anything the tests learn.
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

// Equips an armor item with `max_hp` and `def`. It uses the weapon slot, which
// none of these tests needs for anything else.
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

// Puts a weapon of `type` in the character's hand, for skills that check the
// weapon.
void EquipWeapon(CharacterInstance& character, EquipType type) {
  EquipPrototype weapon;
  weapon.set_name("Weapon");
  weapon.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  weapon.set_equip_type(type);
  character.PickUp(std::make_unique<EquipInstance>(weapon));
  character.Equip(character.inventory().size() - 1);
}

// A weapon with attack, so combat power has something to change.
void EquipAttackWeapon(CharacterInstance& character) {
  EquipPrototype weapon;
  weapon.set_name("Bow");
  weapon.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  weapon.mutable_base_stats()->set_attack(80);
  character.PickUp(std::make_unique<EquipInstance>(weapon));
  character.Equip(character.inventory().size() - 1);
}

// A character with the four primary stats set directly. These tests care about
// the stats, not how much AP it took.
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

// A ring with STR, for checking whether worn stats count.
void EquipStrRing(CharacterInstance& character, int str) {
  EquipPrototype ring;
  ring.set_name("Ring");
  ring.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  ring.mutable_base_stats()->set_str(str);
  character.PickUp(std::make_unique<EquipInstance>(ring));
  character.Equip(0);
}

// Critical Shot: +2% crit rate per level.
Skill CriticalShot() {
  Skill skill;
  skill.set_name("Critical Shot");
  skill.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(skill, JOB_ADVANCEMENT_SWORDMAN);
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
  PlaceIn(skill, JOB_ADVANCEMENT_SWORDMAN);
  skill.set_max_level(20);
  skill.mutable_base()->set_max_mp_pct(0.01);
  skill.mutable_base()->set_max_mp_per_level(25);
  skill.mutable_per_level()->set_max_mp_pct(0.01);
  skill.mutable_per_level()->set_max_mp_per_level(5);
  return skill;
}

// Shaped like Nimble Body (+1 LUK per level) but put in the warrior's book.
// These tests are about how a lever combines, not whose book it is in, and this
// character can't learn another job's skill.
Skill NimbleBody() {
  Skill skill;
  skill.set_name("Nimble Body");
  skill.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(skill, JOB_ADVANCEMENT_SWORDMAN);
  skill.set_max_level(20);
  skill.mutable_base()->set_luk(1);
  skill.mutable_per_level()->set_luk(1);
  return skill;
}

// Physical Training as the wiki states it: +6 STR and +6 DEX per level.
Skill PhysicalTraining() {
  Skill skill;
  skill.set_name("Physical Training");
  skill.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(skill, JOB_ADVANCEMENT_SWORDMAN);
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
  PlaceIn(skill, JOB_ADVANCEMENT_SWORDMAN);
  skill.set_max_level(10);
  skill.mutable_base()->set_mastery(0.14);
  skill.mutable_per_level()->set_mastery(0.04);
  return skill;
}

// Final Attack as the wiki states it for a Spearman: a 2*L% chance of an extra
// hit of 2 lines at (60+L)%.
Skill FinalAttack() {
  Skill skill;
  skill.set_name("Final Attack");
  skill.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(skill, JOB_ADVANCEMENT_SWORDMAN);
  skill.set_max_level(20);
  skill.mutable_base()->set_final_attack_chance(0.02);
  skill.mutable_base()->set_final_attack_pct(1.22);
  skill.mutable_per_level()->set_final_attack_chance(0.02);
  skill.mutable_per_level()->set_final_attack_pct(0.02);
  return skill;
}

// Shaped like Archery Mastery: +1 attack speed stage, flat at every level. Put
// in the warrior's book for the same reason as Nimble Body.
Skill ArcheryMastery() {
  Skill skill;
  skill.set_name("Archery Mastery");
  skill.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(skill, JOB_ADVANCEMENT_SWORDMAN);
  skill.set_max_level(15);
  skill.mutable_base()->set_attack_speed(1);
  return skill;
}

// Warrior Mastery, trimmed to the one lever we model: +(5 + L) HP per level.
Skill WarriorMastery() {
  Skill skill;
  skill.set_name("Warrior Mastery");
  skill.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(skill, JOB_ADVANCEMENT_SWORDMAN);
  skill.set_max_level(15);
  skill.mutable_base()->set_max_hp_per_level(6);
  skill.mutable_per_level()->set_max_hp_per_level(1);
  return skill;
}

// Advanced Blessing, trimmed to HP and MP: a flat grant, the same at character
// level 1 as at 140.
Skill AdvancedBlessing() {
  Skill skill;
  skill.set_name("Advanced Blessing");
  skill.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(skill, JOB_ADVANCEMENT_SWORDMAN);
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

// With autoswap on, the activity picks the gear as well as the allocations. A
// boss fight uses the boss preset's weapon, and farming never sees it.
TEST_F(DerivedStatsTest, TheActivityPicksTheGearPreset) {
  CharacterInstance c = MakeCharacter(rng_, 15, 50, /*mp=*/20);
  c.set_autoswap_presets(true);
  EquipArmor(c, 100, 30);
  EquipPrototype boss_armor;
  boss_armor.set_name("Boss Armor");
  boss_armor.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  boss_armor.mutable_base_stats()->set_max_hp(900);
  c.PickUp(std::make_unique<EquipInstance>(boss_armor));
  ASSERT_TRUE(c.Equip(c.inventory().size() - 1, StatPreset::kSecond));

  EXPECT_EQ(DerivedStatsFor(c, {}, {}, {}, Activity::kFarming).max_hp, 150);
  EXPECT_EQ(DerivedStatsFor(c, {}, {}, {}, Activity::kBossing).max_hp, 950);
}

// With autoswap off, one preset is used for both activities.
TEST_F(DerivedStatsTest, WithTheAutoswapOffOnePresetAnswersForBoth) {
  CharacterInstance c = MakeCharacter(rng_, 15, 50, /*mp=*/20);
  EquipArmor(c, 100, 30);
  EquipPrototype boss_armor;
  boss_armor.set_name("Boss Armor");
  boss_armor.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  boss_armor.mutable_base_stats()->set_max_hp(900);
  c.PickUp(std::make_unique<EquipInstance>(boss_armor));
  ASSERT_TRUE(c.Equip(c.inventory().size() - 1, StatPreset::kSecond));

  EXPECT_EQ(DerivedStatsFor(c, {}, {}, {}, Activity::kBossing).max_hp, 150);
  c.SetSlotInUse(PresetKind::kEquip, StatPreset::kSecond);
  EXPECT_EQ(DerivedStatsFor(c, {}, {}, {}, Activity::kFarming).max_hp, 950);
}

// A V Matrix node grants what it states like any other passive. It belongs to
// no advancement, so nothing may require one for it.
TEST_F(DerivedStatsTest, ACommonNodeGrantsWhatItStates) {
  CharacterInstance c = MakeCharacter(rng_, 15, 50, /*mp=*/20);
  Skill rope;
  rope.set_name("Rope Lift");
  rope.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(rope, JOB_ADVANCEMENT_COMMON);
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

// A node whose ladder rises every fifth level rather than every level, like
// GMS's Decent nodes. The fraction is floored when read, so the character sees
// whole points at the levels GMS raises it.
TEST_F(DerivedStatsTest, AFractionalLadderStepsEveryFifthLevel) {
  Skill door;
  door.set_name("Decent Mystic Door");
  door.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(door, JOB_ADVANCEMENT_COMMON);
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

// A four-piece set with tiers at three and four pieces. Its levers include flat
// stats, attack, and a percentage of the HP pool.
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
  // The fifth slot names a family rather than an item, since a weapon belongs
  // to one class and the set can't say which.
  EquipSetMember* weapon = set.add_members();
  weapon->set_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  weapon->set_family("Frozen Weapon");
  EquipSetTier* five = set.add_tiers();
  five->set_pieces(5);
  five->mutable_effect()->set_attack(11);
  return {{"frozen", set}};
}

// Wears the `index`-th piece of the set, in that piece's slot.
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

// Taking a piece off removes the tier: the bonus follows what is currently
// worn.
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

// Any item of the family fills the slot the set names that family for. An item
// with no family fills nothing, which keeps an ordinary weapon out of a set.
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

// A slot with an alternate from a boss drop lists both, and either fills it. It
// never counts twice, since both use the same slot.
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

// Gear outside the set doesn't count toward it, however much is worn.
TEST_F(DerivedStatsTest, OtherGearDoesNotCountTowardASet) {
  CharacterInstance c = MakeCharacter(rng_, 15, 1000);
  c.UseEquipSets(FrozenSet());
  WearFrozen(c, 2);
  EquipArmor(c, 100, 30);

  EXPECT_EQ(DerivedStatsFor(c, {}).skill_stats.str(), 0);
}

// A character who was never given the set definitions gets no bonuses. Every
// test and sim that ignores sets relies on this.
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

  // Flat first: 50 allocated + (25 + 5*19) * 15 levels = 1850, then the skill's
  // +20% on the total.
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
  // The percentage also applies to the pendant's flat HP, not just the base
  // pool.
  EXPECT_EQ(stats.max_hp, 330);
  EXPECT_EQ(stats.max_mp, 110);
}

// Both shares apply to the same total instead of compounding: 20% on 100 is
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

// Freezing Crush, the base of the mechanism: a cap, and what one stack is
// worth.
Skill FreezingCrush() {
  Skill skill;
  skill.set_name("Freezing Crush");
  skill.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(skill, JOB_ADVANCEMENT_SWORDMAN);
  skill.set_max_level(1);
  skill.set_freeze_stack_cap(5);
  skill.mutable_base()->set_crit_dmg_per_freeze_stack(0.01);
  return skill;
}

// Glacial Fury: a buff that raises another skill's cap and pays per stack.
Skill GlacialFury() {
  Skill skill;
  skill.set_name("Glacial Fury");
  skill.set_kind(SKILL_KIND_ACTIVE);
  PlaceIn(skill, JOB_ADVANCEMENT_SWORDMAN);
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

  const BuffUp up[] = {{&fury}};
  DerivedStats standing = DerivedStatsFor(c, skills, absl::MakeConstSpan(up));
  EXPECT_EQ(standing.freeze.cap, 13);
  EXPECT_EQ(standing.freeze.matt_per_stack, 5);
}

// The buff raises a cap but doesn't create one. A character who never learned
// Freezing Crush has no stacks for it to raise or pay for.
TEST_F(DerivedStatsTest, TheCapBonusGrantsNothingWithoutACap) {
  CharacterInstance c = MakeCharacter(rng_, 15, 0);
  Skill fury = GlacialFury();
  std::map<std::string, Skill> skills = {{"glacial_fury", fury}};
  ASSERT_TRUE(c.LearnSkill(fury, 1));

  const BuffUp up[] = {{&fury}};
  DerivedStats stats = DerivedStatsFor(c, skills, absl::MakeConstSpan(up));
  EXPECT_EQ(stats.freeze.cap, 0);
  EXPECT_EQ(stats.freeze.matt_per_stack, 0);
}

// Magic Guard as the data states it: 22% of a hit to MP, plus 7% per level.
Skill MagicGuard() {
  Skill skill;
  skill.set_name("Magic Guard");
  skill.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(skill, JOB_ADVANCEMENT_SWORDMAN);
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

  // 0.85 and 0.10 summed would cancel almost everything; multiplied, they leave
  // 0.15 * 0.90 of the hit.
  DerivedStats stats = DerivedStatsFor(c, skills);
  EXPECT_NEAR(stats.damage_taken_pct, 1.0 - 0.15 * 0.90, 1e-9);
}

// Shaped like Evasion Boost: 12% dodge at level 1, plus 2 points per level.
Skill EvasionBoost() {
  Skill skill;
  skill.set_name("Evasion Boost");
  skill.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(skill, JOB_ADVANCEMENT_SWORDMAN);
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

  // Dodging isn't reduction: it cancels whole hits rather than part of each,
  // and the fight receives them in separate fields.
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

  // 30% and 30% summed would be 60%; what gets through is 0.7 * 0.7.
  DerivedStats stats = DerivedStatsFor(c, skills);
  EXPECT_NEAR(stats.dodge_chance, 1.0 - 0.70 * 0.70, 1e-9);
}

// Shaped like Frailty Curse: 11% off the monster's attack at level 1, plus a
// point per level.
Skill Barrier() {
  Skill skill;
  skill.set_name("Frailty Curse");
  skill.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(skill, JOB_ADVANCEMENT_SWORDMAN);
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

  // These are points on one number, so they add up, unlike reduction and dodge.
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
  // then Iron Body's +10% on the total. Applying the percentage to any one
  // source would come out short.
  DerivedStats stats = DerivedStatsFor(c, skills);
  EXPECT_EQ(stats.max_hp, 1089);
  // The MP half of the same grant, with no percentage on it.
  EXPECT_EQ(stats.max_mp, 750);
}

TEST_F(DerivedStatsTest, PercentHpSurvivesItsOwnAccumulation) {
  CharacterInstance c = MakeCharacter(rng_, 15, 50);
  Skill iron_body = IronBody();
  std::map<std::string, Skill> skills = {{"iron_body", iron_body}};
  ASSERT_TRUE(c.LearnSkill(iron_body, 16));

  // 16 levels of +1% sum to slightly under 0.16 in floating point. Flooring
  // that directly would give 57 for what is really 50 * 1.16.
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

  // The bonus is +1 at every level, so even a maxed skill adds one stage.
  DerivedStats stats = DerivedStatsFor(c, skills);
  EXPECT_EQ(stats.attack_speed_bonus, 1);
}

// A skill's stages go to one of two fields, never both. The capped field holds
// the weapon and book stages, and only the other one can pass the cap.
TEST_F(DerivedStatsTest, StagesThatPassTheCapAreCountedApart) {
  CharacterInstance c = MakeCharacter(rng_, 15, 50);
  Skill mastery = ArcheryMastery();
  Skill infusion = ArcheryMastery();
  infusion.set_name("Decent Speed Infusion");
  infusion.mutable_base()->clear_attack_speed();
  infusion.mutable_base()->set_uncapped_attack_speed(1);
  std::map<std::string, Skill> skills = {{"archery_mastery", mastery},
                                         {"infusion", infusion}};
  ASSERT_TRUE(c.LearnSkill(mastery, 15));
  ASSERT_TRUE(c.LearnSkill(infusion, 1));

  DerivedStats stats = DerivedStatsFor(c, skills);
  EXPECT_EQ(stats.attack_speed_bonus, 1);
  EXPECT_EQ(stats.uncapped_attack_speed_bonus, 1);
  // A character already at the cap keeps the second stage and not the first.
  EXPECT_EQ(AttackSpeedStage(kAttackSpeedSoftCap, stats.attack_speed_bonus,
                             stats.uncapped_attack_speed_bonus),
            kAttackSpeedSoftCap + 1);
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

// STR from a skill gives as much base DEF as STR from AP. That is why base DEF
// is computed from total stats rather than the allocation.
TEST_F(DerivedStatsTest, SkillGrantedStrBuysBaseDefLikeAnyOtherStr) {
  CharacterInstance c = MakeCharacter(rng_, 40, 50);
  Skill training = PhysicalTraining();
  std::map<std::string, Skill> skills = {{"physical_training", training}};
  int bare = DerivedStatsFor(c, skills).def;
  ASSERT_TRUE(c.LearnSkill(training, 5));

  // 30 STR at 1.5 DEF each and 30 DEX at 0.4.
  EXPECT_EQ(DerivedStatsFor(c, skills).def, bare + 45 + 12);
}

TEST_F(DerivedStatsTest, WeaponMasteryReachesTheDerivedStats) {
  CharacterInstance c = MakeCharacter(rng_, 40, 50);
  Skill mastery = WeaponMastery();
  std::map<std::string, Skill> skills = {{"weapon_mastery", mastery}};
  ASSERT_TRUE(c.LearnSkill(mastery, 10));

  EXPECT_DOUBLE_EQ(DerivedStatsFor(c, skills).mastery, 0.50);  // 10 + 4*10 %
}

// Two masteries use the better one, not the sum. Every other lever here adds
// up.
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

// The chance and the damage stay separate throughout, because the fight rolls
// one and pays the other.
TEST_F(DerivedStatsTest, FinalAttackKeepsItsChanceAndItsDamageApart) {
  CharacterInstance c = MakeCharacter(rng_, 40, 50);
  Skill final_attack = FinalAttack();
  std::map<std::string, Skill> skills = {{"final_attack", final_attack}};
  ASSERT_TRUE(c.LearnSkill(final_attack, 20));

  // A 40% chance of an extra hit worth 160%. It names no tag, so every swing
  // triggers it, which suits a Final Attack gated on the weapon in hand.
  DerivedStats stats = DerivedStatsFor(c, skills);
  ASSERT_EQ(stats.final_attacks.size(), 1u);
  EXPECT_NEAR(stats.final_attacks[0].chance, 0.40, 1e-9);
  EXPECT_NEAR(stats.final_attacks[0].damage_pct, 1.60, 1e-9);
  EXPECT_EQ(stats.final_attacks[0].required_tag, SKILL_TAG_UNSPECIFIED);
  // A source that doesn't state its hit count lands one hit.
  EXPECT_EQ(stats.final_attacks[0].lines, 1);

  // Three hits at 160% each, where the line above is one at 160%. The damage
  // stays per hit; only the count changed.
  final_attack.mutable_base()->set_final_attack_lines(3);
  skills["final_attack"] = final_attack;
  stats = DerivedStatsFor(c, skills);
  ASSERT_EQ(stats.final_attacks.size(), 1u);
  EXPECT_NEAR(stats.final_attacks[0].damage_pct, 1.60, 1e-9);
  EXPECT_EQ(stats.final_attacks[0].lines, 3);
}

// A boost aimed at a passive has no swing to apply to, but a passive with a
// Final Attack has its extra hit to boost. The Hero's two hypers do exactly
// this.
TEST_F(DerivedStatsTest, ABoostReachesTheFinalAttackOfThePassiveItNames) {
  CharacterInstance c = MakeCharacter(rng_, 140, 50);
  Skill final_attack = FinalAttack();
  Skill hyper;
  hyper.set_name("Advanced Final Attack - Opportunity");
  hyper.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(hyper, JOB_ADVANCEMENT_SWORDMAN);
  hyper.set_max_level(1);
  SkillBoost* boost = hyper.add_boost();
  boost->set_skill_name("Final Attack");
  boost->mutable_effect()->set_final_attack_chance(0.15);
  boost->mutable_effect()->set_damage_pct(0.10);
  std::map<std::string, Skill> skills = {{"final_attack", final_attack},
                                         {"hyper", hyper}};
  ASSERT_TRUE(c.LearnSkill(final_attack, 20));
  ASSERT_TRUE(c.LearnSkill(hyper, 1));

  // The chance goes from 40% to 55%, and only the extra hits get the 10%
  // damage. The multiplier doesn't change, because the boost's damage is
  // separate from the skill's own.
  DerivedStats stats = DerivedStatsFor(c, skills);
  ASSERT_EQ(stats.final_attacks.size(), 1u);
  EXPECT_NEAR(stats.final_attacks[0].chance, 0.55, 1e-9);
  EXPECT_NEAR(stats.final_attacks[0].damage_bonus_pct, 0.10, 1e-9);
  EXPECT_NEAR(stats.final_attacks[0].damage_pct, 1.60, 1e-9);
  EXPECT_NEAR(stats.damage_pct, 0.0, 1e-9);
}

// A buff's boost is the skill's boost while the buff is up: it reaches the
// named skill only in stats built with that buff active. Storm of Arrows is the
// reason it exists. GMS doubles Magic Arrow's chance only during the storm, and
// the doubling may go past 100%.
TEST_F(DerivedStatsTest, ABuffsBoostReachesTheNamedSkillOnlyWhileItStands) {
  CharacterInstance c = MakeCharacter(rng_, 140, 50);
  Skill final_attack = FinalAttack();
  Skill storm;
  storm.set_name("Storm of Arrows");
  storm.set_kind(SKILL_KIND_ACTIVE);
  PlaceIn(storm, JOB_ADVANCEMENT_SWORDMAN);
  storm.set_max_level(1);
  storm.mutable_buff()->set_duration_seconds(70.0);
  SkillBoost* boost = storm.mutable_buff()->add_boost();
  boost->set_skill_name("Final Attack");
  boost->set_final_attack_chance_mult(2.0);
  std::map<std::string, Skill> skills = {{"final_attack", final_attack},
                                         {"storm", storm}};
  ASSERT_TRUE(c.LearnSkill(final_attack, 20));
  ASSERT_TRUE(c.LearnSkill(storm, 1));

  DerivedStats down = DerivedStatsFor(c, skills);
  ASSERT_EQ(down.final_attacks.size(), 1u);
  EXPECT_NEAR(down.final_attacks[0].chance, 0.40, 1e-9);

  const BuffUp up[] = {{&skills["storm"]}};
  DerivedStats standing = DerivedStatsFor(c, skills, up);
  ASSERT_EQ(standing.final_attacks.size(), 1u);
  EXPECT_NEAR(standing.final_attacks[0].chance, 0.80, 1e-9);
}

// The other damage a boost can give a Final Attack: points on the hit's own
// multiplier, like GMS's "Night Lord's Mark Damage: +100% points".
TEST_F(DerivedStatsTest, ABoostCanLiftAFinalAttacksOwnMultiplier) {
  CharacterInstance c = MakeCharacter(rng_, 140, 50);
  Skill final_attack = FinalAttack();
  Skill hyper;
  hyper.set_name("Death Star");
  hyper.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(hyper, JOB_ADVANCEMENT_SWORDMAN);
  hyper.set_max_level(1);
  SkillBoost* boost = hyper.add_boost();
  boost->set_skill_name("Final Attack");
  boost->mutable_effect()->set_skill_pct(1.00);
  std::map<std::string, Skill> skills = {{"final_attack", final_attack},
                                         {"hyper", hyper}};
  ASSERT_TRUE(c.LearnSkill(final_attack, 20));
  ASSERT_TRUE(c.LearnSkill(hyper, 1));

  // 160% becomes 260%, and every hit gets the full amount.
  DerivedStats stats = DerivedStatsFor(c, skills);
  ASSERT_EQ(stats.final_attacks.size(), 1u);
  EXPECT_NEAR(stats.final_attacks[0].damage_pct, 2.60, 1e-9);
  EXPECT_NEAR(stats.final_attacks[0].damage_bonus_pct, 0.0, 1e-9);

  // Shaped like Blizzard: a swing with its own Final Attack. The same points
  // already apply to the swing, so the Final Attack must not get them again.
  Skill swung = final_attack;
  swung.set_kind(SKILL_KIND_ATTACK);
  swung.mutable_base()->set_skill_pct(2.00);
  skills["final_attack"] = swung;
  EXPECT_NEAR(DerivedStatsFor(c, skills).final_attacks[0].damage_pct, 1.60,
              1e-9);
}

// A boost can also be negative. Hurricane - Split Attack adds a second arrow
// but cuts each one's damage by a quarter, and combining two final damage
// sources must keep the negative instead of clamping it.
TEST_F(DerivedStatsTest, ABoostCanTakeALeverAway) {
  CharacterInstance c = MakeCharacter(rng_, 140, 50);
  Skill hyper;
  hyper.set_name("Hurricane - Split Attack");
  hyper.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(hyper, JOB_ADVANCEMENT_SWORDMAN);
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

// A boost naming a skill the character doesn't have does nothing, and a Final
// Attack no boost names keeps its original values.
TEST_F(DerivedStatsTest, AFinalAttackKeepsItsOwnChanceWhenNobodyNamesIt) {
  CharacterInstance c = MakeCharacter(rng_, 140, 50);
  Skill final_attack = FinalAttack();
  Skill hyper;
  hyper.set_name("Aimed Elsewhere");
  hyper.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(hyper, JOB_ADVANCEMENT_SWORDMAN);
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

// A burn on a passive applies to every swing the character makes. A burn on an
// attack stays with that attack, where the swing is priced.
TEST_F(DerivedStatsTest, OnlyAPassivesBurnFollowsTheCharacter) {
  CharacterInstance c = MakeCharacter(rng_, 40, 50);
  Skill venom;
  venom.set_name("Venom");
  venom.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(venom, JOB_ADVANCEMENT_SWORDMAN);
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

// Every Final Attack keeps its own entry, including the Hunter's matching pair.
// They are two independent rolls, and one merged entry would need a chance and
// damage that neither source has.
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

// Shaped like Advanced Final Attack: it states all of the Final Attack it
// replaces rather than a difference, so both must never count.
Skill AdvancedFinalAttack() {
  Skill skill;
  skill.set_name("Advanced Final Attack");
  skill.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(skill, JOB_ADVANCEMENT_SWORDMAN);
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

  // 60% of 5.10 and nothing else: the Fighter's own Final Attack is gone rather
  // than rolled a second time.
  DerivedStats stats = DerivedStatsFor(c, skills);
  ASSERT_EQ(stats.final_attacks.size(), 1u);
  EXPECT_NEAR(stats.final_attacks[0].chance, 0.60, 1e-9);
  EXPECT_NEAR(stats.final_attacks[0].damage_pct, 5.10, 1e-9);
}

// Three cases where a supersede doesn't apply, all from one rule: a skill that
// grants nothing replaces nothing. The superseding skill is unlearned, in
// another branch's book, or missing its gear, and the skill it names keeps
// working.
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
    // Learned in this character's own book, then put in the catalog as another
    // branch's. That is the only way to have a level in a book you don't hold,
    // and it is what a shared display name really does.
    Skill theirs = advanced;
    if (which == 1) {
      theirs.clear_placement();
      PlaceIn(theirs, JOB_ADVANCEMENT_FIGHTER);
    }
    std::map<std::string, Skill> skills = {{"final_attack", final_attack},
                                           {"advanced", theirs}};

    // Summed rather than read from one source, so a failing case reports
    // instead of aborting the next two. It also distinguishes 3.70 (both
    // counted) and 3.06 (the wrong one counted) from the correct 0.64.
    double total = 0.0;
    for (const FinalAttackSource& source :
         DerivedStatsFor(c, skills).final_attacks) {
      total += source.chance * source.damage_pct;
    }
    EXPECT_NEAR(total, 0.64, 1e-9) << "case " << which;
  }
}

// --- Exclusive groups ---

// The Sharp Eyes pair, the case groups exist for: a Decent version that beats
// three points in the real skill and loses to a maxed one, plus a stat it
// grants that no other member of the group gives.
Skill SharpEyes() {
  Skill skill;
  skill.set_name("Sharp Eyes");
  skill.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(skill, JOB_ADVANCEMENT_SWORDMAN);
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
  PlaceIn(skill, JOB_ADVANCEMENT_SWORDMAN);
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

  // The Decent's value wins and the three points in the real skill give
  // nothing. Its LUK applies whichever one wins, since nothing else in the
  // group grants LUK.
  DerivedStats stats = DerivedStatsFor(c, skills);
  EXPECT_NEAR(stats.crit_rate, 0.10, 1e-9);
  EXPECT_EQ(stats.skill_stats.luk(), 6);

  ASSERT_TRUE(c.LearnSkill(sharp, 17));
  stats = DerivedStatsFor(c, skills);
  EXPECT_NEAR(stats.crit_rate, 0.20, 1e-9);
  EXPECT_EQ(stats.skill_stats.luk(), 6);
}

// Two members giving the same amount count once, not twice. A skill in no group
// still adds up with everything, as every other skill does.
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
  // armor's DEF end up in the same stat line, no longer distinguishable.
  DerivedStats stats = DerivedStatsFor(c, skills);
  EquipStats total = TotalEquipStats(c, stats);
  EXPECT_EQ(total.luk(), 5);
  // 7 worn and 10 from Iron Body. This is DEF as a stat line holds it, which
  // isn't the character's DEF: stats.def adds the primary-stat base on top,
  // here the 2 from the skill's 5 LUK.
  EXPECT_EQ(total.def(), 17);
  EXPECT_EQ(stats.def, 19);
}

// --- base DEF from the primary stats ---

// Every character has DEF before wearing anything: 1.5 per point of STR, 0.4
// per point of DEX and LUK. A level-1 character with no gear isn't at zero.
TEST_F(DerivedStatsTest, PrimaryStatsCarryDefWithNothingWorn) {
  CharacterInstance c = MakeStatCharacter(rng_, 13, 4, 4, 4);

  // 1.5*13 + 0.4*(4+4) = 19.5 + 3.2 = 22.7, floored.
  EXPECT_EQ(DerivedStatsFor(c, {}).def, 22);
}

// INT gives no DEF, unlike the other three.
TEST_F(DerivedStatsTest, IntBuysNoDef) {
  CharacterInstance c = MakeStatCharacter(rng_, 0, 0, 500, 0);

  EXPECT_EQ(DerivedStatsFor(c, {}).def, 0);
}

// STR gives nearly four times the DEF of DEX or LUK, so the same AP spent on
// different stats doesn't give the same DEF.
TEST_F(DerivedStatsTest, StrIsWorthMoreDefThanDexOrLuk) {
  CharacterInstance strong = MakeStatCharacter(rng_, 100, 0, 0, 0);
  CharacterInstance quick = MakeStatCharacter(rng_, 0, 100, 0, 0);
  CharacterInstance lucky = MakeStatCharacter(rng_, 0, 0, 0, 100);

  EXPECT_EQ(DerivedStatsFor(strong, {}).def, 150);
  EXPECT_EQ(DerivedStatsFor(quick, {}).def, 40);
  EXPECT_EQ(DerivedStatsFor(lucky, {}).def, 40);
}

// Base DEF adds to armour; neither replaces the other.
TEST_F(DerivedStatsTest, BaseDefAddsToWornDef) {
  CharacterInstance c = MakeStatCharacter(rng_, 100, 0, 0, 0);
  EquipArmor(c, /*max_hp=*/0, /*def=*/30);

  EXPECT_EQ(DerivedStatsFor(c, {}).def, 180);
}

// A stat from gear gives exactly as much DEF as an allocated one, so the
// formula must read the total rather than the AP spent.
TEST_F(DerivedStatsTest, StatsFromGearBuyDefToo) {
  CharacterInstance allocated = MakeStatCharacter(rng_, 100, 0, 0, 0);
  CharacterInstance worn = MakeStatCharacter(rng_, 0, 0, 0, 0);
  EquipStrRing(worn, /*str=*/100);

  EXPECT_EQ(DerivedStatsFor(worn, {}).def, DerivedStatsFor(allocated, {}).def);
}

// So does a stat from a passive. Nimble Body's LUK counts toward DEF the same
// way a ring's would, which is why skill_stats is read back in.
TEST_F(DerivedStatsTest, StatsFromPassivesBuyDefToo) {
  CharacterInstance c = MakeCharacter(rng_, 15, 50);
  Skill nimble = NimbleBody();
  std::map<std::string, Skill> skills = {{"nimble_body", nimble}};
  ASSERT_TRUE(c.LearnSkill(nimble, 20));

  // 20 LUK from the skill, at 0.4 DEF each.
  EXPECT_EQ(DerivedStatsFor(c, skills).def, 8);
}

// The percentage applies to the whole total (stats, worn gear and flat skill
// grants) and leaves base DEF alone. The stats page reads base DEF to show both
// numbers.
TEST_F(DerivedStatsTest, DefPercentLeavesBaseDefAlone) {
  CharacterInstance c = MakeStatCharacter(rng_, 100, 0, 0, 0);
  EquipArmor(c, /*max_hp=*/0, /*def=*/30);
  Skill mastery = IronBody();
  mastery.mutable_base()->set_def_pct(0.5);
  mastery.mutable_per_level()->set_def_pct(0.0);
  std::map<std::string, Skill> skills = {{"iron_body", mastery}};
  ASSERT_TRUE(c.LearnSkill(mastery, 1));

  // 150 from STR, 30 worn, 10 from the skill, then +50% on the total.
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

  // Summed, the pair would be +5% and leave 157 DEF. Multiplied, they leave
  // 1.30 * 0.75 of the 150 from STR.
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

  // Reckless Hunt's trade: a quarter of the DEF given up. The base the
  // percentage applies to is unchanged.
  DerivedStats derived = DerivedStatsFor(c, skills);
  EXPECT_EQ(derived.base_def, 150);
  EXPECT_EQ(derived.def, 112);
}

// Combat Orders as the White Knight's book states it: one level for most of the
// ladder and two at the top. The per-level form can only express that step by
// carrying a fraction.
Skill CombatOrders() {
  Skill skill;
  skill.set_name("Combat Orders");
  skill.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(skill, JOB_ADVANCEMENT_SWORDMAN);
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

// A granted level counts everywhere a skill is read: Iron Body learned to 18,
// with two levels granted, is worth its level 20.
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

// The 4th job's rule: a skill marked for it can go two granted levels past its
// master level. Those levels aren't on its skill page, but they are worth what
// the ladder says, like any other level.
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

// Level bonuses from a group count as one grant too. They are read where every
// skill reads its level, not in the passive fold.
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

// The same four rules applied to a bare level, which is what the skill page has
// when it asks what one more point would give.
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

// GMS's Combat Orders doesn't raise beginner, hyper or 5th job skills. All
// three here have room to rise, so none is held back by a cap instead.
TEST_F(DerivedStatsTest, GrantedLevelsSkipTheBeginnersPageHypersAndVNodes) {
  Skill hyper = IronBody();
  hyper.set_hyper(true);
  Skill node = IronBody();
  node.set_v_node(V_NODE_KIND_COMMON);
  Skill fairy = IronBody();
  fairy.set_account_levels_per_level(10);
  Skill beginner = IronBody();
  beginner.clear_placement();
  PlaceIn(beginner, JOB_ADVANCEMENT_BEGINNER);
  Skill link = IronBody();
  link.set_link_line(JOB_ROGUE);

  EXPECT_EQ(LevelWithBonus(hyper, 5, 2), 5) << "a hyper skill";
  EXPECT_EQ(LevelWithBonus(node, 5, 2), 5) << "a V Matrix node";
  EXPECT_EQ(LevelWithBonus(fairy, 5, 2), 5) << "a skill nobody buys";
  EXPECT_EQ(LevelWithBonus(beginner, 5, 2), 5) << "the beginner's book";
  EXPECT_EQ(LevelWithBonus(link, 5, 2), 5) << "what the account climbed";
  EXPECT_EQ(LevelWithBonus(IronBody(), 5, 2), 7) << "and the ordinary skill";
}

// The account's progress is what counts: every character gets Blessing of the
// Fairy, whatever book they hold and without spending anything.
TEST_F(DerivedStatsTest, ASkillNobodyBuysStillPays) {
  Skill fairy;
  fairy.set_name("Blessing of the Fairy");
  fairy.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(fairy, JOB_ADVANCEMENT_BEGINNER);
  fairy.set_account_levels_per_level(10);
  fairy.mutable_base()->set_attack(1);
  fairy.mutable_per_level()->set_attack(1);
  std::map<std::string, Skill> skills = {{"blessing_of_the_fairy", fairy}};

  CharacterInstance c = MakeCharacter(rng_, 137, 0);
  EXPECT_EQ(DerivedStatsFor(c, skills).skill_stats.attack(), 13);

  CharacterInstance fresh = MakeCharacter(rng_, 1, 0);
  EXPECT_EQ(DerivedStatsFor(fresh, skills).skill_stats.attack(), 0)
      << "nothing until the account reaches ten";
}

// Two rules for a skill not marked for the 4th job's rule: the bonus never
// takes it past its master level, and never raises the skill that grants the
// bonus.
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
  // It has room to rise but isn't raised, because it grants the levels.
  EXPECT_EQ(EffectiveSkillLevel(c, orders, bonus), 5);
}

// The bonus doesn't teach a skill nobody learned.
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

// Another branch's Combat Orders isn't this character's, so it grants nothing.
// The same rule keeps a Page's Weapon Mastery off a Fighter.
TEST_F(DerivedStatsTest, BonusLevelsComeOnlyFromTheCharactersOwnBook) {
  CharacterInstance c = MakeCharacter(rng_, 100, 0);
  Skill iron_body = IronBody();
  Skill orders = CombatOrders();
  std::map<std::string, Skill> skills = {{"iron_body", iron_body},
                                         {"combat_orders", orders}};
  ASSERT_TRUE(c.LearnSkill(iron_body, 18));
  ASSERT_TRUE(c.LearnSkill(orders, 10));
  // Learned, then moved into a book this Swordman doesn't hold. The level
  // stays, since it is keyed by display name, but stops counting.
  skills["combat_orders"].mutable_placement(0)->set_job_advancement(
      JOB_ADVANCEMENT_MAGICIAN);

  EXPECT_EQ(BonusSkillLevels(c, skills), 0);
  EXPECT_EQ(DerivedStatsFor(c, skills).def, 180);
}

// GMS puts permanent grants on active skills and marks them "[Passive Effects:
// ...]". Phoenix is a summon that also raises DEF permanently. So a skill's
// kind decides what it does in a fight, not whether its levers are read.
TEST_F(DerivedStatsTest, AnAttackSkillsPermanentGrantsStillLand) {
  CharacterInstance c = MakeCharacter(rng_, 15, 50);
  Skill phoenix;
  phoenix.set_name("Phoenix");
  phoenix.set_kind(SKILL_KIND_AUTO_ATTACK);
  PlaceIn(phoenix, JOB_ADVANCEMENT_SWORDMAN);
  phoenix.set_max_level(10);
  phoenix.mutable_base()->set_skill_pct(2.28);  // what it hits for
  phoenix.mutable_base()->set_def(30);          // and what it grants for good
  phoenix.mutable_base()->set_max_hp_pct(0.10);
  std::map<std::string, Skill> skills = {{"phoenix", phoenix}};
  ASSERT_TRUE(c.LearnSkill(phoenix, 1));

  DerivedStats stats = DerivedStatsFor(c, skills);
  EXPECT_EQ(stats.max_hp, 55);
  EXPECT_EQ(stats.def, 30);
  // Its damage belongs to the fight and stays out of the stat line.
  EXPECT_DOUBLE_EQ(stats.damage_pct, 0.0);
}

// Fighter, Page and Spearman share skill names, and learned levels are keyed by
// display name, so the catalog has several entries matching one learned level.
// Only the character's own book may count, or the other branch's copy doubles
// it.
TEST_F(DerivedStatsTest, AnotherBranchsCopyOfASharedNameIsIgnored) {
  Character proto;
  proto.set_level(60);
  proto.set_job(JOB_SPEARMAN);
  proto.set_job_stage(2);
  (*proto.mutable_skill_levels())["Physical Training"] = 5;
  CharacterInstance c(rng_, std::move(proto));

  Skill mine = PhysicalTraining();
  PlaceIn(mine, JOB_ADVANCEMENT_SPEARMAN);
  Skill theirs = mine;
  theirs.clear_placement();
  PlaceIn(theirs, JOB_ADVANCEMENT_FIGHTER);
  std::map<std::string, Skill> skills = {{"spearman_physical_training", mine},
                                         {"fighter_physical_training", theirs}};

  // 30 STR, not 60: the Fighter's entry is another job's book.
  EXPECT_EQ(DerivedStatsFor(c, skills).skill_stats.str(), 30);
}

// The character's own book still counts, which is the other half of the same
// check.
TEST_F(DerivedStatsTest, TheCharactersOwnBookStillCounts) {
  Character proto;
  proto.set_level(60);
  proto.set_job(JOB_SPEARMAN);
  proto.set_job_stage(2);
  (*proto.mutable_skill_levels())["Physical Training"] = 5;
  CharacterInstance c(rng_, std::move(proto));

  Skill mine = PhysicalTraining();
  PlaceIn(mine, JOB_ADVANCEMENT_SPEARMAN);
  std::map<std::string, Skill> skills = {{"spearman_physical_training", mine}};
  EXPECT_EQ(DerivedStatsFor(c, skills).skill_stats.str(), 30);
}

// Spirit Blade's two levers: attack, in the same form a weapon grants it, and
// the share of a hit reflected back at the attacker.
TEST_F(DerivedStatsTest, AttackAndReflectionFoldIn) {
  CharacterInstance c = MakeCharacter(rng_, 60, 0);
  Skill blade;
  blade.set_name("Spirit Blade");
  blade.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(blade, JOB_ADVANCEMENT_SWORDMAN);
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

// Combo Attack states its attack per orb and how many orbs it gives. The orbs
// are assumed full, so the character gets the product.
TEST_F(DerivedStatsTest, ComboOrbsAreWorthTheirAttackApiece) {
  CharacterInstance c = MakeCharacter(rng_, 60, 0);
  Skill combo;
  combo.set_name("Combo Attack");
  combo.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(combo, JOB_ADVANCEMENT_SWORDMAN);
  combo.set_max_level(1);
  combo.set_combo_orbs(5);
  combo.mutable_base()->set_attack_per_combo_orb(2);
  std::map<std::string, Skill> skills = {{"combo_attack", combo}};
  ASSERT_TRUE(c.LearnSkill(combo, 1));

  EXPECT_EQ(DerivedStatsFor(c, skills).skill_stats.attack(), 10);
}

// The skill that prices the orbs isn't the one that grants them, so the count
// must come from across the book: Combo Synergy gives final damage per orb, and
// only Combo Attack says there are five. Priced against that ring, the pair is
// worth 5 x 5%, and the attack per orb still applies.
TEST_F(DerivedStatsTest, OneSkillPricesTheOrbsAnotherHandsOut) {
  CharacterInstance c = MakeCharacter(rng_, 60, 0);
  Skill combo;
  combo.set_name("Combo Attack");
  combo.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(combo, JOB_ADVANCEMENT_SWORDMAN);
  combo.set_max_level(1);
  combo.set_combo_orbs(5);
  combo.mutable_base()->set_attack_per_combo_orb(2);
  Skill synergy;
  synergy.set_name("Combo Synergy");
  synergy.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(synergy, JOB_ADVANCEMENT_SWORDMAN);
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

// A character has one ring of orbs however many skills describe it, so two
// skills stating a count use the larger, not the sum. This matters when a later
// skill raises the maximum: summing would add to the old count instead of
// replacing it.
TEST_F(DerivedStatsTest, TwoOrbCountsLeaveTheLargerRing) {
  CharacterInstance c = MakeCharacter(rng_, 60, 0);
  std::map<std::string, Skill> skills;
  int counts[] = {5, 8};
  for (int i = 0; i < 2; ++i) {
    Skill skill;
    skill.set_name("Combo " + std::to_string(i));
    skill.set_kind(SKILL_KIND_PASSIVE);
    PlaceIn(skill, JOB_ADVANCEMENT_SWORDMAN);
    skill.set_max_level(1);
    skill.set_combo_orbs(counts[i]);
    skills.insert({"combo_" + std::to_string(i), skill});
    ASSERT_TRUE(c.LearnSkill(skill, 1));
  }
  Skill priced;
  priced.set_name("Combo Attack");
  priced.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(priced, JOB_ADVANCEMENT_SWORDMAN);
  priced.set_max_level(1);
  priced.mutable_base()->set_attack_per_combo_orb(2);
  skills.insert({"combo_attack", priced});
  ASSERT_TRUE(c.LearnSkill(priced, 1));

  EXPECT_EQ(DerivedStatsFor(c, skills).skill_stats.attack(), 16);
}

// Advanced Combo enlarges the ring that the Fighter's Combo Attack prices,
// without replacing Combo Attack. So the ATT per orb is still paid, against
// more orbs than the granting skill names.
TEST_F(DerivedStatsTest, AWiderRingIsStillPricedByTheSkillThatNamedIt) {
  CharacterInstance c = MakeCharacter(rng_, 100, 0);
  Skill combo;
  combo.set_name("Combo Attack");
  combo.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(combo, JOB_ADVANCEMENT_SWORDMAN);
  combo.set_max_level(1);
  combo.set_combo_orbs(5);
  combo.mutable_base()->set_attack_per_combo_orb(2);
  Skill advanced;
  advanced.set_name("Advanced Combo");
  advanced.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(advanced, JOB_ADVANCEMENT_SWORDMAN);
  advanced.set_max_level(20);
  advanced.set_combo_orbs(5);
  advanced.set_combo_orbs_per_level(0.26316);
  std::map<std::string, Skill> skills = {{"combo_attack", combo},
                                         {"advanced_combo", advanced}};
  ASSERT_TRUE(c.LearnSkill(combo, 1));

  // Five orbs at 2 ATT each, before Advanced Combo is learned.
  EXPECT_EQ(DerivedStatsFor(c, skills).skill_stats.attack(), 10);
  ASSERT_TRUE(c.LearnSkill(advanced, 20));
  EXPECT_EQ(DerivedStatsFor(c, skills).skill_stats.attack(), 20);
}

// The Hero's two: boss damage and DEF, each worth the orb count times the
// per-orb value.
TEST_F(DerivedStatsTest, BossDamageAndDefArePricedPerOrb) {
  CharacterInstance c = MakeCharacter(rng_, 140, 0);
  Skill combo;
  combo.set_name("Advanced Combo");
  combo.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(combo, JOB_ADVANCEMENT_SWORDMAN);
  combo.set_max_level(1);
  combo.set_combo_orbs(10);
  Skill rush;
  rush.set_name("Boss Rush");
  rush.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(rush, JOB_ADVANCEMENT_SWORDMAN);
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

// Instinctual Combo's gain raises what the ring is worth rather than adding its
// own per-orb bonus, so every per-orb lever moves at once. The flat ones are
// floored, as whole-number grants are everywhere else.
TEST_F(DerivedStatsTest, AGainLiftsEveryOrbBargainAtOnce) {
  CharacterInstance c = MakeCharacter(rng_, 200, 0);
  Skill combo;
  combo.set_name("Advanced Combo");
  combo.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(combo, JOB_ADVANCEMENT_SWORDMAN);
  combo.set_max_level(1);
  combo.set_combo_orbs(10);
  combo.mutable_base()->set_attack_per_combo_orb(2);
  combo.mutable_base()->set_final_dmg_pct_per_combo_orb(0.10);
  combo.mutable_base()->set_boss_pct_per_combo_orb(0.02);
  combo.mutable_base()->set_def_per_combo_orb(100);
  std::map<std::string, Skill> skills = {{"advanced_combo", combo}};
  ASSERT_TRUE(c.LearnSkill(combo, 1));

  DerivedStats before = DerivedStatsFor(c, skills);
  EXPECT_EQ(before.skill_stats.attack(), 20);
  EXPECT_NEAR(before.final_dmg_pct, 1.0, 1e-9);
  EXPECT_NEAR(before.boss_pct, 0.20, 1e-9);
  EXPECT_EQ(before.skill_stats.def(), 1000);

  Skill instinct;
  instinct.set_name("Instinctual Combo");
  instinct.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(instinct, JOB_ADVANCEMENT_SWORDMAN);
  instinct.set_max_level(1);
  instinct.mutable_base()->set_combo_orb_gain_pct(0.13);
  skills.insert({"instinctual_combo", instinct});
  ASSERT_TRUE(c.LearnSkill(instinct, 1));

  DerivedStats after = DerivedStatsFor(c, skills);
  EXPECT_EQ(after.skill_stats.attack(), 22);
  EXPECT_NEAR(after.final_dmg_pct, 1.13, 1e-9);
  EXPECT_NEAR(after.boss_pct, 0.226, 1e-9);
  EXPECT_EQ(after.skill_stats.def(), 1130);
}

// Shaped like Sword Illusion: six orbs' worth of final damage added to the
// ring. They count only for that lever, and a gain raises them like the rest of
// the ring.
TEST_F(DerivedStatsTest, LitOrbsPayFinalDamageAndNothingElse) {
  CharacterInstance c = MakeCharacter(rng_, 200, 0);
  Skill combo;
  combo.set_name("Advanced Combo");
  combo.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(combo, JOB_ADVANCEMENT_SWORDMAN);
  combo.set_max_level(1);
  combo.set_combo_orbs(10);
  combo.mutable_base()->set_attack_per_combo_orb(2);
  combo.mutable_base()->set_final_dmg_pct_per_combo_orb(0.10);
  combo.mutable_base()->set_boss_pct_per_combo_orb(0.02);
  std::map<std::string, Skill> skills = {{"advanced_combo", combo}};
  ASSERT_TRUE(c.LearnSkill(combo, 1));

  Skill illusion;
  illusion.set_name("Sword Illusion");
  illusion.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(illusion, JOB_ADVANCEMENT_SWORDMAN);
  illusion.set_max_level(1);
  illusion.mutable_base()->set_final_dmg_combo_orbs(6);
  skills.insert({"sword_illusion", illusion});
  ASSERT_TRUE(c.LearnSkill(illusion, 1));

  // Sixteen orbs of final damage, and ten orbs of everything else.
  DerivedStats lit = DerivedStatsFor(c, skills);
  EXPECT_NEAR(lit.final_dmg_pct, 1.60, 1e-9);
  EXPECT_EQ(lit.skill_stats.attack(), 20);
  EXPECT_NEAR(lit.boss_pct, 0.20, 1e-9);

  // A gain raises the six along with the ten: 18.08 orbs rather than 16.
  Skill instinct;
  instinct.set_name("Instinctual Combo");
  instinct.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(instinct, JOB_ADVANCEMENT_SWORDMAN);
  instinct.set_max_level(1);
  instinct.mutable_base()->set_combo_orb_gain_pct(0.13);
  skills.insert({"instinctual_combo", instinct});
  ASSERT_TRUE(c.LearnSkill(instinct, 1));
  EXPECT_NEAR(DerivedStatsFor(c, skills).final_dmg_pct, 1.808, 1e-9);
}

// With no orbs, the per-orb bonus is worth nothing, and the character is left
// with the plain final damage they bought.
TEST_F(DerivedStatsTest, PerOrbFinalDamageIsWorthNothingWithoutOrbs) {
  CharacterInstance c = MakeCharacter(rng_, 60, 0);
  Skill synergy;
  synergy.set_name("Combo Synergy");
  synergy.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(synergy, JOB_ADVANCEMENT_SWORDMAN);
  synergy.set_max_level(20);
  synergy.mutable_base()->set_final_dmg_pct_per_combo_orb(0.05);
  synergy.mutable_base()->set_final_dmg_pct(0.10);
  std::map<std::string, Skill> skills = {{"combo_synergy", synergy}};
  ASSERT_TRUE(c.LearnSkill(synergy, 1));

  EXPECT_NEAR(DerivedStatsFor(c, skills).final_dmg_pct, 0.10, 1e-9);

  // Orbs added only for final damage don't count without a ring either. That
  // matches GMS's "while Combo Attack is active" without needing a separate
  // gate.
  synergy.mutable_base()->clear_final_dmg_pct_per_combo_orb();
  synergy.mutable_base()->set_final_dmg_combo_orbs(6);
  skills["combo_synergy"] = synergy;
  EXPECT_NEAR(DerivedStatsFor(c, skills).final_dmg_pct, 0.10, 1e-9);
}

// The two damage levers only differ once there is a second source: % damage
// adds, final damage multiplies. Two skills of 10% each give 20% and 21%.
TEST_F(DerivedStatsTest, DamagePercentSumsAndFinalDamageMultiplies) {
  CharacterInstance c = MakeCharacter(rng_, 60, 0);
  std::map<std::string, Skill> skills;
  for (const std::string& name : {"first", "second"}) {
    Skill skill;
    skill.set_name(name);
    skill.set_kind(SKILL_KIND_PASSIVE);
    PlaceIn(skill, JOB_ADVANCEMENT_SWORDMAN);
    skill.set_max_level(1);
    skill.mutable_base()->set_damage_pct(0.10);
    skill.mutable_base()->set_final_dmg_pct(0.10);
    ASSERT_TRUE(c.LearnSkill(skill, 1));
    skills[name] = skill;
  }

  DerivedStats stats = DerivedStatsFor(c, skills);
  EXPECT_DOUBLE_EQ(stats.damage_pct, 0.20);
  // Not DOUBLE_EQ: 1.1 * 1.1 - 1 lands a few ulps off 0.21.
  EXPECT_NEAR(stats.final_dmg_pct, 0.21, 1e-9);
}

// Both levers must reach the damage formula, and only PassiveOffenseFor carries
// them there.
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

// Shaped like Marksmanship: 6% of the monster's DEF ignored at level 1, plus a
// point per level.
Skill Marksmanship() {
  Skill skill;
  skill.set_name("Marksmanship");
  skill.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(skill, JOB_ADVANCEMENT_SWORDMAN);
  skill.set_max_level(20);
  skill.mutable_base()->set_ied_pct(0.06);
  skill.mutable_per_level()->set_ied_pct(0.01);
  return skill;
}

// Marksmanship's other half: attack raised by a percentage rather than a flat
// amount, from 6% plus a point per level.
Skill AttackPercent() {
  Skill skill;
  skill.set_name("Marksmanship");
  skill.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(skill, JOB_ADVANCEMENT_SWORDMAN);
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
  PlaceIn(grant, JOB_ADVANCEMENT_SWORDMAN);
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

  // 80 worn and 20 granted make 100, and the 25% applies to both together
  // rather than either alone.
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

  // A magician uses magic attack, so a percentage of the attacking stat has to
  // reach it too.
  EXPECT_EQ(TotalEquipStats(c, DerivedStatsFor(c, skills)).magic_attack(), 100);
}

// Speed Mirage's passive half: a skill that makes one other skill hit harder,
// named rather than tagged.
Skill SpeedMirage() {
  Skill skill;
  skill.set_name("Speed Mirage");
  skill.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(skill, JOB_ADVANCEMENT_SWORDMAN);
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
  // It isn't plain damage: everything else the character swings is unchanged.
  EXPECT_DOUBLE_EQ(stats.damage_pct, 0.0);
}

// The other damage a boost can grant: a share of the character's own % damage,
// which only the named skill gets. See SkillBoost::effect.
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

// A boost node states its damage from level 1 and its extras at 20 and 40, so a
// bonus below its level isn't read at all. See SkillBoost::min_level.
TEST_F(DerivedStatsTest, AGatedBoostPaysNothingBelowItsLevel) {
  CharacterInstance c = MakeCharacter(rng_, 15, 100);
  Skill node = SpeedMirage();
  SkillBoost* gated = node.add_boost();
  gated->set_skill_name("Wind Arrow");
  gated->set_min_level(20);
  gated->mutable_effect()->set_ied_pct(0.20);
  std::map<std::string, Skill> skills = {{"speed_mirage", node}};
  ASSERT_TRUE(c.LearnSkill(node, 19));
  EXPECT_DOUBLE_EQ(DerivedStatsFor(c, skills).skill_bonus.at("Wind Arrow").ied,
                   0.0);

  ASSERT_TRUE(c.LearnSkill(node, 1));
  EXPECT_NEAR(DerivedStatsFor(c, skills).skill_bonus.at("Wind Arrow").ied, 0.20,
              1e-9);
}

// A boost aimed at the passive that owns a Final Attack has no swing to apply
// to, so all its levers go on the extra hit, not only the damage.
TEST_F(DerivedStatsTest, ABoostReachesTheFinalAttackItNames) {
  CharacterInstance c = MakeCharacter(rng_, 15, 100);
  Skill strike;
  strike.set_name("Final Attack");
  strike.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(strike, JOB_ADVANCEMENT_SWORDMAN);
  strike.set_max_level(20);
  strike.mutable_base()->set_final_attack_chance(0.40);
  strike.mutable_base()->set_final_attack_pct(1.60);
  Skill node = SpeedMirage();
  SkillBoost* boost = node.mutable_boost(0);
  boost->set_skill_name("Final Attack");
  boost->clear_effect_per_level();
  boost->mutable_effect()->clear_skill_pct();
  boost->mutable_effect()->set_crit_rate(0.05);
  boost->mutable_effect()->set_ied_pct(0.20);
  boost->mutable_effect()->set_final_dmg_pct(1.20);
  std::map<std::string, Skill> skills = {{"final_attack", strike},
                                         {"speed_mirage", node}};
  ASSERT_TRUE(c.LearnSkill(strike, 20));
  ASSERT_TRUE(c.LearnSkill(node, 20));

  DerivedStats stats = DerivedStatsFor(c, skills);
  ASSERT_EQ(stats.final_attacks.size(), 1u);
  const FinalAttackSource& source = stats.final_attacks.front();
  EXPECT_NEAR(source.crit_rate, 0.05, 1e-9);
  EXPECT_NEAR(source.ied, 0.20, 1e-9);
  EXPECT_NEAR(source.final_dmg_pct, 1.20, 1e-9);
  // None of it applies to the character's ordinary swings.
  EXPECT_DOUBLE_EQ(stats.crit_rate, 0.0);
}

TEST_F(DerivedStatsTest, TwoBoostsOnOneSkillSum) {
  CharacterInstance c = MakeCharacter(rng_, 15, 100);
  Skill first = SpeedMirage();
  Skill second = SpeedMirage();
  second.set_name("Silhouette Mirage");
  // The lever aimed at the mark the skill leaves adds up the same way, and
  // rises with the granting skill's level like the one above.
  for (Skill* skill : {&first, &second}) {
    skill->mutable_boost(0)->set_dot_skill_pct(0.11);
    skill->mutable_boost(0)->set_dot_skill_pct_per_level(0.01);
    skill->mutable_boost(0)->set_dot_duration_seconds(2.0);
    skill->mutable_boost(0)->mutable_effect()->set_normal_pct(0.10);
  }
  std::map<std::string, Skill> skills = {{"speed_mirage", first},
                                         {"silhouette", second}};
  ASSERT_TRUE(c.LearnSkill(first, 20));
  ASSERT_TRUE(c.LearnSkill(second, 20));

  DerivedStats derived = DerivedStatsFor(c, skills);
  const SkillBonus& bonus = derived.skill_bonus.at("Wind Arrow");
  EXPECT_NEAR(bonus.skill_pct, 1.40, 1e-9);
  EXPECT_NEAR(bonus.normal_pct, 0.20, 1e-9);
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

  // 25% and 40% summed would be 65%; what remains of the DEF is 0.75 * 0.60.
  EXPECT_NEAR(DerivedStatsFor(c, skills).ied, 1.0 - 0.75 * 0.60, 1e-9);
}

// The elemental version does add up, and that is the whole difference: GMS
// applies each source to the resistance directly rather than to what the
// previous source left.
TEST_F(DerivedStatsTest, TwoSourcesOfIgnoredElementalResistanceSum) {
  CharacterInstance c = MakeCharacter(rng_, 15, 100);
  Skill decrease = Marksmanship();
  decrease.set_name("Elemental Decrease");
  decrease.clear_per_level();
  decrease.mutable_base()->clear_ied_pct();
  decrease.mutable_base()->set_ier_pct(0.10);
  Skill indignant = decrease;
  indignant.set_name("Righteously Indignant");
  std::map<std::string, Skill> skills = {{"elemental_decrease", decrease},
                                         {"righteously_indignant", indignant}};
  ASSERT_TRUE(c.LearnSkill(decrease, 1));
  ASSERT_TRUE(c.LearnSkill(indignant, 1));

  EXPECT_NEAR(DerivedStatsFor(c, skills).ier, 0.20, 1e-9);
}

// An attack's own ignored defence, boss damage and final damage belong to its
// swing, so they never reach the character's stat line. OffenseStatsFor reads
// them from the skill being swung instead. Gungnir's Descent ignores 30% on its
// own hit and nothing on the spear thrust after it. Mist Eruption's final
// damage works the same way.
TEST_F(DerivedStatsTest, AnAttacksOwnSwingLeversStayOffTheStatLine) {
  CharacterInstance c = MakeCharacter(rng_, 15, 100);
  Skill gungnir = Marksmanship();
  gungnir.set_name("Gungnir's Descent");
  gungnir.set_kind(SKILL_KIND_ATTACK);
  gungnir.mutable_base()->set_boss_pct(0.30);
  gungnir.mutable_base()->set_normal_pct(0.20);
  gungnir.mutable_base()->set_final_dmg_pct(0.20);
  std::map<std::string, Skill> skills = {{"gungnirs_descent", gungnir}};
  ASSERT_TRUE(c.LearnSkill(gungnir, 20));

  DerivedStats stats = DerivedStatsFor(c, skills);
  EXPECT_DOUBLE_EQ(stats.ied, 0.0);
  EXPECT_DOUBLE_EQ(stats.boss_pct, 0.0);
  EXPECT_DOUBLE_EQ(stats.normal_pct, 0.0);
  EXPECT_DOUBLE_EQ(stats.final_dmg_pct, 0.0);

  // A skill on its own clock keeps them the same way: Radiant Evil ignores
  // defence on the eye's attacks, but the Dark Knight's own swing doesn't.
  Skill clock = gungnir;
  clock.set_kind(SKILL_KIND_AUTO_ATTACK);
  clock.set_cast_interval_seconds(20.0);
  DerivedStats ticking = DerivedStatsFor(c, {{"gungnirs_descent", clock}});
  EXPECT_DOUBLE_EQ(ticking.ied, 0.0);
  EXPECT_DOUBLE_EQ(ticking.boss_pct, 0.0);
  EXPECT_DOUBLE_EQ(ticking.normal_pct, 0.0);
  EXPECT_DOUBLE_EQ(ticking.final_dmg_pct, 0.0);

  // The same levers on a passive are added as usual.
  Skill passive = gungnir;
  passive.set_kind(SKILL_KIND_PASSIVE);
  DerivedStats folded = DerivedStatsFor(c, {{"gungnirs_descent", passive}});
  EXPECT_NEAR(folded.ied, 0.25, 1e-9);
  EXPECT_NEAR(folded.boss_pct, 0.30, 1e-9);
  EXPECT_NEAR(folded.normal_pct, 0.20, 1e-9);
  EXPECT_NEAR(folded.final_dmg_pct, 0.20, 1e-9);
}

// Vicious Shot uses the character's final crit rate, including the base 5% and
// with nothing capped. The rate past 100% is what the skill exists to use.
TEST_F(DerivedStatsTest, CriticalDamagePerCriticalRateSpendsTheUncappedRate) {
  CharacterInstance c = MakeCharacter(rng_, 15, 100);
  Skill shot = Marksmanship();
  shot.set_name("Vicious Shot");
  shot.mutable_base()->clear_ied_pct();
  shot.mutable_per_level()->clear_ied_pct();
  shot.mutable_base()->set_crit_rate(0.60);
  shot.mutable_base()->set_crit_dmg_per_crit_rate(0.50);
  ASSERT_TRUE(c.LearnSkill(shot, 20));

  DerivedStats stats = DerivedStatsFor(c, {{"vicious_shot", shot}});
  EXPECT_NEAR(stats.crit_rate, 0.60, 1e-9);
  EXPECT_NEAR(stats.crit_dmg, 0.50 * 0.65, 1e-9);

  // Past 100% it keeps counting, which is the point of the skill.
  CharacterInstance rich = MakeCharacter(rng_, 15, 100);
  Skill over = shot;
  over.mutable_base()->set_crit_rate(1.20);
  ASSERT_TRUE(rich.LearnSkill(over, 20));
  DerivedStats spent = DerivedStatsFor(rich, {{"vicious_shot", over}});
  EXPECT_NEAR(spent.crit_dmg, 0.50 * 1.25, 1e-9);
}

// The part an attack states separately stays with the character, whatever the
// lever. Cruel Stab's final damage applies to the Shadower's Assassinate, but
// the 50% Assassinate states for itself doesn't apply to the Shadower.
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

// The two halves together are the whole effect: each lever is in exactly one,
// and neither adds a lever. The skill page lists them separately, so a lever
// missing from both would be neither shown nor applied.
TEST_F(DerivedStatsTest, TheSwingLeversAndTheRestPartitionAnEffect) {
  SkillEffect effect;
  effect.set_ied_pct(0.40);
  effect.set_ier_pct(0.15);
  effect.set_boss_pct(0.30);
  effect.set_crit_rate(0.20);
  effect.set_final_dmg_pct(0.10);
  effect.set_hp_recover_pct(0.08);
  effect.set_attack(20);
  effect.set_damage_pct(0.05);

  SkillEffect swing = SwingLeversOf(effect);
  EXPECT_DOUBLE_EQ(swing.ied_pct(), 0.40);
  EXPECT_DOUBLE_EQ(swing.ier_pct(), 0.15);
  EXPECT_DOUBLE_EQ(swing.boss_pct(), 0.30);
  EXPECT_DOUBLE_EQ(swing.crit_rate(), 0.20);
  EXPECT_DOUBLE_EQ(swing.final_dmg_pct(), 0.10);
  EXPECT_DOUBLE_EQ(swing.hp_recover_pct(), 0.08);
  EXPECT_EQ(swing.attack(), 0);
  EXPECT_DOUBLE_EQ(swing.damage_pct(), 0.05);

  SkillEffect kept = WithoutSwingLevers(effect);
  EXPECT_DOUBLE_EQ(kept.ied_pct(), 0.0);
  EXPECT_DOUBLE_EQ(kept.ier_pct(), 0.0);
  EXPECT_DOUBLE_EQ(kept.boss_pct(), 0.0);
  EXPECT_DOUBLE_EQ(kept.crit_rate(), 0.0);
  EXPECT_DOUBLE_EQ(kept.final_dmg_pct(), 0.0);
  EXPECT_DOUBLE_EQ(kept.hp_recover_pct(), 0.0);
  EXPECT_EQ(kept.attack(), 20);
  EXPECT_DOUBLE_EQ(kept.damage_pct(), 0.0);
}

// Pick Pocket drops the meso and Meso Explosion throws it, so neither is worth
// anything without the other. Meso Mastery's points apply to each line of the
// throw, whatever order the catalog adds the three in.
TEST_F(DerivedStatsTest, MesoExplosionPairsWithPickPocket) {
  CharacterInstance c = MakeCharacter(rng_, 15, 100);
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
  explosion.mutable_base()->set_meso_hit_pct(0.43);
  explosion.mutable_per_level()->set_meso_hit_pct(0.03);
  explosion.mutable_base()->set_normal_skill_pct(0.012);
  explosion.mutable_per_level()->set_normal_skill_pct(0.002);

  Skill mastery;
  mastery.set_name("Meso Mastery");
  mastery.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(mastery, JOB_ADVANCEMENT_SWORDMAN);
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
  // A 30% chance per line to drop a meso, and each throw is two lines of 100%
  // plus Meso Mastery's 20 points.
  EXPECT_NEAR(derived.final_attacks[0].chance, 0.30, 1e-9);
  EXPECT_NEAR(derived.final_attacks[0].damage_pct, 2 * 1.20, 1e-9);
  EXPECT_TRUE(derived.final_attacks[0].per_line);
  EXPECT_NEAR(derived.meso_pct, 0.20, 1e-9);
  // Nothing has given the mesos boss damage, so they hit a boss like the
  // character does.
  EXPECT_NEAR(derived.final_attacks[0].boss_pct, 0.0, 1e-9);
  // GMS's points against non-boss monsters follow the line count the same way
  // as the damage: 5 per line, so 10 per meso.
  EXPECT_NEAR(derived.final_attacks[0].normal_skill_pct, 2 * 0.05, 1e-9);
  // They don't apply to the character, who hits normal monsters no harder for
  // having the pair.
  EXPECT_NEAR(derived.damage_pct, 0.0, 1e-9);

  // Blood Money gives them boss damage. It applies to the throw and not the
  // Shadower, so the stat line is unchanged.
  Skill money;
  money.set_name("Blood Money");
  money.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(money, JOB_ADVANCEMENT_SWORDMAN);
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

  // A boost node also aims crit rate and final damage at it, and the mesos are
  // the only place either can go, since Meso Explosion isn't a swing.
  Skill node;
  node.set_name("Meso Explosion Boost");
  node.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(node, JOB_ADVANCEMENT_SWORDMAN);
  node.set_max_level(3);
  SkillBoost* node_boost = node.add_boost();
  node_boost->set_skill_name("Meso Explosion");
  node_boost->mutable_effect()->set_final_dmg_pct(0.60);
  node_boost->mutable_effect_per_level()->set_final_dmg_pct(0.60);
  SkillBoost* node_crit = node.add_boost();
  node_crit->set_skill_name("Meso Explosion");
  node_crit->set_min_level(2);
  node_crit->mutable_effect()->set_crit_rate(0.05);
  skills["node"] = node;
  ASSERT_TRUE(c.LearnSkill(node, 3));
  DerivedStats boosted = DerivedStatsFor(c, skills);

  ASSERT_EQ(boosted.final_attacks.size(), 1u);
  EXPECT_NEAR(boosted.final_attacks[0].final_dmg_pct, 1.80, 1e-9);
  EXPECT_NEAR(boosted.final_attacks[0].crit_rate, 0.05, 1e-9);
  // Neither reaches the character: the node names one skill, and that skill
  // throws mesos rather than swinging.
  EXPECT_NEAR(boosted.final_dmg_pct, branded.final_dmg_pct, 1e-9);
  EXPECT_NEAR(boosted.crit_rate, branded.crit_rate, 1e-9);
}

// Boss damage adds up across passives, like plain damage and unlike IED.
TEST_F(DerivedStatsTest, BossDamageSumsAcrossPassives) {
  CharacterInstance c = MakeCharacter(rng_, 15, 100);
  Skill spirit;
  spirit.set_name("Spirit of the Star");
  spirit.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(spirit, JOB_ADVANCEMENT_SWORDMAN);
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

// Holy Fountain states a pulse and an interval, and both change with level.
// They reach the fight separately, so it can heal on each tick.
TEST_F(DerivedStatsTest, AFountainKeepsItsPulseAndItsInterval) {
  CharacterInstance c = MakeCharacter(rng_, 15, 100);
  Skill fountain;
  fountain.set_name("Holy Fountain");
  fountain.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(fountain, JOB_ADVANCEMENT_SWORDMAN);
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

// Shaped like Holy Water: the same pulse again for every whole step of INT, so
// 2500 INT doubles it and 5000 triples it. It uses total INT, so INT from
// skills counts along with AP.
TEST_F(DerivedStatsTest, AFountainCanPourHarderForACleverCharacter) {
  Skill water;
  water.set_name("Holy Water");
  water.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(water, JOB_ADVANCEMENT_SWORDMAN);
  water.set_max_level(10);
  water.mutable_base()->set_regen_pct(0.005);
  water.mutable_per_level()->set_regen_pct(0.005);
  water.mutable_base()->set_regen_interval_seconds(10.0);
  water.mutable_base()->set_regen_int_step(2500);
  Skill wisdom;
  wisdom.set_name("High Wisdom");
  wisdom.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(wisdom, JOB_ADVANCEMENT_SWORDMAN);
  wisdom.set_max_level(10);
  wisdom.mutable_base()->set_int_(1000);
  std::map<std::string, Skill> skills = {{"holy_water", water},
                                         {"high_wisdom", wisdom}};

  CharacterInstance dim = MakeStatCharacter(rng_, 0, 0, 2499, 0);
  ASSERT_TRUE(dim.LearnSkill(water, 10));
  std::vector<RegenPulse> pulses = DerivedStatsFor(dim, skills).regen_pulses;
  ASSERT_EQ(pulses.size(), 1);
  EXPECT_NEAR(pulses[0].pct, 0.05, 1e-9);

  // The pulse grows but the interval doesn't, so a high-INT character heals in
  // bigger pulses, not more frequent ones.
  CharacterInstance clever = MakeStatCharacter(rng_, 0, 0, 5000, 0);
  ASSERT_TRUE(clever.LearnSkill(water, 10));
  pulses = DerivedStatsFor(clever, skills).regen_pulses;
  ASSERT_EQ(pulses.size(), 1);
  EXPECT_NEAR(pulses[0].pct, 3.0 * 0.05, 1e-9);
  EXPECT_NEAR(pulses[0].interval_seconds, 10.0, 1e-9);

  // The last 1000 INT comes from a skill rather than AP and gives the same
  // helping. Reading only the allocation would stop at two.
  CharacterInstance granted = MakeStatCharacter(rng_, 0, 0, 4000, 0);
  ASSERT_TRUE(granted.LearnSkill(water, 10));
  ASSERT_TRUE(granted.LearnSkill(wisdom, 1));
  pulses = DerivedStatsFor(granted, skills).regen_pulses;
  ASSERT_EQ(pulses.size(), 1);
  EXPECT_NEAR(pulses[0].pct, 3.0 * 0.05, 1e-9);
}

// A Bishop has three, each on its own clock. They stay separate rather than
// being summed, since no single interval could describe them.
TEST_F(DerivedStatsTest, TwoFountainsKeepTheirOwnClocks) {
  CharacterInstance c = MakeCharacter(rng_, 15, 100);
  Skill fountain;
  fountain.set_name("Holy Fountain");
  fountain.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(fountain, JOB_ADVANCEMENT_SWORDMAN);
  fountain.set_max_level(10);
  fountain.mutable_base()->set_regen_pct(0.13);
  fountain.mutable_base()->set_regen_interval_seconds(7.5);
  Skill infinity;
  infinity.set_name("Infinity");
  infinity.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(infinity, JOB_ADVANCEMENT_SWORDMAN);
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

// A pulse with no interval can't be timed, so it grants nothing.
TEST_F(DerivedStatsTest, AFountainWithNoIntervalGrantsNothing) {
  CharacterInstance c = MakeCharacter(rng_, 15, 100);
  Skill fountain;
  fountain.set_name("Holy Fountain");
  fountain.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(fountain, JOB_ADVANCEMENT_SWORDMAN);
  fountain.set_max_level(10);
  fountain.mutable_base()->set_regen_pct(0.13);
  std::map<std::string, Skill> skills = {{"holy_fountain", fountain}};
  ASSERT_TRUE(c.LearnSkill(fountain, 1));

  EXPECT_TRUE(DerivedStatsFor(c, skills).regen_pulses.empty());
}

// High Wisdom grants the magician's own stat. It reaches the stat line like any
// other stat and gives no DEF, which is the rule for INT from any source.
TEST_F(DerivedStatsTest, SkillGrantedIntLandsInTheStatLineAndBuysNoDef) {
  CharacterInstance c = MakeCharacter(rng_, 60, 0);
  Skill wisdom;
  wisdom.set_name("High Wisdom");
  wisdom.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(wisdom, JOB_ADVANCEMENT_SWORDMAN);
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

// Freezing Crush's pair: crit damage is added alongside crit rate, and magic
// attack goes into the stat line just like a staff's. That is how a magician's
// skills reach their own damage.
TEST_F(DerivedStatsTest, MagicAttackAndCritDamageFoldIn) {
  CharacterInstance c = MakeCharacter(rng_, 60, 0);
  Skill crush;
  crush.set_name("Freezing Crush");
  crush.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(crush, JOB_ADVANCEMENT_SWORDMAN);
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

// Weapon Mastery gives mastery with both a spear and a polearm, but only the
// spear gets faster swings. The skill works with either; only the bonus stops.
TEST_F(DerivedStatsTest, AWeaponBonusLandsOnlyForItsOwnWeapons) {
  CharacterInstance c = MakeCharacter(rng_, 60, 0);
  Skill mastery;
  mastery.set_name("Weapon Mastery");
  mastery.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(mastery, JOB_ADVANCEMENT_SWORDMAN);
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

// A weapon bonus is flat: it is the same at level 1 as at max, so levelling the
// skill must not multiply it.
TEST_F(DerivedStatsTest, AWeaponBonusDoesNotGrowWithTheSkill) {
  CharacterInstance c = MakeCharacter(rng_, 60, 0);
  Skill mastery;
  mastery.set_name("Weapon Mastery");
  mastery.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(mastery, JOB_ADVANCEMENT_SWORDMAN);
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

// Final Attack needs a sword or an axe. A learned skill whose weapon isn't in
// hand grants nothing, and grants everything again once it is.
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

// The real Shield Mastery on a real Bandit holding a real scabbard. The
// synthetic test below checks the rule; this checks that the rule reaches the
// skill written for it, using real data on both sides.
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

  // Learned but holding no scabbard: the skill grants none of its three levers.
  // The attack that is there comes from Blessing of the Fairy, which every
  // character has: six points at level 60.
  const int kFairyAttack = 6;
  DerivedStats bare = DerivedStatsFor(c, skills);
  EXPECT_EQ(bare.skill_stats.attack(), kFairyAttack);
  EXPECT_DOUBLE_EQ(bare.damage_taken_pct, 0.0);
  EXPECT_EQ(bare.def, bare.base_def);

  c.PickUp(std::make_unique<EquipInstance>(equips.at("hidden_shadow")));
  ASSERT_TRUE(c.Equip(c.inventory().size() - 1));
  ASSERT_TRUE(c.has_secondary());

  // GMS's own numbers at level 10: +20 attack and 60% of damage blocked.
  DerivedStats armed = DerivedStatsFor(c, skills);
  EXPECT_EQ(armed.skill_stats.attack(), kFairyAttack + 20);
  EXPECT_DOUBLE_EQ(armed.damage_taken_pct, 0.6);

  // DEF is 2.1 times the same character's DEF without the lever. That is the
  // only way to check it on a character wearing real gear.
  std::map<std::string, Skill> without = skills;
  without.at("bandit_shield_mastery").mutable_base()->clear_def_pct();
  without.at("bandit_shield_mastery").mutable_per_level()->clear_def_pct();
  EXPECT_EQ(armed.def, static_cast<int>(DerivedStatsFor(c, without).def * 2.1));
}

// Shield Mastery needs a secondary rather than a weapon type. The synthetic
// passive tests the rule alone, apart from whatever the real skill grants.
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

// Shaped like Maple Warrior: a share of AP-bought stats given back as flat
// stats. 1% at level 1 up to 15% at 30, which is GMS's ceil(L/2)%.
Skill MapleWarrior() {
  Skill skill;
  skill.set_name("Maple Warrior");
  skill.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(skill, JOB_ADVANCEMENT_SWORDMAN);
  skill.set_max_level(30);
  skill.mutable_base()->set_ap_stat_pct(0.01);
  skill.mutable_per_level()->set_ap_stat_pct(0.00483);
  return skill;
}

// A character who spent AP, and a ring granting the same stat, so the test can
// tell what the share applies to.
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

  // The ring's 500 STR wasn't bought with AP, so 15% is 150 and not 225. What
  // the skill grants counts like the ring's grant.
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
  // 1.5 DEF per STR and 0.4 per DEX, over all of the character's stats.
  EXPECT_EQ(stats.base_def, static_cast<int>(1.5 * 1650 + 0.4 * 115));
}

// The share is rounded down per stat, as GMS does: 100 DEX at 1% is one point,
// and at 15% it is fifteen, not a fraction of the two stats summed.
TEST_F(DerivedStatsTest, MapleWarriorRoundsEachStatDown) {
  CharacterInstance c = MapleWarriorCharacter(rng_);
  Skill mw = MapleWarrior();
  std::map<std::string, Skill> skills = {{"maple_warrior", mw}};
  ASSERT_TRUE(c.LearnSkill(mw, 1));

  DerivedStats stats = DerivedStatsFor(c, skills);
  EXPECT_EQ(stats.skill_stats.str(), 10);
  EXPECT_EQ(stats.skill_stats.dex(), 1);
  // Nothing was spent on these, so neither gets anything.
  EXPECT_EQ(stats.skill_stats.int_(), 0);
  EXPECT_EQ(stats.skill_stats.luk(), 0);
}

// Maple World Goddess's Blessing, whose only effect multiplies the skill above.
// GMS's "increases stat bonuses as Maple Warrior by 400%" means adding 3.00 to
// the existing 100%.
Skill GoddessBlessing() {
  Skill skill;
  skill.set_name("Maple World Goddess's Blessing");
  skill.set_kind(SKILL_KIND_ACTIVE);
  PlaceIn(skill, JOB_ADVANCEMENT_SWORDMAN);
  skill.set_max_level(30);
  skill.mutable_buff()->mutable_base()->set_ap_stat_bonus_pct(0.10);
  skill.mutable_buff()->mutable_per_level()->set_ap_stat_bonus_pct(0.10);
  return skill;
}

TEST_F(DerivedStatsTest, GoddessBlessingMultipliesMapleWarriorsShare) {
  CharacterInstance c = MapleWarriorCharacter(rng_);
  Skill mw = MapleWarrior();
  Skill blessing = GoddessBlessing();
  std::map<std::string, Skill> skills = {{"maple_warrior", mw},
                                         {"blessing", blessing}};
  ASSERT_TRUE(c.LearnSkill(mw, 30));
  ASSERT_TRUE(c.LearnSkill(blessing, 30));

  // Without it: 15% of the 1000 STR bought with AP.
  EXPECT_EQ(DerivedStatsFor(c, skills).skill_stats.str(), 150);

  // With it up, the share is four times as big (60%), and still rounded per
  // stat, so 100 DEX gives 60.
  const BuffUp up[] = {{&blessing}};
  DerivedStats buffed = DerivedStatsFor(c, skills, up);
  EXPECT_EQ(buffed.skill_stats.str(), 600);
  EXPECT_EQ(buffed.skill_stats.dex(), 60);
}

// The multiplier has nothing of its own to grant. A character without Maple
// Warrior gets four times nothing, which gives GMS's "can only be used when you
// have Maple Warrior" for free.
TEST_F(DerivedStatsTest, GoddessBlessingGrantsNothingWithoutMapleWarrior) {
  CharacterInstance c = MapleWarriorCharacter(rng_);
  Skill blessing = GoddessBlessing();
  std::map<std::string, Skill> skills = {{"blessing", blessing}};
  ASSERT_TRUE(c.LearnSkill(blessing, 30));

  const BuffUp up[] = {{&blessing}};
  EXPECT_EQ(DerivedStatsFor(c, skills, up).skill_stats.str(), 0);
}

// --- Hyper Stats ---

// A level-200 character with the whole Hyper Stat pool to spend.
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
  const StatPreset farming = StatPreset::kFirst;
  ASSERT_TRUE(c.AllocateHyperStat(HYPER_STAT_FIELD_STR, farming, 10));
  ASSERT_TRUE(c.AllocateHyperStat(HYPER_STAT_FIELD_MAX_HP, farming, 5));
  ASSERT_TRUE(c.AllocateHyperStat(HYPER_STAT_FIELD_CRIT_RATE, farming, 5));
  ASSERT_TRUE(c.AllocateHyperStat(HYPER_STAT_FIELD_ATTACK, farming, 3));

  DerivedStats stats = DerivedStatsFor(c, {});
  // 300 from the Hyper Stat and 30 from the Inner Ability lines every character
  // has from level 160.
  EXPECT_EQ(stats.skill_stats.str(), 330);
  EXPECT_EQ(stats.skill_stats.attack(), 9);
  EXPECT_EQ(stats.skill_stats.magic_attack(), 9)
      << "one stat pays both attacks";
  EXPECT_DOUBLE_EQ(stats.crit_rate, 0.05);
  EXPECT_EQ(stats.max_hp, 11000) << "+10% over a 10,000 pool";
}

TEST_F(DerivedStatsTest, HyperDamageLeversSplitBossFromNormal) {
  CharacterInstance c = HyperStatCharacter(rng_);
  const StatPreset farming = StatPreset::kFirst;
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

// The four stats are final stats: Maple Warrior takes its share of the
// allocation only, so it doesn't raise the Hyper Stat's 300.
TEST_F(DerivedStatsTest, MapleWarriorLeavesTheHyperStatAlone) {
  CharacterInstance c = HyperStatCharacter(rng_);
  Skill mw = MapleWarrior();
  std::map<std::string, Skill> skills = {{"maple_warrior", mw}};
  ASSERT_TRUE(c.LearnSkill(mw, 30));
  ASSERT_TRUE(
      c.AllocateHyperStat(HYPER_STAT_FIELD_STR, StatPreset::kFirst, 10));

  DerivedStats stats = DerivedStatsFor(c, skills);
  EXPECT_EQ(stats.skill_stats.str(), 150 + 300 + 30)
      << "the default Inner Ability lines are final stat too";
}

// --- Inner Ability ---

// A character with `lines` in the given preset, at a level where they apply.
CharacterInstance AbilityCharacter(std::mt19937& rng, AbilityRank rank,
                                   const std::vector<AbilityLine>& lines,
                                   StatPreset preset = StatPreset::kFirst,
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

// Every character's starting lines give +10 all stats each, and nothing applies
// below the unlock level.
TEST_F(DerivedStatsTest, TheDefaultLinesPayFromLevel160) {
  CharacterInstance below = AbilityCharacter(rng_, ABILITY_RANK_RARE, {},
                                             StatPreset::kFirst, /*level=*/159);
  EXPECT_EQ(DerivedStatsFor(below, {}).skill_stats.str(), 0);

  CharacterInstance c = AbilityCharacter(rng_, ABILITY_RANK_RARE, {});
  DerivedStats stats = DerivedStatsFor(c, {});
  EXPECT_EQ(stats.skill_stats.str(), 30) << "three Rare All Stats lines";
  EXPECT_EQ(stats.skill_stats.dex(), 30);
  EXPECT_EQ(stats.skill_stats.int_(), 30);
  EXPECT_EQ(stats.skill_stats.luk(), 30);
}

// The switch that applies an ability line has one case per type and a
// static_assert to force a look when a type is added. But a case that falls
// through gives nothing silently, and the line still looks right on the panel.
// So every type has to change something.
TEST_F(DerivedStatsTest, EveryAbilityLineTypePaysSomething) {
  auto fingerprint = [this](AbilityLineType type) {
    std::vector<AbilityLine> lines;
    if (type != ABILITY_LINE_TYPE_UNSPECIFIED) {
      lines.push_back(Line(type, ABILITY_RANK_LEGENDARY));
    }
    CharacterInstance c = AbilityCharacter(rng_, ABILITY_RANK_LEGENDARY, lines);
    DerivedStats d = DerivedStatsFor(c, {});
    return std::vector<double>{
        static_cast<double>(d.skill_stats.str()),
        static_cast<double>(d.skill_stats.dex()),
        static_cast<double>(d.skill_stats.int_()),
        static_cast<double>(d.skill_stats.luk()),
        static_cast<double>(d.skill_stats.attack()),
        static_cast<double>(d.skill_stats.magic_attack()),
        static_cast<double>(d.max_hp),
        static_cast<double>(d.attack_speed_bonus),
        d.crit_rate,
        d.boss_pct,
        d.normal_pct,
        d.meso_pct,
        d.item_drop_pct,
        d.buff_duration_pct,
    };
  };

  const std::vector<double> bare = fingerprint(ABILITY_LINE_TYPE_UNSPECIFIED);
  for (int i = AbilityLineType_MIN; i <= AbilityLineType_MAX; ++i) {
    if (!AbilityLineType_IsValid(i) || i == ABILITY_LINE_TYPE_UNSPECIFIED) {
      continue;
    }
    EXPECT_NE(fingerprint(static_cast<AbilityLineType>(i)), bare)
        << AbilityLineType_Name(i) << " pays nothing";
  }
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

// All Stats gives each of the four stats, which is why one line of it is worth
// four lines of a single stat.
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

// Ability stats are final stats, just like Hyper Stats: Maple Warrior's share
// applies to the allocation only.
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
// allocation.
TEST_F(DerivedStatsTest, TheBossingAbilityIsReadOnlyWhenAskedFor) {
  CharacterInstance c = AbilityCharacter(
      rng_, ABILITY_RANK_LEGENDARY,
      {Line(ABILITY_LINE_TYPE_BOSS_DAMAGE, ABILITY_RANK_LEGENDARY),
       Line(ABILITY_LINE_TYPE_MESO, ABILITY_RANK_EPIC),
       Line(ABILITY_LINE_TYPE_ITEM_DROP, ABILITY_RANK_EPIC)},
      StatPreset::kSecond);
  c.set_autoswap_presets(true);

  EXPECT_DOUBLE_EQ(DerivedStatsFor(c, {}).boss_pct, 0.0);
  EXPECT_DOUBLE_EQ(DerivedStatsFor(c, {}, {}, {}, Activity::kBossing).boss_pct,
                   0.20);

  // With autoswap off, the preset in use applies to both.
  c.set_autoswap_presets(false);
  c.SetSlotInUse(PresetKind::kInnerAbility, StatPreset::kSecond);
  EXPECT_DOUBLE_EQ(DerivedStatsFor(c, {}).boss_pct, 0.20);
}

// The allocation read is the one the caller asks for.
TEST_F(DerivedStatsTest, TheBossingAllocationIsReadOnlyWhenAskedFor) {
  CharacterInstance c = HyperStatCharacter(rng_);
  c.set_autoswap_presets(true);
  ASSERT_TRUE(c.AllocateHyperStat(HYPER_STAT_FIELD_BOSS_DAMAGE,
                                  StatPreset::kSecond, 10));

  EXPECT_DOUBLE_EQ(DerivedStatsFor(c, {}).boss_pct, 0.0);
  EXPECT_DOUBLE_EQ(DerivedStatsFor(c, {}, {}, {}, Activity::kBossing).boss_pct,
                   0.35);
}

// With autoswap off there is no activity to follow: the slot the player
// selected applies to farming and bossing alike, and each kind is chosen
// separately.
TEST_F(DerivedStatsTest, TheSlotInUseAnswersForEveryActivity) {
  CharacterInstance c = HyperStatCharacter(rng_);
  ASSERT_TRUE(c.AllocateHyperStat(HYPER_STAT_FIELD_BOSS_DAMAGE,
                                  StatPreset::kThird, 10));
  EXPECT_DOUBLE_EQ(DerivedStatsFor(c, {}).boss_pct, 0.0);

  c.SetSlotInUse(PresetKind::kHyperStats, StatPreset::kThird);
  EXPECT_EQ(c.SlotInUse(PresetKind::kHyperStats), StatPreset::kThird);
  EXPECT_EQ(c.SlotInUse(PresetKind::kInnerAbility), StatPreset::kFirst);
  EXPECT_DOUBLE_EQ(DerivedStatsFor(c, {}).boss_pct, 0.35);
  EXPECT_DOUBLE_EQ(DerivedStatsFor(c, {}, {}, {}, Activity::kBossing).boss_pct,
                   0.35);

  // Turning autoswap on overrides it: autoswap reads the first two slots.
  c.set_autoswap_presets(true);
  EXPECT_DOUBLE_EQ(DerivedStatsFor(c, {}).boss_pct, 0.0);
}

// Arcane Force from the Hyper Stat adds to what the symbols give.
TEST_F(DerivedStatsTest, HyperArcaneForceAddsToTheSymbols) {
  CharacterInstance c = HyperStatCharacter(rng_);
  c.set_autoswap_presets(true);
  EXPECT_EQ(c.arcane_force(), 0);
  ASSERT_TRUE(c.AllocateHyperStat(HYPER_STAT_FIELD_ARCANE_FORCE,
                                  StatPreset::kFirst, 10));
  EXPECT_EQ(c.arcane_force(), 50);
  EXPECT_EQ(c.arcane_force(Activity::kBossing), 0);
}

// --- Final Pact ---

// Shaped like Final Pact: the cooldown between revivals gets shorter with
// level, from 1103 seconds down to 900 at 30.
Skill FinalPact() {
  Skill skill;
  skill.set_name("Final Pact");
  skill.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(skill, JOB_ADVANCEMENT_SWORDMAN);
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

// Two revival skills aren't one long one. What matters is how soon the next
// revival comes, so the shorter cooldown is used.
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

// Cooldown reductions add up, unlike the cooldowns themselves, and come off
// whichever cooldown is used. A character with only the reduction has no
// revival, so the cooldown stays at zero.
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

// Shaped like Dark Resonance: permanent ignored defence, plus more of it while
// the buff is up.
Skill DarkResonance() {
  Skill skill;
  skill.set_name("Dark Resonance");
  skill.set_kind(SKILL_KIND_ACTIVE);
  PlaceIn(skill, JOB_ADVANCEMENT_SWORDMAN);
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

// This is why a buff can't be a multiplier on a finished damage number: what it
// grants combines with what the character has the same way two skills do, and
// two sources of ignored defence combine multiplicatively rather than adding.
TEST_F(DerivedStatsTest, ABuffCombinesWithThePermanentHalf) {
  CharacterInstance c = MakeCharacter(rng_, 100, 100);
  Skill resonance = DarkResonance();
  std::map<std::string, Skill> skills = {{"dark_resonance", resonance}};
  ASSERT_TRUE(c.LearnSkill(resonance, 30));

  EXPECT_NEAR(DerivedStatsFor(c, skills).ied, 0.30, 1e-9);
  const BuffUp up[] = {{&skills.at("dark_resonance")}};
  DerivedStats buffed = DerivedStatsFor(c, skills, absl::MakeConstSpan(up));
  // 30% and 10%, which together leave 63% of the monster's DEF.
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

// A node's buff can be cast like a book's. It is gated on the character's
// matrix rather than an advancement, since a common node names none.
TEST_F(DerivedStatsTest, ANodesBuffIsOneTheCharacterCanPutUp) {
  CharacterInstance c = MakeCharacter(rng_, 100, 100);
  Skill node = DarkResonance();
  node.set_name("Decent Advanced Blessing");
  PlaceIn(node, JOB_ADVANCEMENT_COMMON);
  node.set_v_node(V_NODE_KIND_COMMON);
  node.set_max_level(MaxVNodeLevel(V_NODE_KIND_COMMON));
  std::map<std::string, Skill> skills = {{"decent_advanced_blessing", node}};
  while (c.proto().job_stage() < kFifthJobStage) {
    c.AdvanceJob(c.proto().job());
  }
  EXPECT_TRUE(BuffSkillsFor(c, skills).empty()) << "unlearned it raises none";

  c.AddVPoints(VNodeCost(V_NODE_KIND_COMMON, 0, 1));
  ASSERT_TRUE(c.LearnSkill(node, 1));
  std::vector<const Skill*> buffs = BuffSkillsFor(c, skills);
  ASSERT_EQ(buffs.size(), 1u);
  EXPECT_EQ(buffs[0]->name(), "Decent Advanced Blessing");
}

// Drop rate comes in two units (whole percents on equipment, a fraction on a
// passive) and must come out as one number.
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
  PlaceIn(greed, JOB_ADVANCEMENT_SWORDMAN);
  greed.set_max_level(10);
  greed.mutable_base()->set_item_drop_pct(0.01);
  greed.mutable_per_level()->set_item_drop_pct(0.01);
  std::map<std::string, Skill> skills = {{"greed", greed}};
  ASSERT_TRUE(c.LearnSkill(greed, 10));
  EXPECT_NEAR(DerivedStatsFor(c, skills).item_drop_pct, 0.30, 1e-9);
}

// Meso comes in the same two units, but the worn part has its own cap and the
// granted part doesn't.
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

  // A skill's share is added past the worn cap, not under it.
  Skill greed;
  greed.set_name("Greed");
  greed.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(greed, JOB_ADVANCEMENT_SWORDMAN);
  greed.set_max_level(10);
  greed.mutable_base()->set_meso_pct(0.05);
  greed.mutable_per_level()->set_meso_pct(0.05);
  std::map<std::string, Skill> skills = {{"greed", greed}};
  ASSERT_TRUE(c.LearnSkill(greed, 10));
  EXPECT_NEAR(MesoBonus(DerivedStatsFor(c, skills)), 1.50, 1e-9);

  // The hard cap applies to whatever the two total.
  DerivedStats piled = DerivedStatsFor(c, skills);
  piled.meso_pct = 10.0;
  EXPECT_NEAR(MesoBonus(piled), kMesoHardCap, 1e-9);
}

// The Wealth Acquisition Potion does three things, and the share it adds goes
// past the equipment cap rather than under it.
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
  // Past the cap, not under it: a character already at +100% from gear earns
  // 2.2 x 1.2 == 2.64 times what a character with nothing does.
  EXPECT_NEAR(MesoBonus(after), 1.20, 1e-9);
  EXPECT_NEAR(after.meso_final_mult, 1.20, 1e-9);
  EXPECT_NEAR(after.item_drop_pct, 0.20, 1e-9);

  // It does nothing in a boss fight: the buff is for farming, and a fight and
  // the Boss stats tab read the bossing preset.
  DerivedStats bossing = DerivedStatsFor(c, {}, {}, {}, Activity::kBossing);
  EXPECT_NEAR(MesoBonus(bossing), 1.00, 1e-9);
  EXPECT_NEAR(bossing.meso_final_mult, 1.0, 1e-9);
  EXPECT_NEAR(bossing.item_drop_pct, 0.0, 1e-9);
}

// The Extreme Green Potion is the reverse: an attack speed stage in a boss
// fight, and nothing on a map.
TEST_F(DerivedStatsTest, TheGreenPotionIsAStageInABossFightAlone) {
  std::mt19937 rng(1);
  CharacterInstance c = MakeCharacter(rng, 190, 0);
  ASSERT_TRUE(c.ToggleConsumable(CONSUMABLE_TYPE_EXTREME_GREEN_POTION));

  EXPECT_EQ(DerivedStatsFor(c, {}).uncapped_attack_speed_bonus, 0);
  EXPECT_EQ(DerivedStatsFor(c, {}, {}, {}, Activity::kBossing)
                .uncapped_attack_speed_bonus,
            kGreenPotionAttackSpeed);
}

// --- the party ---

// Shaped like Bless: the caster and the party get the same thing, which is the
// common case.
Skill Bless() {
  Skill skill;
  skill.set_name("Bless");
  skill.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(skill, JOB_ADVANCEMENT_SWORDMAN);
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

  // A second caster keeps their own rather than getting both: a buff doesn't
  // stack with itself.
  CharacterInstance other = MakeCharacter(rng_, 100, 0);
  ASSERT_TRUE(other.LearnSkill(bless, 10));
  EXPECT_EQ(DerivedStatsFor(other, skills, {}, party).skill_stats.attack(), 15);
}

// Shaped like Smokescreen: the party part is inside the buff, so it reaches an
// ally as a timed buff of their own rather than as a passive. DerivedStatsFor
// is checked for the negative here (nothing permanent), and AllyBuffsFor is
// what gives the fight the buff's uptime.
Skill Smokescreen() {
  Skill skill;
  skill.set_name("Smokescreen");
  skill.set_kind(SKILL_KIND_ACTIVE);
  PlaceIn(skill, JOB_ADVANCEMENT_SWORDMAN);
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

  // Nothing permanent either way: the caster's own part is also a buff.
  EXPECT_DOUBLE_EQ(DerivedStatsFor(plain, skills, {}, party).damage_taken_pct,
                   0.0);
  std::vector<AllyGrant> buffs = AllyBuffsFor(plain, skills, party);
  ASSERT_EQ(buffs.size(), 1u);
  EXPECT_EQ(buffs[0].skill->name(), "Smokescreen");
  EXPECT_EQ(buffs[0].level, 10);
}

// An ally's buff is added the same way as the character's own, so it can grant
// anything a buff grants, not only damage reduction.
TEST_F(DerivedStatsTest, APartyBuffGrantsWhateverABuffGrants) {
  Skill blessing = Smokescreen();
  blessing.set_name("Benediction");
  blessing.set_max_level(30);
  blessing.mutable_buff()->Clear();
  blessing.mutable_buff()->set_duration_seconds(30.0);
  blessing.mutable_buff()->mutable_base()->set_final_dmg_pct(0.33);
  blessing.mutable_buff()->mutable_ally_base()->set_final_dmg_pct(0.05);
  blessing.mutable_buff()->mutable_ally_per_level()->set_final_dmg_pct(0.01);
  std::map<std::string, Skill> skills = {{"benediction", blessing}};
  CharacterInstance caster = MakeCharacter(rng_, 200, 0);
  ASSERT_TRUE(caster.LearnSkill(blessing, 6));
  CharacterInstance plain = MakeCharacter(rng_, 200, 0);
  std::vector<CharacterInstance> party = PartyOf(std::move(caster));

  std::vector<AllyGrant> raised = AllyBuffsFor(plain, skills, party);
  ASSERT_EQ(raised.size(), 1u);
  // While down, it gives nothing: it is a timed buff, not a permanent grant.
  EXPECT_DOUBLE_EQ(DerivedStatsFor(plain, skills, {}, party).final_dmg_pct,
                   0.0);
  // While up, the caster's level decides it: level six is 5% plus five.
  const BuffUp up[] = {{raised[0].skill, raised[0].caster, raised[0].level}};
  EXPECT_NEAR(DerivedStatsFor(plain, skills, up, party).final_dmg_pct, 0.10,
              1e-9);
}

// Shaped like Benediction: the party's share grows with the caster's INT, and
// is capped at the caster's own share divided by the party size, rounded.
TEST_F(DerivedStatsTest, APartyBuffGrowsOnTheCastersIntAndSplitsBetweenThem) {
  Skill blessing = Smokescreen();
  blessing.set_name("Benediction");
  blessing.set_max_level(30);
  Buff* buff = blessing.mutable_buff();
  buff->Clear();
  buff->set_duration_seconds(30.0);
  buff->mutable_base()->set_final_dmg_pct(0.33);
  buff->mutable_ally_base()->set_final_dmg_pct(0.06);
  AllyIntLever* lever = buff->add_ally_int_lever();
  lever->set_int_step(3000);
  lever->mutable_effect()->set_final_dmg_pct(0.01);
  lever->set_cap_is_party_share(true);
  std::map<std::string, Skill> skills = {{"benediction", blessing}};

  CharacterInstance caster = MakeCharacter(rng_, 200, 0);
  ASSERT_TRUE(caster.LearnSkill(blessing, 30));
  CharacterInstance plain = MakeCharacter(rng_, 200, 0);
  std::vector<CharacterInstance> party = PartyOf(std::move(caster));

  // 24,000 INT is eight steps over the 6% base, which a party of two gets in
  // full. The cap there is half the caster's own 33%, rounded up to 17%.
  const BuffUp pair[] = {{&skills["benediction"], &party[0], 30, 24000, 2}};
  EXPECT_NEAR(DerivedStatsFor(plain, skills, pair, party).final_dmg_pct, 0.14,
              1e-9);

  // With three in range, the same INT is capped at a third: 33/3 = 11%.
  const BuffUp trio[] = {{&skills["benediction"], &party[0], 30, 24000, 3}};
  EXPECT_NEAR(DerivedStatsFor(plain, skills, trio, party).final_dmg_pct, 0.11,
              1e-9);

  // The steps count whole thousands: 26,999 is still short of the ninth step.
  const BuffUp under[] = {{&skills["benediction"], &party[0], 30, 26999, 2}};
  EXPECT_NEAR(DerivedStatsFor(plain, skills, under, party).final_dmg_pct, 0.14,
              1e-9);

  // A Bishop with no INT still gives the base their level sets.
  const BuffUp poor[] = {{&skills["benediction"], &party[0], 30, 0, 2}};
  EXPECT_NEAR(DerivedStatsFor(plain, skills, poor, party).final_dmg_pct, 0.06,
              1e-9);

  // That INT is the allocation, gear and skills summed.
  EXPECT_EQ(
      TotalIntFor(party[0], skills),
      party[0].proto().allocated_stats().int_() +
          TotalEquipStats(party[0], DerivedStatsFor(party[0], skills)).int_());
}

// A lever with its own cap stops there, whatever the party size: the recovery
// and attack speed depend only on the caster's INT.
TEST_F(DerivedStatsTest, ACappedIntLeverStopsAtItsOwnCeiling) {
  Skill blessing = Smokescreen();
  blessing.set_name("Benediction");
  blessing.set_max_level(30);
  Buff* buff = blessing.mutable_buff();
  buff->Clear();
  buff->set_duration_seconds(30.0);
  buff->mutable_ally_base()->set_crit_rate(0.01);
  AllyIntLever* lever = buff->add_ally_int_lever();
  lever->set_int_step(2000);
  lever->mutable_effect()->set_crit_rate(0.01);
  lever->mutable_cap()->set_crit_rate(0.10);
  std::map<std::string, Skill> skills = {{"benediction", blessing}};

  CharacterInstance caster = MakeCharacter(rng_, 200, 0);
  ASSERT_TRUE(caster.LearnSkill(blessing, 30));
  CharacterInstance plain = MakeCharacter(rng_, 200, 0);
  std::vector<CharacterInstance> party = PartyOf(std::move(caster));

  // Four whole steps over the 1% base, and still well under the cap.
  const BuffUp mid[] = {{&skills["benediction"], &party[0], 30, 8999, 2}};
  EXPECT_NEAR(DerivedStatsFor(plain, skills, mid, party).crit_rate, 0.05, 1e-9);
  // Past the cap, it stays at the cap however much INT there is.
  const BuffUp rich[] = {{&skills["benediction"], &party[0], 30, 900000, 2}};
  EXPECT_NEAR(DerivedStatsFor(plain, skills, rich, party).crit_rate, 0.10,
              1e-9);
}

// The two rules that filter a permanent grant filter a buff the same way: a
// Shadower gets nothing from the Shadower beside them, and nobody gets anything
// from an ally who never learned it.
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
  // An ally who never learned it gives nothing to anyone.
  EXPECT_TRUE(AllyBuffsFor(plain, skills, PartyOf(MakeCharacter(rng_, 100, 0)))
                  .empty());
  EXPECT_TRUE(AllyBuffsFor(holder, skills, party).empty());
  EXPECT_EQ(AllyBuffsFor(plain, skills, party).size(), 1u);
}

// Shaped like Angel Ray: an attack's own recovery goes with the swing, but the
// share it gives the party is a passive and must survive that removal.
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

// Shaped like Hex of the Evil Eye: the party gets half of what the caster does.
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

// Shaped like Puncture: an ally gets a lever based on the enemy's condition,
// which is combined separately from flat levers and must still reach them.
TEST_F(DerivedStatsTest, AllyHalfCarriesTheEnemysCondition) {
  Skill puncture = Bless();
  puncture.set_name("Puncture");
  puncture.set_kind(SKILL_KIND_ATTACK);
  puncture.mutable_base()->set_final_dmg_pct_when_afflicted(0.25);
  puncture.mutable_ally_base()->set_final_dmg_pct_when_afflicted(0.10);
  puncture.clear_per_level();
  puncture.clear_ally_per_level();
  std::map<std::string, Skill> skills = {{"puncture", puncture}};
  CharacterInstance caster = MakeCharacter(rng_, 100, 0);
  ASSERT_TRUE(caster.LearnSkill(puncture, 10));
  CharacterInstance plain = MakeCharacter(rng_, 100, 0);

  EXPECT_DOUBLE_EQ(
      DerivedStatsFor(caster, skills).condition.final_dmg_pct_when_afflicted,
      0.25);
  std::vector<CharacterInstance> party = PartyOf(std::move(caster));
  EXPECT_DOUBLE_EQ(DerivedStatsFor(plain, skills, {}, party)
                       .condition.final_dmg_pct_when_afflicted,
                   0.10);
}

// Shaped like Parashock Guard: the caster is paid for shielding someone, so
// alone they get nothing.
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

// Divine Echo's other half: the blessing's share is paid only while the echo is
// on someone, so alone the buff gives only its two unconditional shares, and
// those multiply rather than add.
TEST_F(DerivedStatsTest, ABuffsPartyShareWaitsForCompany) {
  Skill echo = Bless();
  echo.set_name("Divine Echo");
  echo.mutable_buff()->set_duration_seconds(30.0);
  echo.mutable_buff()->mutable_base()->set_final_dmg_pct(1.45);
  echo.mutable_buff()->mutable_with_party_base()->set_final_dmg_pct(0.15);
  std::map<std::string, Skill> skills = {{"echo", echo}};
  CharacterInstance caster = MakeCharacter(rng_, 100, 0);
  ASSERT_TRUE(caster.LearnSkill(echo, 10));
  const BuffUp up[] = {{&echo}};

  EXPECT_DOUBLE_EQ(
      DerivedStatsFor(caster, skills, absl::MakeConstSpan(up)).final_dmg_pct,
      1.45);
  std::vector<CharacterInstance> party = PartyOf(MakeCharacter(rng_, 100, 0));
  EXPECT_DOUBLE_EQ(
      DerivedStatsFor(caster, skills, absl::MakeConstSpan(up), party)
          .final_dmg_pct,
      2.45 * 1.15 - 1.0);
}

// Shaped like Divine Echo: the grant goes to one other member, and every client
// picks the same one from the roster's names. The caster is excluded, or a
// Paladin whose name sorted first would echo nobody.
TEST_F(DerivedStatsTest, AGrantReachingOneMemberLandsOnOneName) {
  Skill echo = Bless();
  echo.set_name("Divine Echo");
  echo.set_ally_grant_reaches_one(true);
  std::map<std::string, Skill> skills = {{"echo", echo}};
  auto member = [&](const char* name, bool casts) {
    CharacterInstance member = MakeCharacter(rng_, 100, 0);
    member.SetUsername(name);
    if (casts) {
      member.LearnSkill(echo, 10);
    }
    return member;
  };
  auto party = [&](CharacterInstance caster, CharacterInstance other) {
    std::vector<CharacterInstance> seated;
    seated.push_back(std::move(caster));
    seated.push_back(std::move(other));
    return seated;
  };

  std::vector<CharacterInstance> beside_anna =
      party(member("Paladin", true), member("Zoe", false));
  std::vector<CharacterInstance> beside_zoe =
      party(member("Paladin", true), member("Anna", false));
  EXPECT_EQ(DerivedStatsFor(member("Anna", false), skills, {}, beside_anna)
                .skill_stats.attack(),
            15);
  EXPECT_EQ(DerivedStatsFor(member("Zoe", false), skills, {}, beside_zoe)
                .skill_stats.attack(),
            0);

  std::vector<CharacterInstance> beside_bob =
      party(member("Aaron", true), member("Zoe", false));
  EXPECT_EQ(DerivedStatsFor(member("Bob", false), skills, {}, beside_bob)
                .skill_stats.attack(),
            15);
}

// A skill one ally supersedes is lost for the whole party: a Bishop's Advanced
// Blessing turns off the Cleric's Bless beside it.
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

// An ally gives what is active in their own book: a Bishop's Advanced Blessing,
// not the Bless under it, however many levels they put in the older skill.
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

// Groups are settled across the party too: an archer's Sharp Eyes and the
// Decent one the character bought are two sources of one lever, wherever they
// come from.
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

// The toggle, and the form it switches to in place of Bless. Both are put in
// the Swordman's book so one character can have both.
Skill Toggle() {
  Skill skill;
  skill.set_name("Righteously Indignant");
  skill.set_kind(SKILL_KIND_ACTIVE);
  PlaceIn(skill, JOB_ADVANCEMENT_SWORDMAN);
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

// The toggle's own grant doesn't depend on it being on: GMS marks such a grant
// "[Passive Effects]", which is permanent once learned.
TEST_F(ToggleTest, WhatTheSwitchItselfGrantsIsPermanent) {
  std::map<std::string, Skill> skills = {{"toggle", Toggle()}};
  CharacterInstance c = MakeCharacter(rng_, 100, 0);
  ASSERT_TRUE(c.LearnSkill(Toggle()));
  EXPECT_EQ(DerivedStatsFor(c, skills).skill_stats.magic_attack(), 50);
  ASSERT_TRUE(c.ToggleSkill(Toggle()));
  EXPECT_EQ(DerivedStatsFor(c, skills).skill_stats.magic_attack(), 50);
}

// A character who never learned the toggle has it off, so every form in the
// catalog is dormant for them, and a form's party part never reaches the party.
TEST_F(ToggleTest, AFormSleepsForWhoeverLacksTheSwitch) {
  std::map<std::string, Skill> skills = {{"bless", Bless()},
                                         {"form", VengeanceForm()}};
  CharacterInstance c = MakeCharacter(rng_, 100, 0);
  ASSERT_TRUE(c.LearnSkill(Bless(), 10));
  EXPECT_EQ(DerivedStatsFor(c, skills).skill_stats.attack(), 15);
}

// Blessed Ensemble isn't a buff: it pays per ally present, so two allies with
// it both pay.
TEST_F(DerivedStatsTest, AStackingGrantPaysOncePerAlly) {
  Skill ensemble;
  ensemble.set_name("Blessed Ensemble");
  ensemble.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(ensemble, JOB_ADVANCEMENT_SWORDMAN);
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

// Neither filtering rule applies to a stacking grant. The Bishop who replaced
// their own Ensemble with Harmony still pays, and so does the Cleric beside
// them, whose Ensemble the Bishop's book never touched.
TEST_F(DerivedStatsTest, AStackingGrantIsNotThinnedBySupersession) {
  Skill ensemble;
  ensemble.set_name("Blessed Ensemble");
  ensemble.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(ensemble, JOB_ADVANCEMENT_SWORDMAN);
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

// Two allies with the same buff count as one buff, at the better level.
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

// Combat Orders reaches the party the same way, and the ally's own level in it
// sets how many levels they give.
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

// Equips a ring with `lines`, all at `rank`, on an item of `item_level`.
void EquipPotentialRing(CharacterInstance& character, int item_level,
                        PotentialRank rank,
                        const std::vector<PotentialLineType>& lines) {
  EquipPrototype ring;
  ring.set_name("Potted Ring");
  // It gets its own slot, so a test can wear a plain stat item alongside it.
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

// GMS's rule, which differs from Maple Warrior's on purpose: a potential's
// %stat applies to the AP pool and all worn gear, its own flat lines included.
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

// The rule [[final-stats]] relies on: a symbol's stat is final, so a %stat line
// may not multiply it, even though the symbol is worn like other gear.
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
  // All Stats % gives one rank lower: 9% at Legendary on a level 100 item.
  EXPECT_EQ(stats.skill_stats.str(), 90);
  EXPECT_EQ(stats.skill_stats.dex(), 90);
  EXPECT_EQ(stats.skill_stats.int_(), 90);
  EXPECT_EQ(stats.skill_stats.luk(), 90);
}

// The two attacks are separate here and nowhere else: a %ATT line on a staff is
// worth nothing, but a skill granting attack_pct still gives both.
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

// Two ignored-defence lines combine multiplicatively, like every pair: 35% and
// 40% leave 39% of the defence, not 25%.
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

// The total a %stat line multiplies is recovered from the finished stats, so a
// caller pricing a potential the character isn't wearing gets the same answer
// the fold would. The worn potential's share is subtracted first, not
// multiplied again.
TEST(PotentialStatsTest, StatGrantPricesAPotentialTheCharacterIsNotWearing) {
  std::mt19937 rng(1);
  CharacterInstance c = MakeStatCharacter(rng, 1000, 0, 0, 0);
  EquipPotentialRing(c, 150, POTENTIAL_RANK_UNIQUE,
                     {POTENTIAL_LINE_TYPE_STR_PCT});
  DerivedStats stats = DerivedStatsFor(c, {});
  // 9% of the 1000 AP, and nothing else is worn.
  EXPECT_EQ(stats.potential_stats.str(), 90);
  EXPECT_EQ(PotentialStatGrant(c, stats, c.potential_totals()).str(), 90);

  // A Legendary line on the same item gives 12% of the same 1000, not of 1090.
  PotentialTotals better;
  better.str_pct = 0.12;
  EXPECT_EQ(PotentialStatGrant(c, stats, better).str(), 120);

  // An empty potential removes the whole worn share.
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

// Boss damage is worth nothing while farming and normal damage nothing while
// bossing, so each raises its own mode's combat power and not the other's.
// Level 200 with a weapon, so there is something to change.
TEST(CharacterCombatPowerTest, CountsTheModesMonsterOnly) {
  std::mt19937 rng(1);
  Character bare;
  bare.set_level(200);
  bare.set_job(JOB_SWORDMAN);
  bare.set_job_stage(1);
  bare.mutable_allocated_stats()->set_str(400);

  Character spent = bare;
  (*PresetOf(*spent.mutable_hyper_stats(), StatPreset::kFirst)
        .mutable_levels())[HYPER_STAT_FIELD_NORMAL_DAMAGE] = 10;
  (*PresetOf(*spent.mutable_hyper_stats(), StatPreset::kSecond)
        .mutable_levels())[HYPER_STAT_FIELD_BOSS_DAMAGE] = 10;

  CharacterInstance nothing(rng, std::move(bare));
  nothing.set_autoswap_presets(true);
  EquipAttackWeapon(nothing);
  CharacterInstance c(rng, std::move(spent));
  c.set_autoswap_presets(true);
  EquipAttackWeapon(c);

  int baseline = CharacterCombatPower(nothing, {});
  // The same levels on each side, so the two modes come out equal, and both
  // above a character who has spent nothing.
  EXPECT_GT(CharacterCombatPower(c, {}, Activity::kFarming), baseline);
  EXPECT_EQ(CharacterCombatPower(c, {}, Activity::kFarming),
            CharacterCombatPower(c, {}, Activity::kBossing));

  // And boss damage gives nothing under the farming allocation.
  Character misplaced;
  misplaced.set_level(200);
  misplaced.set_job(JOB_SWORDMAN);
  misplaced.set_job_stage(1);
  misplaced.mutable_allocated_stats()->set_str(400);
  (*PresetOf(*misplaced.mutable_hyper_stats(), StatPreset::kFirst)
        .mutable_levels())[HYPER_STAT_FIELD_BOSS_DAMAGE] = 10;
  CharacterInstance boss_only(rng, std::move(misplaced));
  boss_only.set_autoswap_presets(true);
  EquipAttackWeapon(boss_only);
  EXPECT_EQ(CharacterCombatPower(boss_only, {}, Activity::kFarming), baseline);
}

// A caller may choose the gear preset, which is needed to price a piece against
// a preset no activity uses. Everything follows that choice (stats, potentials
// and the weapon in hand) rather than some of it reading the activity's preset.
TEST(CharacterCombatPowerTest, ReadsTheGearPresetTheCallerNames) {
  std::mt19937 rng(1);
  Character proto;
  proto.set_level(200);
  proto.set_job(JOB_SWORDMAN);
  proto.set_job_stage(1);
  proto.mutable_allocated_stats()->set_str(400);
  CharacterInstance c(rng, std::move(proto));
  c.set_autoswap_presets(true);
  EquipAttackWeapon(c);

  EquipPrototype hat;
  hat.set_name("Drop Hat");
  hat.set_equip_slot(EQUIP_SLOT_HAT);
  hat.mutable_base_stats()->set_str(500);
  c.PickUp(std::make_unique<EquipInstance>(hat));
  ASSERT_TRUE(c.Equip(c.inventory().size() - 1, kDropPreset));

  const int farming = CharacterCombatPower(c, {}, Activity::kFarming);
  EXPECT_EQ(CharacterCombatPower(c, {}, Activity::kFarming, StatPreset::kFirst),
            farming)
      << "naming the preset the activity already names changes nothing";
  EXPECT_GT(CharacterCombatPower(c, {}, Activity::kFarming, kDropPreset),
            farming)
      << "the drop preset wears a hat no activity would have reached";
}

}  // namespace
}  // namespace ms
