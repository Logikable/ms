#include "src/frontend/widgets/stat_rows.h"

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "src/character/hyper_stats.h"
#include "src/item/equip_instance.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/skill.pb.h"
#include "src/testing/prototypes.h"

namespace ms {
namespace {

class StatRowsTest : public testing::Test {
 protected:
  CharacterInstance MakeWarrior() {
    Character proto;
    proto.set_level(15);
    proto.set_job(JOB_SWORDMAN);
    proto.set_job_stage(1);
    proto.mutable_allocated_stats()->set_str(40);
    (*proto.mutable_sp_by_stage())[1] = 20;
    return CharacterInstance(rng_, std::move(proto));
  }

  CharacterInstance MakeMagician() {
    Character proto;
    proto.set_level(15);
    proto.set_job(JOB_MAGICIAN);
    proto.set_job_stage(1);
    proto.mutable_allocated_stats()->set_int_(40);
    (*proto.mutable_sp_by_stage())[1] = 20;
    return CharacterInstance(rng_, std::move(proto));
  }

  // A passive that scales the whole of the character's attack, up or down.
  Skill AttackPercentSkill(double share) {
    Skill skill;
    skill.set_name("Marksmanship");
    skill.set_kind(SKILL_KIND_PASSIVE);
    skill.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
    skill.set_max_level(1);
    skill.mutable_base()->set_attack_pct(share);
    return skill;
  }

  std::map<std::string, Skill> SkillMap(double share) {
    return {{"marksmanship", AttackPercentSkill(share)}};
  }

  void EquipBow(CharacterInstance& c) {
    EquipPrototype bow;
    bow.set_name("Bow");
    bow.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
    bow.mutable_base_stats()->set_attack(80);
    c.PickUp(std::make_unique<EquipInstance>(bow));
    c.Equip(0);
  }

  // The value beside `label`, or "" when the list has no such row.
  static std::string ValueOf(const std::vector<StatLine>& lines,
                             const std::string& label) {
    for (const StatLine& line : lines) {
      if (line.label == label) {
        return line.value;
      }
    }
    return "";
  }

  std::mt19937 rng_{0};
  AccountInstance account_;
};

TEST_F(StatRowsTest, TheExtrasAreInPriorityOrder) {
  CharacterInstance c = MakeWarrior();
  std::vector<StatLine> lines = ExtraStatLines(c, {});
  std::vector<std::string> labels;
  for (const StatLine& line : lines) {
    labels.push_back(line.label);
  }
  // The Character panel drops the tail of this list on a short terminal, and
  // the All Stats screen pairs it two to a row. Both depend on this order.
  // The empty label is the rule between the combat stats and the three that
  // are not about a fight.
  EXPECT_EQ(labels, (std::vector<std::string>{
                        "Attack", "Magic Attack", "Damage", "Final Damage",
                        "Boss Damage", "Normal Damage", "Ignore DEF",
                        "Critical Rate", "Critical Damage", "Buff Duration",
                        "Attack Speed", "", "Meso Drop Rate", "Item Drop Rate",
                        "Additional EXP", "Arcane Force"}));
  EXPECT_TRUE(lines[11].rule) << "the empty row is the rule, not a blank stat";
}

// The panel's list is the same one, opened up by the advancements. The All
// Stats screen's is not touched: it is where every stat always is.
TEST_F(StatRowsTest, ThePanelsListOpensUpWithEachAdvancement) {
  Character proto;
  proto.set_level(60);  // high enough that only the job can be holding it back
  proto.set_job(JOB_BEGINNER);
  CharacterInstance beginner(rng_, std::move(proto));
  EXPECT_TRUE(PanelExtraStatLines(beginner, account_, {}).empty());
  EXPECT_EQ(ExtraStatLines(beginner, {}).size(), 16u);

  CharacterInstance first = MakeWarrior();
  std::vector<std::string> labels;
  for (const StatLine& line : PanelExtraStatLines(first, account_, {})) {
    labels.push_back(line.label);
  }
  EXPECT_EQ(labels, (std::vector<std::string>{"Attack", "Magic Attack",
                                              "Attack Speed"}));

  // The second opens the percent block, but not the rows that pay out on
  // something the player has not met yet -- nor the rule over them.
  Character second_proto;
  second_proto.set_level(35);
  second_proto.set_job(JOB_SPEARMAN);
  second_proto.set_job_stage(2);
  CharacterInstance second(rng_, std::move(second_proto));
  EXPECT_EQ(PanelExtraStatLines(second, account_, {}).size(), 8u);

  Character third_proto;
  third_proto.set_level(70);
  third_proto.set_job(JOB_BERSERKER);
  third_proto.set_job_stage(3);
  CharacterInstance third(rng_, std::move(third_proto));
  EXPECT_EQ(PanelExtraStatLines(third, account_, {}).size(), 16u);
}

TEST_F(StatRowsTest, TheDamageLeversReadAsPercentages) {
  Skill levers = LeverPassive();
  std::map<std::string, Skill> skills = {{"levers", levers}};
  CharacterInstance c = MakeWarrior();
  ASSERT_TRUE(c.LearnSkill(levers, 1));

  std::vector<StatLine> lines = ExtraStatLines(c, skills);
  EXPECT_EQ(ValueOf(lines, "Damage"), "7.50%");
  EXPECT_EQ(ValueOf(lines, "Final Damage"), "5.00%");
  // Crit carries the base pair every character has under the skill's own.
  EXPECT_EQ(ValueOf(lines, "Critical Rate"), "25.00%");
  EXPECT_EQ(ValueOf(lines, "Critical Damage"), "37.50%");
}

// A character who has bought nothing still crits: the page says so rather
// than reading 0.00% at a character with a one-in-twenty chance of +35%.
TEST_F(StatRowsTest, CritReadsItsBaseWithNothingBehindIt) {
  CharacterInstance c = MakeWarrior();
  std::vector<StatLine> lines = ExtraStatLines(c, {});
  EXPECT_EQ(ValueOf(lines, "Critical Rate"), "5.00%");
  EXPECT_EQ(ValueOf(lines, "Critical Damage"), "35.00%");
}

TEST_F(StatRowsTest, AttackSpeedNamesTheStageOrDashesWithNoWeapon) {
  Skill levers = LeverPassive();  // +2 stages
  std::map<std::string, Skill> skills = {{"levers", levers}};
  CharacterInstance c = MakeWarrior();
  ASSERT_TRUE(c.LearnSkill(levers, 1));
  EXPECT_EQ(ValueOf(ExtraStatLines(c, skills), "Attack Speed"), "-");

  EquipPrototype sword;
  sword.set_name("Sword");
  sword.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  sword.set_attack_speed(ATTACK_SPEED_AVERAGE);
  c.PickUp(std::make_unique<EquipInstance>(sword));
  c.Equip(0);
  EXPECT_EQ(ValueOf(ExtraStatLines(c, skills), "Attack Speed"), "Fast 2");
}

// Every staff in the game is Slow, and no magician casts at Slow: the row has
// to say what they swing at, not what they hold.
TEST_F(StatRowsTest, AMagiciansAttackSpeedIgnoresTheStaff) {
  EquipPrototype staff;
  staff.set_name("Staff");
  staff.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  staff.set_attack_speed(ATTACK_SPEED_SLOW_1);

  CharacterInstance mage = MakeMagician();
  mage.PickUp(std::make_unique<EquipInstance>(staff));
  mage.Equip(0);
  EXPECT_EQ(ValueOf(ExtraStatLines(mage, {}), "Attack Speed"), "Average");

  // The same staff on someone who really does swing it reads its own stage.
  CharacterInstance warrior = MakeWarrior();
  warrior.PickUp(std::make_unique<EquipInstance>(staff));
  warrior.Equip(0);
  EXPECT_EQ(ValueOf(ExtraStatLines(warrior, {}), "Attack Speed"), "Slow 1");
}

TEST_F(StatRowsTest, AttackSpeedStopsAtTheFastestStage) {
  Skill levers = LeverPassive();
  std::map<std::string, Skill> skills = {{"levers", levers}};
  CharacterInstance c = MakeWarrior();
  ASSERT_TRUE(c.LearnSkill(levers, 1));
  EquipPrototype sword;
  sword.set_name("Sword");
  sword.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  sword.set_attack_speed(ATTACK_SPEED_FASTEST_3);
  c.PickUp(std::make_unique<EquipInstance>(sword));
  c.Equip(0);
  EXPECT_EQ(ValueOf(ExtraStatLines(c, skills), "Attack Speed"), "Fastest 3");
}

TEST_F(StatRowsTest, TheMainStatsPairUpForTheTwoColumnScreen) {
  CharacterInstance c = MakeWarrior();
  EquipPrototype sword;
  sword.set_name("Sword");
  sword.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  sword.mutable_base_stats()->set_str(5);
  c.PickUp(std::make_unique<EquipInstance>(sword));
  c.Equip(0);

  std::vector<StatLine> lines = MainStatLines(c, {});
  std::vector<std::string> labels;
  for (const StatLine& line : lines) {
    labels.push_back(line.label);
  }
  EXPECT_EQ(labels,
            (std::vector<std::string>{"HP", "MP", "STR", "INT", "DEX", "LUK"}));
  // The breakdown leads the total, so the totals still end in one column.
  EXPECT_EQ(ValueOf(lines, "STR"), "(40+5) 45");
  EXPECT_EQ(ValueOf(lines, "LUK"), "0");
}

// The Attack row shows the split, so the player can see a percentage land
// rather than only the number it left them. A skill can take attack away as
// well as add it, and the sign belongs in the breakdown: a smaller number with
// nothing explaining it reads as a bug.
TEST_F(StatRowsTest, AttackShowsWhatAPercentageDidToIt) {
  CharacterInstance up = MakeWarrior();
  ASSERT_TRUE(up.LearnSkill(AttackPercentSkill(0.25), 1));
  EquipBow(up);
  EXPECT_EQ(ValueOf(ExtraStatLines(up, SkillMap(0.25)), "Attack"),
            "(80+20) 100");

  CharacterInstance down = MakeWarrior();
  ASSERT_TRUE(down.LearnSkill(AttackPercentSkill(-0.25), 1));
  EquipBow(down);
  EXPECT_EQ(ValueOf(ExtraStatLines(down, SkillMap(-0.25)), "Attack"),
            "(80-20) 60");

  // The same row with nothing taken or added stays a plain total.
  CharacterInstance bare = MakeWarrior();
  EquipBow(bare);
  EXPECT_EQ(ValueOf(ExtraStatLines(bare, {}), "Attack"), "80");
}

// The two allocations are two sets of numbers, and every row here reads
// whichever one it is handed -- which is what the Farm/Boss tabs switch.
TEST_F(StatRowsTest, TheRowsReadThePresetTheyAreGiven) {
  Character proto;
  proto.set_level(200);
  proto.set_job(JOB_SWORDMAN);
  proto.set_job_stage(1);
  (*proto.mutable_hyper_stats()
        ->mutable_farming()
        ->mutable_levels())[HYPER_STAT_FIELD_CRIT_DAMAGE] = 5;
  (*proto.mutable_hyper_stats()
        ->mutable_bossing()
        ->mutable_levels())[HYPER_STAT_FIELD_CRIT_DAMAGE] = 9;
  CharacterInstance c(rng_, std::move(proto));

  EXPECT_EQ(ValueOf(ExtraStatLines(c, {}), "Critical Damage"), "40.00%");
  EXPECT_EQ(
      ValueOf(ExtraStatLines(c, {}, HyperPreset::kBossing), "Critical Damage"),
      "44.00%");
}

// Boss damage is worth nothing while farming and normal damage nothing while
// bossing, so each raises its own mode's combat power and neither raises the
// other's. Level 200 with a weapon, so there is a number to move at all.
TEST_F(StatRowsTest, CombatPowerCountsTheModesMonsterOnly) {
  Character bare;
  bare.set_level(200);
  bare.set_job(JOB_SWORDMAN);
  bare.set_job_stage(1);
  bare.mutable_allocated_stats()->set_str(400);

  Character spent = bare;
  (*PresetOf(*spent.mutable_hyper_stats(), HyperPreset::kFarming)
        .mutable_levels())[HYPER_STAT_FIELD_NORMAL_DAMAGE] = 10;
  (*PresetOf(*spent.mutable_hyper_stats(), HyperPreset::kBossing)
        .mutable_levels())[HYPER_STAT_FIELD_BOSS_DAMAGE] = 10;

  CharacterInstance nothing(rng_, std::move(bare));
  EquipBow(nothing);
  CharacterInstance c(rng_, std::move(spent));
  EquipBow(c);

  int baseline = CharacterCombatPower(nothing, {});
  // The same ladder either side, so the two modes come out equal -- and both
  // above a character who has spent nothing.
  EXPECT_GT(CharacterCombatPower(c, {}, HyperPreset::kFarming), baseline);
  EXPECT_EQ(CharacterCombatPower(c, {}, HyperPreset::kFarming),
            CharacterCombatPower(c, {}, HyperPreset::kBossing));

  // And the boss ladder buys nothing at all under the farming allocation.
  Character misplaced;
  misplaced.set_level(200);
  misplaced.set_job(JOB_SWORDMAN);
  misplaced.set_job_stage(1);
  misplaced.mutable_allocated_stats()->set_str(400);
  (*PresetOf(*misplaced.mutable_hyper_stats(), HyperPreset::kFarming)
        .mutable_levels())[HYPER_STAT_FIELD_BOSS_DAMAGE] = 10;
  CharacterInstance boss_only(rng_, std::move(misplaced));
  EquipBow(boss_only);
  EXPECT_EQ(CharacterCombatPower(boss_only, {}, HyperPreset::kFarming),
            baseline);
}

TEST(CombatPowerTextTest, SpellsItOutUntilSevenFigures) {
  EXPECT_EQ(CombatPowerText(0), "Combat Power 0");
  EXPECT_EQ(CombatPowerText(999999), "Combat Power 999,999");
  EXPECT_EQ(CombatPowerText(1000000), "CP 1,000,000");
}

}  // namespace
}  // namespace ms
