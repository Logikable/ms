#include "src/frontend/widgets/stat_rows.h"

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "src/item/equip_instance.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/skill.pb.h"

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

  // A passive granting one of every lever the extra stats report.
  Skill Levers() {
    Skill skill;
    skill.set_name("Levers");
    skill.set_kind(SKILL_KIND_PASSIVE);
    skill.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
    skill.set_max_level(1);
    skill.mutable_base()->set_damage_pct(0.075);
    skill.mutable_base()->set_final_dmg_pct(0.05);
    skill.mutable_base()->set_crit_rate(0.2);
    skill.mutable_base()->set_crit_dmg(0.025);
    skill.mutable_base()->set_attack_speed(2);
    return skill;
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
  EXPECT_EQ(labels, (std::vector<std::string>{
                        "Attack", "Magic Attack", "Damage", "Final Damage",
                        "Boss Damage", "Ignore DEF", "Critical Rate",
                        "Critical Damage", "Buff Duration", "Attack Speed",
                        "Elemental Resist", "Status Resist", "Meso Drop Rate",
                        "Item Drop Rate", "Additional EXP", "Defense"}));
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
  EXPECT_EQ(labels, (std::vector<std::string>{
                        "Attack", "Magic Attack", "Attack Speed",
                        "Elemental Resist", "Status Resist", "Defense"}));

  // The second opens the percent block, but not the five that pay out on
  // something the player has not met yet.
  Character second_proto;
  second_proto.set_level(35);
  second_proto.set_job(JOB_SPEARMAN);
  second_proto.set_job_stage(2);
  CharacterInstance second(rng_, std::move(second_proto));
  EXPECT_EQ(PanelExtraStatLines(second, account_, {}).size(), 11u);

  Character third_proto;
  third_proto.set_level(70);
  third_proto.set_job(JOB_BERSERKER);
  third_proto.set_job_stage(3);
  CharacterInstance third(rng_, std::move(third_proto));
  EXPECT_EQ(PanelExtraStatLines(third, account_, {}).size(), 16u);
}

TEST_F(StatRowsTest, TheDamageLeversReadAsPercentages) {
  Skill levers = Levers();
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
  Skill levers = Levers();  // +2 stages
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
  Skill levers = Levers();
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

// A skill that takes DEF away rather than granting it: the row has to say so,
// or the player reads a smaller number with nothing explaining it.
TEST_F(StatRowsTest, ALostStatReadsAsASubtraction) {
  Skill reckless;
  reckless.set_name("Reckless Hunt");
  reckless.set_kind(SKILL_KIND_PASSIVE);
  reckless.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  reckless.set_max_level(1);
  reckless.mutable_base()->set_def_pct(-0.25);
  std::map<std::string, Skill> skills = {{"reckless", reckless}};
  CharacterInstance c = MakeWarrior();
  ASSERT_TRUE(c.LearnSkill(reckless, 1));

  // 40 STR buys 60 DEF, and a quarter of it goes.
  EXPECT_EQ(ValueOf(ExtraStatLines(c, skills), "Defense"), "(60-15) 45");
  // The same row with nothing taken or added stays a plain total.
  CharacterInstance bare = MakeWarrior();
  EXPECT_EQ(ValueOf(ExtraStatLines(bare, {}), "Defense"), "60");
}

// Marksmanship's percentage over a worn weapon, read off the row that has to
// show the player it landed.
TEST_F(StatRowsTest, AttackShowsWhatAPercentageAddedToIt) {
  Skill marks;
  marks.set_name("Marksmanship");
  marks.set_kind(SKILL_KIND_PASSIVE);
  marks.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  marks.set_max_level(1);
  marks.mutable_base()->set_attack_pct(0.25);
  std::map<std::string, Skill> skills = {{"marksmanship", marks}};
  CharacterInstance c = MakeWarrior();
  ASSERT_TRUE(c.LearnSkill(marks, 1));

  EquipPrototype bow;
  bow.set_name("Bow");
  bow.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  bow.mutable_base_stats()->set_attack(80);
  c.PickUp(std::make_unique<EquipInstance>(bow));
  c.Equip(0);

  EXPECT_EQ(ValueOf(ExtraStatLines(c, skills), "Attack"), "(80+20) 100");
}

TEST(CombatPowerTextTest, SpellsItOutUntilSevenFigures) {
  EXPECT_EQ(CombatPowerText(0), "Combat Power 0");
  EXPECT_EQ(CombatPowerText(999999), "Combat Power 999,999");
  EXPECT_EQ(CombatPowerText(1000000), "CP 1,000,000");
}

}  // namespace
}  // namespace ms
