#include "src/frontend/widgets/game_names.h"

#include <gtest/gtest.h>

#include <map>
#include <set>
#include <string>
#include <vector>

#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/text_columns.h"
#include "src/item/potential.h"
#include "src/protos/skill.pb.h"

namespace ms {
namespace {

// --- DisplayStatFor ---

TEST(DisplayStatForTest, FindsTheEntryTheFieldNames) {
  EquipStats stats;
  stats.set_luk(7);
  const DisplayStat* stat = DisplayStatFor(STAT_FIELD_LUK);
  ASSERT_NE(stat, nullptr);
  EXPECT_STREQ(stat->label, "LUK");
  EXPECT_EQ(stat->GetFrom(stats), 7);
}

// HP is spelled max_hp on EquipStats; the join is by label, so it still lands.
TEST(DisplayStatForTest, FindsAFieldWithARenamedAccessor) {
  EquipStats stats;
  stats.set_max_hp(150);
  const DisplayStat* stat = DisplayStatFor(STAT_FIELD_HP);
  ASSERT_NE(stat, nullptr);
  EXPECT_EQ(stat->GetFrom(stats), 150);
}

TEST(DisplayStatForTest, UnspecifiedFieldHasNoEntry) {
  EXPECT_EQ(DisplayStatFor(STAT_FIELD_UNSPECIFIED), nullptr);
}

// --- FormatWeaponList ---

TEST(FormatWeaponListTest, NamesOneWeaponAndJoinsSeveral) {
  EXPECT_EQ(FormatWeaponList({}), "");
  EXPECT_EQ(FormatWeaponList({EQUIP_TYPE_DAGGER}), "Dagger");
  EXPECT_EQ(FormatWeaponList({EQUIP_TYPE_DAGGER, EQUIP_TYPE_CLAW}),
            "Dagger / Claw");
}

// Both hands of one weapon is how the data says "any sword".
TEST(FormatWeaponListTest, AWholePairCollapsesToItsBareName) {
  EXPECT_EQ(FormatWeaponList(
                {EQUIP_TYPE_ONE_HANDED_SWORD, EQUIP_TYPE_TWO_HANDED_SWORD,
                 EQUIP_TYPE_ONE_HANDED_AXE, EQUIP_TYPE_TWO_HANDED_AXE}),
            "Sword / Axe");
  // The collapsed name lands where the first half was listed, not at the end.
  EXPECT_EQ(FormatWeaponList({EQUIP_TYPE_ONE_HANDED_BLUNT, EQUIP_TYPE_SPEAR,
                              EQUIP_TYPE_TWO_HANDED_BLUNT}),
            "Blunt / Spear");
}

TEST(FormatWeaponListTest, HalfAPairStaysTheWeaponItNames) {
  EXPECT_EQ(FormatWeaponList({EQUIP_TYPE_TWO_HANDED_SWORD}),
            "Two-Handed Sword");
}

// --- TagFor ---

TEST(TagForTest, EveryKindGetsAFourColumnTag) {
  Skill skill;
  skill.set_kind(SKILL_KIND_ATTACK);
  EXPECT_EQ(std::string(TagFor(skill).text), "A:  ");
  skill.set_kind(SKILL_KIND_ACTIVE);
  EXPECT_EQ(std::string(TagFor(skill).text), "A:  ");
  skill.set_kind(SKILL_KIND_AUTO_ATTACK);
  EXPECT_EQ(std::string(TagFor(skill).text), "AA: ");
  skill.set_kind(SKILL_KIND_PASSIVE);
  EXPECT_EQ(std::string(TagFor(skill).text), "P:  ");
  // A kind-less skill gets the blanks rather than a tag that would be wrong.
  skill.set_kind(SKILL_KIND_UNSPECIFIED);
  EXPECT_EQ(std::string(TagFor(skill).text), "    ");
}

// --- FormatJobCategories ---

TEST(FormatJobCategoriesTest, NamesThemOrSaysAll) {
  EquipPrototype proto;
  EXPECT_EQ(FormatJobCategories(proto), "All");
  proto.add_equip_job_categories(EQUIP_JOB_CATEGORY_UNIVERSAL);
  EXPECT_EQ(FormatJobCategories(proto), "All");

  proto.clear_equip_job_categories();
  proto.add_equip_job_categories(EQUIP_JOB_CATEGORY_WARRIOR);
  EXPECT_EQ(FormatJobCategories(proto), "Warrior");
  proto.add_equip_job_categories(EQUIP_JOB_CATEGORY_THIEF);
  EXPECT_EQ(FormatJobCategories(proto), "Warrior/Thief");
}

TEST(AttackSpeedNameTest, EveryStageHasAName) {
  for (int stage = ATTACK_SPEED_SLOWER; stage <= ATTACK_SPEED_FASTEST_3;
       ++stage) {
    EXPECT_FALSE(AttackSpeedName(static_cast<AttackSpeed>(stage)).empty())
        << "stage " << stage;
  }
  EXPECT_EQ(AttackSpeedName(ATTACK_SPEED_FAST_2), "Fast 2");
  EXPECT_EQ(AttackSpeedName(ATTACK_SPEED_UNSPECIFIED), "");
}

TEST(StatFieldNameTest, NamesTheFourAllocatableStats) {
  EXPECT_EQ(StatFieldName(STAT_FIELD_STR), "STR");
  EXPECT_EQ(StatFieldName(STAT_FIELD_DEX), "DEX");
  EXPECT_EQ(StatFieldName(STAT_FIELD_INT), "INT");
  EXPECT_EQ(StatFieldName(STAT_FIELD_LUK), "LUK");
  EXPECT_EQ(StatFieldName(STAT_FIELD_UNSPECIFIED), "");
}

// A skill kind added without a look at this reads as a passive, which is what
// happened to the first auto-attack: it inspected as " Passive " and showed no
// effects at any level.
TEST(IsActiveTest, EverythingButAPassiveIsActive) {
  Skill skill;
  skill.set_kind(SKILL_KIND_ATTACK);
  EXPECT_TRUE(IsActive(skill));
  skill.set_kind(SKILL_KIND_ACTIVE);
  EXPECT_TRUE(IsActive(skill));
  skill.set_kind(SKILL_KIND_AUTO_ATTACK);
  EXPECT_TRUE(IsActive(skill));
  skill.set_kind(SKILL_KIND_PASSIVE);
  EXPECT_FALSE(IsActive(skill));
}

// One key per stage: the tab arrives again at every advancement, and having
// seen the first is not having seen the second.

TEST(FormatSlotTest, NamesEverySlot) {
  EXPECT_EQ(FormatSlot(EQUIP_SLOT_PRIMARY_WEAPON), "Weapon");
  EXPECT_EQ(FormatSlot(EQUIP_SLOT_PROJECTILE), "Projectile");
  EXPECT_EQ(FormatSlot(EQUIP_SLOT_SECONDARY), "Secondary");
  EXPECT_EQ(FormatSlot(EQUIP_SLOT_HAT), "Hat");
  EXPECT_EQ(FormatSlot(EQUIP_SLOT_TOP), "Top");
  EXPECT_EQ(FormatSlot(EQUIP_SLOT_BOTTOM), "Bottom");
  EXPECT_EQ(FormatSlot(EQUIP_SLOT_CAPE), "Cape");
  EXPECT_EQ(FormatSlot(EQUIP_SLOT_FACE_ACCESSORY), "Face");
  EXPECT_EQ(FormatSlot(EQUIP_SLOT_EYE_ACCESSORY), "Eye");
  EXPECT_EQ(FormatSlot(EQUIP_SLOT_RING), "Ring");
  EXPECT_EQ(FormatSlot(EQUIP_SLOT_PENDANT), "Pendant");
  EXPECT_EQ(FormatSlot(EQUIP_SLOT_BELT), "Belt");
  EXPECT_EQ(FormatSlot(EQUIP_SLOT_SHOULDER), "Shoulder");
  EXPECT_EQ(FormatSlot(EQUIP_SLOT_POCKET), "Pocket");
  EXPECT_EQ(FormatSlot(EQUIP_SLOT_EARRINGS), "Earrings");
  EXPECT_EQ(FormatSlot(EQUIP_SLOT_GLOVES), "Gloves");
  EXPECT_EQ(FormatSlot(EQUIP_SLOT_SHOES), "Shoes");
  EXPECT_EQ(FormatSlot(EQUIP_SLOT_BADGE), "Badge");
  EXPECT_EQ(FormatSlot(EQUIP_SLOT_EMBLEM), "Emblem");
  EXPECT_EQ(FormatSlot(EQUIP_SLOT_MEDAL), "Medal");
  EXPECT_EQ(FormatSlot(EQUIP_SLOT_HEART), "Heart");
  EXPECT_EQ(FormatSlot(EQUIP_SLOT_UNSPECIFIED), "");
  // A ring is a ring in all four of its slots: this is what an item is, not
  // where it is worn.
  EXPECT_EQ(FormatSlot(EQUIP_SLOT_RING_4), "Ring");
  EXPECT_EQ(FormatSlot(EQUIP_SLOT_PENDANT_2), "Pendant");
}

// A slot added without a name here shows the player a blank column, which is
// how the last one nearly shipped.
TEST(FormatSlotTest, NoSlotIsLeftUnnamedOrTooWide) {
  for (int i = 1; i <= EquipSlot_MAX; ++i) {
    if (!EquipSlot_IsValid(i)) {
      continue;
    }
    EquipSlot slot = static_cast<EquipSlot>(i);
    EXPECT_FALSE(FormatSlot(slot).empty()) << EquipSlot_Name(slot);
    EXPECT_LE(FormatWornSlot(slot).size(), 10u) << EquipSlot_Name(slot);
  }
}

// A worn row says which of a family's slots it is, and leaves every slot with
// only one alone -- a character wears one hat, and "Hat 1" says nothing.
TEST(FormatWornSlotTest, NumbersOnlyTheSlotsWithSiblings) {
  EXPECT_EQ(FormatWornSlot(EQUIP_SLOT_RING), "Ring 1");
  EXPECT_EQ(FormatWornSlot(EQUIP_SLOT_RING_4), "Ring 4");
  EXPECT_EQ(FormatWornSlot(EQUIP_SLOT_PENDANT), "Pendant 1");
  EXPECT_EQ(FormatWornSlot(EQUIP_SLOT_PENDANT_2), "Pendant 2");
  EXPECT_EQ(FormatWornSlot(EQUIP_SLOT_HAT), "Hat");
  EXPECT_EQ(FormatWornSlot(EQUIP_SLOT_UNSPECIFIED), "");
  // The longest of them still fits the column the row gives a slot.
  EXPECT_LE(FormatWornSlot(EQUIP_SLOT_PENDANT_2).size(), 10u);
}

// The whole point of balancing: a name that has to break should break near the
// middle rather than leaving one word alone on the second line.
// --- SkillsForAdvancement ---

Skill PageSkill(const std::string& name, int order) {
  Skill skill;
  skill.set_name(name);
  skill.set_job_advancement(JOB_ADVANCEMENT_CLERIC);
  skill.set_max_level(10);
  skill.set_skill_order(order);
  return skill;
}

std::vector<std::string> NamesOf(const std::vector<const Skill*>& page) {
  std::vector<std::string> names;
  for (const Skill* skill : page) {
    names.push_back(skill->name());
  }
  return names;
}

// A Vengeance form takes the row of the skill it replaces -- the same place in
// the book -- and only while its switch is on. Off, it is not on the page at
// all.
TEST(SkillsForAdvancementTest, TheSwitchDecidesWhichFormIsListed) {
  Skill form = PageSkill("Angelic Wrath", 2);
  form.set_replaces_skill_name("Heal");
  form.set_toggle_skill_name("Righteously Indignant");
  std::map<std::string, Skill> catalog = {
      {"bless", PageSkill("Bless", 1)},
      {"heal", PageSkill("Heal", 2)},
      {"holy_arrow", PageSkill("Holy Arrow", 3)},
      {"angelic_wrath", form}};

  EXPECT_EQ(NamesOf(SkillsForAdvancement(catalog, JOB_ADVANCEMENT_CLERIC)),
            (std::vector<std::string>{"Bless", "Heal", "Holy Arrow"}));
  EXPECT_EQ(
      NamesOf(SkillsForAdvancement(catalog, JOB_ADVANCEMENT_CLERIC,
                                   /*hyper=*/false, {"Righteously Indignant"})),
      (std::vector<std::string>{"Bless", "Angelic Wrath", "Holy Arrow"}));
}

TEST(FormatEquipSetTest, NamesEverySet) {
  EXPECT_EQ(FormatEquipSet(EQUIP_SET_NAME_FROZEN), "Frozen Set");
  EXPECT_EQ(FormatEquipSet(EQUIP_SET_NAME_BOSS_ACCESSORY),
            "Boss Accessory Set");
  EXPECT_EQ(FormatEquipSet(EQUIP_SET_NAME_UNSPECIFIED), "");
}

// --- Inner Ability lines ---

// Flat for a flat line, a percent sign for a percentage, and the two Max HP
// lines told apart by the value rather than by the name.
TEST(AbilityLineTextTest, NamesAndValuesEveryKindOfLine) {
  AbilityLine line;
  line.set_type(ABILITY_LINE_TYPE_BOSS_DAMAGE);
  line.set_rank(ABILITY_RANK_LEGENDARY);
  EXPECT_EQ(AbilityLineName(line.type()), "Boss Damage");
  EXPECT_EQ(AbilityLineValueText(line), "+20%");

  line.set_type(ABILITY_LINE_TYPE_MAX_HP);
  EXPECT_EQ(AbilityLineName(line.type()), "Max HP");
  EXPECT_EQ(AbilityLineValueText(line), "+600");

  line.set_type(ABILITY_LINE_TYPE_MAX_HP_PCT);
  EXPECT_EQ(AbilityLineName(line.type()), "Max HP");
  EXPECT_EQ(AbilityLineValueText(line), "+20%");

  line.set_type(ABILITY_LINE_TYPE_ATTACK_SPEED);
  EXPECT_EQ(AbilityLineName(line.type()), "Attack Speed");
  EXPECT_EQ(AbilityLineValueText(line), "+1");

  EXPECT_EQ(AbilityLineName(ABILITY_LINE_TYPE_UNSPECIFIED), "");
}

// A potential line reads its value off the item's level, and the flat lines,
// the shares and the two cooldown lines each say it their own way.
TEST(PotentialLineTextTest, NamesAndValuesEveryKindOfLine) {
  PotentialLine line;
  line.set_type(POTENTIAL_LINE_TYPE_LUK_PCT);
  line.set_rank(POTENTIAL_RANK_LEGENDARY);
  EXPECT_EQ(PotentialLineName(line.type()), "LUK");
  EXPECT_EQ(PotentialLineValueText(line, 100), "+12%");
  // The same line on a lesser item pays a lesser band.
  EXPECT_EQ(PotentialLineValueText(line, 30), "+6%");

  line.set_type(POTENTIAL_LINE_TYPE_ALL_STATS);
  line.set_rank(POTENTIAL_RANK_RARE);
  EXPECT_EQ(PotentialLineName(line.type()), "All Stats");
  EXPECT_EQ(PotentialLineValueText(line, 100), "+5");

  line.set_type(POTENTIAL_LINE_TYPE_COOLDOWN_2);
  line.set_rank(POTENTIAL_RANK_LEGENDARY);
  EXPECT_EQ(PotentialLineName(line.type()), "Cooldown");
  EXPECT_EQ(PotentialLineValueText(line, 100), "-2s");

  line.set_type(POTENTIAL_LINE_TYPE_IGNORE_DEFENSE_35);
  EXPECT_EQ(PotentialLineName(line.type()), "Ignore DEF");
  EXPECT_EQ(PotentialLineValueText(line, 100), "+35%");

  EXPECT_EQ(PotentialLineName(POTENTIAL_LINE_TYPE_UNSPECIFIED), "");
}

// The column cell: value first, the name abbreviated, and every line held to
// the one width so a list's columns line up under their header.
TEST(PotentialLineTextTest, ShortensEveryNameThatOutgrowsAColumn) {
  EXPECT_EQ(PotentialLineShortName(POTENTIAL_LINE_TYPE_CRIT_DAMAGE_PCT),
            "Crit DMG");
  EXPECT_EQ(PotentialLineShortName(POTENTIAL_LINE_TYPE_IGNORE_DEFENSE_40),
            "IED");
  EXPECT_EQ(PotentialLineShortName(POTENTIAL_LINE_TYPE_BOSS_DAMAGE_40), "Boss");
  EXPECT_EQ(PotentialLineShortName(POTENTIAL_LINE_TYPE_MESO_RATE), "Meso");
  EXPECT_EQ(PotentialLineShortName(POTENTIAL_LINE_TYPE_ITEM_DROP_RATE), "Drop");
  EXPECT_EQ(PotentialLineShortName(POTENTIAL_LINE_TYPE_COOLDOWN_1), "CD");
  EXPECT_EQ(PotentialLineShortName(POTENTIAL_LINE_TYPE_MAX_HP), "HP");
  EXPECT_EQ(PotentialLineShortName(POTENTIAL_LINE_TYPE_ALL_STATS_PCT),
            "All Stat");
  // A name that already fits a column is left as the card says it.
  EXPECT_EQ(PotentialLineShortName(POTENTIAL_LINE_TYPE_ATTACK_PCT), "ATT");
  EXPECT_EQ(PotentialLineShortName(POTENTIAL_LINE_TYPE_DAMAGE_PCT), "Damage");
}

// One line of `type` at `rank`, as a potential the cell can read.
Potential OneLine(PotentialLineType type, PotentialRank rank) {
  Potential potential;
  PotentialLine* line = potential.add_lines();
  line->set_type(type);
  line->set_rank(rank);
  return potential;
}

TEST(PotentialLineTextTest, CellIsValueThenNameAtOneWidth) {
  EXPECT_EQ(PotentialCell(OneLine(POTENTIAL_LINE_TYPE_ATTACK_PCT,
                                  POTENTIAL_RANK_LEGENDARY),
                          150),
            "12% ATT     ");
  EXPECT_EQ(PotentialCell(OneLine(POTENTIAL_LINE_TYPE_COOLDOWN_2,
                                  POTENTIAL_RANK_LEGENDARY),
                          150),
            "-2s CD      ");

  // The widest total the game rolls fills the column exactly, and nothing is
  // cut to reach it: three of the widest line still hold the width.
  Potential all_stats;
  for (int i = 0; i < kPotentialLines; ++i) {
    PotentialLine* line = all_stats.add_lines();
    line->set_type(POTENTIAL_LINE_TYPE_ALL_STATS_PCT);
    line->set_rank(POTENTIAL_RANK_LEGENDARY);
  }
  EXPECT_EQ(PotentialCell(all_stats, 200), "30% All Stat");
  for (int type = 0; type < PotentialLineType_ARRAYSIZE; ++type) {
    Potential potential;
    for (int i = 0; i < kPotentialLines; ++i) {
      PotentialLine* line = potential.add_lines();
      line->set_type(static_cast<PotentialLineType>(type));
      line->set_rank(POTENTIAL_RANK_LEGENDARY);
    }
    EXPECT_EQ(TextColumns(PotentialCell(potential, 200)), kPotentialCellWidth)
        << "type " << type;
  }
}

// A column reports what the item grants, not what one of its lines says: two
// lines of one stat are one figure.
TEST(PotentialLineTextTest, CellSumsEveryLineGrantingTheTopStat) {
  Potential potential;
  PotentialLine* line = potential.add_lines();
  line->set_type(POTENTIAL_LINE_TYPE_INT_PCT);
  line->set_rank(POTENTIAL_RANK_LEGENDARY);
  // A line of something else, which the total leaves alone.
  line = potential.add_lines();
  line->set_type(POTENTIAL_LINE_TYPE_MESO_RATE);
  line->set_rank(POTENTIAL_RANK_LEGENDARY);
  line = potential.add_lines();
  line->set_type(POTENTIAL_LINE_TYPE_INT_PCT);
  line->set_rank(POTENTIAL_RANK_UNIQUE);
  EXPECT_EQ(PotentialCell(potential, 150), "21% INT     ");

  // Ignored defence meets in reverse, as it does everywhere else: 15% and 30%
  // together leave 59.5% of the defence standing.
  Potential ied =
      OneLine(POTENTIAL_LINE_TYPE_IGNORE_DEFENSE_15, POTENTIAL_RANK_EPIC);
  line = ied.add_lines();
  line->set_type(POTENTIAL_LINE_TYPE_IGNORE_DEFENSE_30);
  line->set_rank(POTENTIAL_RANK_UNIQUE);
  EXPECT_EQ(PotentialCell(ied, 150), "41% IED     ");

  // Boss damage is stated at three sizes and adds up across all of them.
  Potential boss =
      OneLine(POTENTIAL_LINE_TYPE_BOSS_DAMAGE_30, POTENTIAL_RANK_UNIQUE);
  line = boss.add_lines();
  line->set_type(POTENTIAL_LINE_TYPE_BOSS_DAMAGE_40);
  line->set_rank(POTENTIAL_RANK_LEGENDARY);
  EXPECT_EQ(PotentialCell(boss, 150), "70% Boss    ");
}

TEST(PotentialLineTextTest, NamesEveryRank) {
  EXPECT_EQ(PotentialRankName(POTENTIAL_RANK_RARE), "Rare");
  EXPECT_EQ(PotentialRankName(POTENTIAL_RANK_EPIC), "Epic");
  EXPECT_EQ(PotentialRankName(POTENTIAL_RANK_UNIQUE), "Unique");
  EXPECT_EQ(PotentialRankName(POTENTIAL_RANK_LEGENDARY), "Legendary");
  EXPECT_EQ(PotentialRankName(POTENTIAL_RANK_UNSPECIFIED), "");
}

}  // namespace
}  // namespace ms
