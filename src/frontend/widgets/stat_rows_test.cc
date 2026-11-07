#include "src/frontend/widgets/stat_rows.h"

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "src/character/character_stats.h"
#include "src/character/consumables.h"
#include "src/character/hyper_stats.h"
#include "src/character/sacred_power.h"
#include "src/character/skill_placement.h"
#include "src/combat/damage.h"
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

  // A bow master at `job_stage`, for testing the 5th job difference.
  CharacterInstance MakeArcher(int job_stage) {
    Character proto;
    proto.set_level(200);
    proto.set_job(JOB_BOW_MASTER);
    proto.set_job_stage(job_stage);
    proto.mutable_allocated_stats()->set_dex(40);
    (*proto.mutable_sp_by_stage())[1] = 20;
    return CharacterInstance(rng_, std::move(proto));
  }

  // A passive granting more crit rate than any attack can roll against.
  static Skill CritPassive(JobAdvancement advancement) {
    Skill skill;
    skill.set_name("Sharp Eyes");
    skill.set_kind(SKILL_KIND_PASSIVE);
    PlaceIn(skill, advancement);
    skill.set_max_level(1);
    skill.mutable_base()->set_crit_rate(1.2);
    return skill;
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

  // A passive that scales the character's whole attack, up or down.
  Skill AttackPercentSkill(double share) {
    Skill skill;
    skill.set_name("Marksmanship");
    skill.set_kind(SKILL_KIND_PASSIVE);
    PlaceIn(skill, JOB_ADVANCEMENT_SWORDMAN);
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
  // The Character panel drops the end of this list on a short terminal, and All
  // Stats lays it out in two columns, so both depend on this order. The empty
  // label is the rule before the non-combat rows.
  EXPECT_EQ(labels,
            (std::vector<std::string>{
                "Damage Range", "Attack", "Magic Attack", "Final Damage",
                "Damage", "Boss Damage", "Normal Damage", "Ignore DEF",
                "Critical Rate", "Critical Damage", "Buff Duration",
                "Attack Speed", "", "Meso Drop Rate", "Item Drop Rate",
                "Additional EXP", "Arcane Force"}));
  EXPECT_TRUE(lines[12].rule) << "the empty row is the rule, not a blank stat";
}

// The Sacred Power row appears at the Grandis level, not before.
TEST_F(StatRowsTest, SacredPowerRowOpensWithGrandis) {
  CharacterInstance c = MakeWarrior();
  auto has_row = [](const std::vector<StatLine>& lines) {
    for (const StatLine& line : lines) {
      if (line.label == "Sacred Power") {
        return true;
      }
    }
    return false;
  };
  EXPECT_FALSE(has_row(ExtraStatLines(c, {})));
  Character proto = c.proto();
  proto.set_level(kGrandisLevel);
  CharacterInstance grandis(rng_, std::move(proto));
  std::vector<StatLine> lines = ExtraStatLines(grandis, {});
  ASSERT_TRUE(has_row(lines));
  EXPECT_EQ(lines.back().label, "Sacred Power");
  EXPECT_EQ(lines.back().value, "0");
}

// The panel shows the same list, revealed as the character advances. The All
// Stats screen always shows every stat.
TEST_F(StatRowsTest, ThePanelsListOpensUpWithEachAdvancement) {
  Character proto;
  proto.set_level(60);  // high enough that only the job can block it
  proto.set_job(JOB_BEGINNER);
  CharacterInstance beginner(rng_, std::move(proto));
  EXPECT_TRUE(PanelExtraStatLines(beginner, account_, {}).empty());
  EXPECT_EQ(ExtraStatLines(beginner, {}).size(), 17u);

  CharacterInstance first = MakeWarrior();
  std::vector<std::string> labels;
  for (const StatLine& line : PanelExtraStatLines(first, account_, {})) {
    labels.push_back(line.label);
  }
  EXPECT_EQ(labels, (std::vector<std::string>{"Damage Range", "Attack",
                                              "Magic Attack", "Attack Speed"}));

  // The second advancement adds the percent rows, but not the rows for things
  // the player hasn't reached yet, nor the rule above them.
  Character second_proto;
  second_proto.set_level(35);
  second_proto.set_job(JOB_SPEARMAN);
  second_proto.set_job_stage(2);
  CharacterInstance second(rng_, std::move(second_proto));
  EXPECT_EQ(PanelExtraStatLines(second, account_, {}).size(), 9u);

  Character third_proto;
  third_proto.set_level(70);
  third_proto.set_job(JOB_BERSERKER);
  third_proto.set_job_stage(3);
  CharacterInstance third(rng_, std::move(third_proto));
  EXPECT_EQ(PanelExtraStatLines(third, account_, {}).size(), 17u);
}

TEST_F(StatRowsTest, TheDamageLeversReadAsPercentages) {
  Skill levers = LeverPassive();
  std::map<std::string, Skill> skills = {{"levers", levers}};
  CharacterInstance c = MakeWarrior();
  ASSERT_TRUE(c.LearnSkill(levers, 1));

  std::vector<StatLine> lines = ExtraStatLines(c, skills);
  EXPECT_EQ(ValueOf(lines, "Damage"), "7.50%");
  EXPECT_EQ(ValueOf(lines, "Final Damage"), "5.00%");
  // Crit includes the base pair every character has, under the skill's own.
  EXPECT_EQ(ValueOf(lines, "Critical Rate"), "25.00%");
  EXPECT_EQ(ValueOf(lines, "Critical Damage"), "37.50%");
}

// A character who has bought nothing still crits, ignores some defence and
// keeps buffs longer, so the page shows those four bases instead of 0.00%. The
// fifth base, ignored elemental resistance, has no row (see constants.h).
TEST_F(StatRowsTest, TheBasesEveryCharacterCarriesAreShown) {
  CharacterInstance c = MakeWarrior();
  std::vector<StatLine> lines = ExtraStatLines(c, {});
  EXPECT_EQ(ValueOf(lines, "Critical Rate"), "5.00%");
  EXPECT_EQ(ValueOf(lines, "Critical Damage"), "35.00%");
  EXPECT_EQ(ValueOf(lines, "Ignore DEF"), "10.00%");
  EXPECT_EQ(ValueOf(lines, "Buff Duration"), "10.00%");
}

// A rate above 100% promises damage no attack can deal, since every roll is
// capped there. The archer's 5th job is the only build that uses the excess,
// and the only one shown it.
TEST_F(StatRowsTest, CritRateReadsCappedBelowTheArchersFifthJob) {
  Skill warrior_crit = CritPassive(JOB_ADVANCEMENT_SWORDMAN);
  std::map<std::string, Skill> warrior_skills = {{"crit", warrior_crit}};
  CharacterInstance warrior = MakeWarrior();
  ASSERT_TRUE(warrior.LearnSkill(warrior_crit, 1));
  EXPECT_EQ(ValueOf(ExtraStatLines(warrior, warrior_skills), "Critical Rate"),
            "100.00%");

  Skill archer_crit = CritPassive(JOB_ADVANCEMENT_ARCHER);
  std::map<std::string, Skill> archer_skills = {{"crit", archer_crit}};
  CharacterInstance fourth = MakeArcher(4);
  ASSERT_TRUE(fourth.LearnSkill(archer_crit, 1));
  EXPECT_EQ(ValueOf(ExtraStatLines(fourth, archer_skills), "Critical Rate"),
            "100.00%");

  CharacterInstance fifth = MakeArcher(kFifthJobStage);
  ASSERT_TRUE(fifth.LearnSkill(archer_crit, 1));
  EXPECT_EQ(ValueOf(ExtraStatLines(fifth, archer_skills), "Critical Rate"),
            "125.00%");
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

// Staffs are Slow, but no magician attacks at Slow, so the row has to show the
// speed they attack at, not the weapon's.
TEST_F(StatRowsTest, AMagiciansAttackSpeedIgnoresTheStaff) {
  EquipPrototype staff;
  staff.set_name("Staff");
  staff.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  staff.set_attack_speed(ATTACK_SPEED_SLOW_1);

  CharacterInstance mage = MakeMagician();
  mage.PickUp(std::make_unique<EquipInstance>(staff));
  mage.Equip(0);
  EXPECT_EQ(ValueOf(ExtraStatLines(mage, {}), "Attack Speed"), "Average");

  // The same staff on a job that does use its speed reads the staff's stage.
  CharacterInstance warrior = MakeWarrior();
  warrior.PickUp(std::make_unique<EquipInstance>(staff));
  warrior.Equip(0);
  EXPECT_EQ(ValueOf(ExtraStatLines(warrior, {}), "Attack Speed"), "Slow 1");
}

TEST_F(StatRowsTest, AttackSpeedStopsAtTheSoftCap) {
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
  EXPECT_EQ(ValueOf(ExtraStatLines(c, skills), "Attack Speed"), "Fastest 1");
}

// The two buffs affect different activities, and each tab shows only its own.
TEST_F(StatRowsTest, EachBuffShowsOnTheTabItPaysOn) {
  Character proto;
  proto.set_level(190);
  proto.set_job(JOB_SWORDMAN);
  proto.set_job_stage(1);
  CharacterInstance c(rng_, std::move(proto));
  EquipPrototype sword;
  sword.set_name("Sword");
  sword.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  sword.set_attack_speed(ATTACK_SPEED_FASTER);
  c.PickUp(std::make_unique<EquipInstance>(sword));
  ASSERT_TRUE(c.Equip(0));
  ASSERT_TRUE(c.ToggleConsumable(CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION));
  ASSERT_TRUE(c.ToggleConsumable(CONSUMABLE_TYPE_EXTREME_GREEN_POTION));

  std::vector<StatLine> farm = ExtraStatLines(c, {}, Activity::kFarming);
  EXPECT_EQ(ValueOf(farm, "Meso Drop Rate"), "44.00%");
  EXPECT_EQ(ValueOf(farm, "Item Drop Rate"), "20.00%");
  EXPECT_EQ(ValueOf(farm, "Attack Speed"), "Faster");

  std::vector<StatLine> boss = ExtraStatLines(c, {}, Activity::kBossing);
  EXPECT_EQ(ValueOf(boss, "Meso Drop Rate"), "0.00%");
  EXPECT_EQ(ValueOf(boss, "Item Drop Rate"), "0.00%");
  EXPECT_EQ(ValueOf(boss, "Attack Speed"), "Fastest 1");
}

TEST_F(StatRowsTest, TheMainStatsAreTheFourApStats) {
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
  // Down the left column and then the right, so the screen reads STR/INT over
  // DEX/LUK.
  EXPECT_EQ(labels, (std::vector<std::string>{"STR", "DEX", "INT", "LUK"}));
  // The breakdown comes before the total, so the totals still line up at the
  // end.
  EXPECT_EQ(ValueOf(lines, "STR"), "(40+5) 45");
  EXPECT_EQ(ValueOf(lines, "LUK"), "0");
}

// The Attack row shows what a percentage did, including a lowering one, since
// an unexplained smaller number looks like a bug.
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

  // The same row with nothing added or removed shows only the total.
  CharacterInstance bare = MakeWarrior();
  EquipBow(bare);
  EXPECT_EQ(ValueOf(ExtraStatLines(bare, {}), "Attack"), "80");
}

// The reader's GMS Combat Power option picks the formula, so a %attack passive
// counts only while it is off.
TEST_F(StatRowsTest, ShownCombatPowerFollowsTheReadersOption) {
  CharacterInstance c = MakeWarrior();
  ASSERT_TRUE(c.LearnSkill(AttackPercentSkill(0.25), 1));
  EquipBow(c);
  const std::map<std::string, Skill> skills = SkillMap(0.25);
  EXPECT_EQ(ShownCombatPower(account_, c, skills),
            CharacterCombatPower(c, skills));
  account_.SetGmsCombatPower(true);
  EXPECT_EQ(ShownCombatPower(account_, c, skills),
            GmsCharacterCombatPower(c, skills));
  EXPECT_EQ(GmsCharacterOffense(c, skills).attack, 80);
}

// The two allocations are separate sets of numbers, and every row reads the one
// it is given. That is what the Farm/Boss tabs switch.
TEST_F(StatRowsTest, TheRowsReadThePresetTheyAreGiven) {
  Character proto;
  proto.set_level(200);
  proto.set_job(JOB_SWORDMAN);
  proto.set_job_stage(1);
  HyperStats& hyper = *proto.mutable_hyper_stats();
  (*PresetOf(hyper, StatPreset::kFirst)
        .mutable_levels())[HYPER_STAT_FIELD_CRIT_DAMAGE] = 5;
  (*PresetOf(hyper, StatPreset::kSecond)
        .mutable_levels())[HYPER_STAT_FIELD_CRIT_DAMAGE] = 9;
  CharacterInstance c(rng_, std::move(proto));
  c.set_autoswap_presets(true);

  EXPECT_EQ(ValueOf(ExtraStatLines(c, {}), "Critical Damage"), "40.00%");
  EXPECT_EQ(
      ValueOf(ExtraStatLines(c, {}, Activity::kBossing), "Critical Damage"),
      "44.00%");
}

// The card's three parts add up to the Stats tab's number: %stat multiplies
// the AP and the potential's own pile, and a Hyper Stat is added after it.
TEST_F(StatRowsTest, AStatsBreakdownAddsUpToItsRow) {
  auto make = [this](int hyper_level) {
    Character proto;
    proto.set_level(200);
    proto.set_job(JOB_SWORDMAN);
    proto.set_job_stage(1);
    proto.mutable_allocated_stats()->set_str(1000);
    (*PresetOf(*proto.mutable_hyper_stats(), StatPreset::kFirst)
          .mutable_levels())[HYPER_STAT_FIELD_STR] = hyper_level;
    CharacterInstance c(rng_, std::move(proto));
    EquipPrototype ring;
    ring.set_name("Potted Ring");
    ring.set_equip_slot(EQUIP_SLOT_HAT);
    ring.set_required_level(100);
    Equip state;
    Potential* potential = state.mutable_main_potential();
    potential->set_rank(POTENTIAL_RANK_LEGENDARY);
    PotentialLine* line = potential->add_lines();
    line->set_type(POTENTIAL_LINE_TYPE_STR_PCT);
    line->set_rank(POTENTIAL_RANK_LEGENDARY);
    c.PickUp(std::make_unique<EquipInstance>(ring, state));
    c.Equip(c.inventory().size() - 1);
    return c;
  };
  CharacterInstance plain = make(0);
  CharacterInstance hyper = make(5);
  const StatBreakdown before = StatBreakdownFor(plain, {}, InspectStat::kStr);
  const StatBreakdown after = StatBreakdownFor(hyper, {}, InspectStat::kStr);

  EXPECT_EQ(after.base, 1000);
  EXPECT_DOUBLE_EQ(after.pct, 0.12);
  EXPECT_EQ(after.total, after.base + 120 + after.final_stat);
  // The Hyper Stat lands in Final Stat alone: %stat doesn't multiply it.
  EXPECT_EQ(after.base, before.base);
  EXPECT_EQ(after.final_stat - before.final_stat,
            static_cast<int>(hyper.hyper_stat_bonus(HYPER_STAT_FIELD_STR,
                                                    StatPreset::kFirst)));
  const std::string row = ValueOf(MainStatLines(hyper, {}), "STR");
  EXPECT_NE(row.find(") " + std::to_string(after.total)), std::string::npos)
      << row;
}

// GMS's Damage Range: weapon constant, 4 x primary + secondary, attack, %
// damage and final damage over 100, and the floor is mastery's share of it.
TEST_F(StatRowsTest, DamageRangeIsTheFormulasCharacterHalf) {
  OffenseStats offense;
  offense.primary = 1000;
  offense.secondary = 200;
  offense.attack = 500;
  offense.weapon_constant = 1.34;
  offense.damage_pct = 0.5;
  offense.boss_pct = 3.0;  // left out, as GMS's window leaves it out
  offense.crit_dmg = 1.0;  // left out too
  offense.final_dmg_pct = 0.2;
  offense.mastery = 0.9;
  const DamageRangeValues range = DamageRange(offense);
  // 4200 / 100 * 500 * 1.34 * 1.5 * 1.2 = 50652.
  EXPECT_EQ(range.max, 50652);
  EXPECT_EQ(range.min, 45586);
}

// Both ends fit the Stats tab's value column: written out under ten thousand,
// then three digits and a unit, rounded down.
TEST_F(StatRowsTest, TheDamageRangeRowIsShortEnoughForThePanel) {
  CharacterInstance c = MakeWarrior();
  const std::string row = ValueOf(ExtraStatLines(c, {}), "Damage Range");
  EXPECT_NE(row.find(" ~ "), std::string::npos) << row;
  const DamageRangeValues range = DamageRange(CharacterOffense(c, {}));
  ASSERT_LT(range.max, 10000);
  EXPECT_EQ(row, std::to_string(range.min) + " ~ " + std::to_string(range.max));
}

TEST_F(StatRowsTest, TheDamageRangesUnitsKeepThreeDigits) {
  EXPECT_EQ(DamageRangeText({.min = 9999, .max = 10000}), "9,999 ~ 10K");
  EXPECT_EQ(DamageRangeText({.min = 45586, .max = 50652}), "45.5K ~ 50.6K");
  EXPECT_EQ(DamageRangeText({.min = 1239999, .max = 999999999}),
            "1.23M ~ 999M");
  EXPECT_EQ(DamageRangeText({.min = 12345678901, .max = 2000000000000}),
            "12.3B ~ 2T");
}

// Attack has no final source, so all of it is base and %Attack.
TEST_F(StatRowsTest, AttacksBreakdownHasNoFinalPart) {
  CharacterInstance c = MakeWarrior();
  const StatBreakdown attack = StatBreakdownFor(c, {}, InspectStat::kAttack);
  EXPECT_EQ(attack.final_stat, 0);
  EXPECT_EQ(
      std::to_string(attack.total),
      ValueOf(ExtraStatLines(c, {}), "Attack")
          .substr(ValueOf(ExtraStatLines(c, {}), "Attack").rfind(' ') + 1));
}

// The Boss tab's drop rate is the boss drop roll's, which wears the Drop
// preset; the farm tab keeps its own gear.
TEST_F(StatRowsTest, AutoswapsBossDropRateReadsTheDropPreset) {
  CharacterInstance c = MakeWarrior();
  c.set_autoswap_presets(true);
  EquipPrototype charm;
  charm.set_name("Drop Charm");
  charm.set_equip_slot(EQUIP_SLOT_PENDANT);
  charm.mutable_base_stats()->set_item_drop_rate(40);
  c.PickUp(std::make_unique<EquipInstance>(charm));
  ASSERT_TRUE(c.Equip(0, kDropPreset));

  EXPECT_EQ(
      ValueOf(ExtraStatLines(c, {}, Activity::kBossing), "Item Drop Rate"),
      "40.00%");
  EXPECT_EQ(
      ValueOf(ExtraStatLines(c, {}, Activity::kFarming), "Item Drop Rate"),
      "0.00%");
}

TEST(CombatPowerTextTest, SpellsItOutUntilSevenFigures) {
  EXPECT_EQ(CombatPowerText(0), "Combat Power 0");
  EXPECT_EQ(CombatPowerText(999999), "Combat Power 999,999");
  EXPECT_EQ(CombatPowerText(1000000), "CP 1,000,000");
}

}  // namespace
}  // namespace ms
