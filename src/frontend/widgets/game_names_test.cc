#include "src/frontend/widgets/game_names.h"

#include <gtest/gtest.h>

#include <map>
#include <set>
#include <string>
#include <vector>

#include "ftxui/screen/string.hpp"
#include "src/character/skill_placement.h"
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

// HP is named max_hp on EquipStats. The lookup is by label, so it still finds
// it.
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

// The data lists both hands of one weapon to mean "any sword".
TEST(FormatWeaponListTest, AWholePairCollapsesToItsBareName) {
  EXPECT_EQ(FormatWeaponList(
                {EQUIP_TYPE_ONE_HANDED_SWORD, EQUIP_TYPE_TWO_HANDED_SWORD,
                 EQUIP_TYPE_ONE_HANDED_AXE, EQUIP_TYPE_TWO_HANDED_AXE}),
            "Sword / Axe");
  // The collapsed name appears where the first half was listed, not at the end.
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
  // A skill with no kind gets blanks rather than a wrong tag.
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

// A new skill kind that nobody checks here reads as a passive. That happened to
// the first auto-attack, which inspected as " Passive " and showed no effects
// at any level.
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
  // All four ring slots read "Ring": this names what the item is, not where it
  // is worn.
  EXPECT_EQ(FormatSlot(EQUIP_SLOT_RING_4), "Ring");
  EXPECT_EQ(FormatSlot(EQUIP_SLOT_PENDANT_2), "Pendant");
}

// A new slot with no name here would show the player a blank column.
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

// A worn row says which slot of its family it is, but slots with no siblings
// get no number. A character wears one hat, and "Hat 1" says nothing.
TEST(FormatWornSlotTest, NumbersOnlyTheSlotsWithSiblings) {
  EXPECT_EQ(FormatWornSlot(EQUIP_SLOT_RING), "Ring 1");
  EXPECT_EQ(FormatWornSlot(EQUIP_SLOT_RING_4), "Ring 4");
  EXPECT_EQ(FormatWornSlot(EQUIP_SLOT_PENDANT), "Pendant 1");
  EXPECT_EQ(FormatWornSlot(EQUIP_SLOT_PENDANT_2), "Pendant 2");
  EXPECT_EQ(FormatWornSlot(EQUIP_SLOT_HAT), "Hat");
  EXPECT_EQ(FormatWornSlot(EQUIP_SLOT_UNSPECIFIED), "");
  // The longest one still fits the slot column.
  EXPECT_LE(FormatWornSlot(EQUIP_SLOT_PENDANT_2).size(), 10u);
}

// --- SkillsForAdvancement ---

Skill PageSkill(const std::string& name, int order) {
  Skill skill;
  skill.set_name(name);
  PlaceIn(skill, JOB_ADVANCEMENT_CLERIC, order);
  skill.set_max_level(10);
  return skill;
}

std::vector<std::string> NamesOf(const std::vector<const Skill*>& page) {
  std::vector<std::string> names;
  for (const Skill* skill : page) {
    names.push_back(skill->name());
  }
  return names;
}

// A Vengeance form takes the row of the skill it replaces, but only while its
// toggle is on. With the toggle off it isn't on the page.
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
  EXPECT_EQ(FormatEquipSet(EQUIP_SET_NAME_SENGOKU_TREASURE),
            "Sengoku Treasure Set");
  EXPECT_EQ(FormatEquipSet(EQUIP_SET_NAME_UNSPECIFIED), "");
}

// --- Every enum value has a name ---
//
// The static_asserts force a look when a value is added, but not that it got a
// name; a missing one shows as a blank cell.

TEST(GameNamesTest, EveryAbilityLineHasAName) {
  for (int i = AbilityLineType_MIN; i <= AbilityLineType_MAX; ++i) {
    if (!AbilityLineType_IsValid(i) || i == ABILITY_LINE_TYPE_UNSPECIFIED) {
      continue;
    }
    EXPECT_FALSE(AbilityLineName(static_cast<AbilityLineType>(i)).empty())
        << AbilityLineType_Name(i);
  }
}

TEST(GameNamesTest, EveryPotentialLineHasBothNames) {
  for (int i = PotentialLineType_MIN; i <= PotentialLineType_MAX; ++i) {
    if (!PotentialLineType_IsValid(i) || i == POTENTIAL_LINE_TYPE_UNSPECIFIED) {
      continue;
    }
    PotentialLineType type = static_cast<PotentialLineType>(i);
    EXPECT_FALSE(PotentialLineName(type).empty()) << PotentialLineType_Name(i);
    // The short name fills a column cell, so a blank one leaves a gap in a list
    // that otherwise lines up.
    EXPECT_FALSE(PotentialLineShortName(type).empty())
        << PotentialLineType_Name(i);
  }
}

TEST(GameNamesTest, EveryRankHasAName) {
  for (int i = PotentialRank_MIN; i <= PotentialRank_MAX; ++i) {
    if (!PotentialRank_IsValid(i) || i == POTENTIAL_RANK_UNSPECIFIED) {
      continue;
    }
    EXPECT_FALSE(PotentialRankName(static_cast<PotentialRank>(i)).empty())
        << PotentialRank_Name(i);
  }
  for (int i = AbilityRank_MIN; i <= AbilityRank_MAX; ++i) {
    if (!AbilityRank_IsValid(i) || i == ABILITY_RANK_UNSPECIFIED) {
      continue;
    }
    EXPECT_FALSE(AbilityRankName(static_cast<AbilityRank>(i)).empty())
        << AbilityRank_Name(i);
  }
}

TEST(GameNamesTest, EveryHyperStatHasAName) {
  for (int i = HyperStatField_MIN; i <= HyperStatField_MAX; ++i) {
    if (!HyperStatField_IsValid(i) || i == HYPER_STAT_FIELD_UNSPECIFIED) {
      continue;
    }
    EXPECT_FALSE(HyperStatName(static_cast<HyperStatField>(i)).empty())
        << HyperStatField_Name(i);
  }
}

TEST(GameNamesTest, EveryEquipSetHasAName) {
  for (int i = EquipSetName_MIN; i <= EquipSetName_MAX; ++i) {
    if (!EquipSetName_IsValid(i) || i == EQUIP_SET_NAME_UNSPECIFIED) {
      continue;
    }
    EXPECT_FALSE(FormatEquipSet(static_cast<EquipSetName>(i)).empty())
        << EquipSetName_Name(i);
  }
}

TEST(GameNamesTest, EveryEquipTypeAndSlotHasAName) {
  for (int i = EquipType_MIN; i <= EquipType_MAX; ++i) {
    if (!EquipType_IsValid(i) || i == EQUIP_TYPE_UNSPECIFIED) {
      continue;
    }
    EXPECT_FALSE(FormatEquipType(static_cast<EquipType>(i)).empty())
        << EquipType_Name(i);
  }
  for (int i = EquipSlot_MIN; i <= EquipSlot_MAX; ++i) {
    if (!EquipSlot_IsValid(i) || i == EQUIP_SLOT_UNSPECIFIED) {
      continue;
    }
    EquipSlot slot = static_cast<EquipSlot>(i);
    EXPECT_FALSE(FormatSlot(slot).empty()) << EquipSlot_Name(i);
    EXPECT_FALSE(FormatWornSlot(slot).empty()) << EquipSlot_Name(i);
  }
}

TEST(GameNamesTest, EveryCubeAndTrackHasAName) {
  EXPECT_FALSE(CubeName(CubeType::kRed).empty());
  EXPECT_FALSE(CubeTrackName(PotentialTrack::kMain).empty());
  EXPECT_FALSE(CubeTrackName(PotentialTrack::kBonus).empty());
}

// --- Inner Ability lines ---

// Flat lines show a number, percentages a percent sign, and the two Max HP
// lines differ by value rather than by name.
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

// A potential line's value depends on the item's level. Flat lines, percentage
// lines and the two cooldown lines each format it differently.
TEST(PotentialLineTextTest, NamesAndValuesEveryKindOfLine) {
  PotentialLine line;
  line.set_type(POTENTIAL_LINE_TYPE_LUK_PCT);
  line.set_rank(POTENTIAL_RANK_LEGENDARY);
  EXPECT_EQ(PotentialLineName(line.type()), "LUK");
  EXPECT_EQ(PotentialLineValueText(line, 100), "+12%");
  // The same line on a lower-level item rolls a smaller value.
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

// The column cell: value first, an abbreviated name, and every line within one
// width so the columns line up under their header.
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
  // A name that already fits a column is left as the card shows it.
  EXPECT_EQ(PotentialLineShortName(POTENTIAL_LINE_TYPE_ATTACK_PCT), "ATT");
  EXPECT_EQ(PotentialLineShortName(POTENTIAL_LINE_TYPE_DAMAGE_PCT), "Damage");
}

// A potential column with room for one effect, as a panel at its narrowest
// gives it.
constexpr int kOneEffect = 12;

// One line of `type` at `rank`, as a potential the cell can read.
Potential OneLine(PotentialLineType type, PotentialRank rank) {
  Potential potential;
  PotentialLine* line = potential.add_lines();
  line->set_type(type);
  line->set_rank(rank);
  return potential;
}

// Adds a line of `type` at `rank` to `potential`.
void AddLine(Potential& potential, PotentialLineType type, PotentialRank rank) {
  PotentialLine* line = potential.add_lines();
  line->set_type(type);
  line->set_rank(rank);
}

TEST(PotentialLineTextTest, CellIsValueThenNameAtOneWidth) {
  EXPECT_EQ(PotentialCell(OneLine(POTENTIAL_LINE_TYPE_ATTACK_PCT,
                                  POTENTIAL_RANK_LEGENDARY),
                          150, STAT_FIELD_STR, STAT_FIELD_DEX, kOneEffect),
            "12% ATT     ");
  EXPECT_EQ(PotentialCell(OneLine(POTENTIAL_LINE_TYPE_COOLDOWN_2,
                                  POTENTIAL_RANK_LEGENDARY),
                          150, STAT_FIELD_STR, STAT_FIELD_DEX, kOneEffect),
            "-2s CD      ");

  // The widest total the game rolls fills the column exactly, and three of the
  // widest line still fit without cutting.
  Potential crit;
  for (int i = 0; i < kPotentialLines; ++i) {
    AddLine(crit, POTENTIAL_LINE_TYPE_CRIT_DAMAGE_PCT,
            POTENTIAL_RANK_LEGENDARY);
  }
  EXPECT_EQ(
      PotentialCell(crit, 200, STAT_FIELD_STR, STAT_FIELD_DEX, kOneEffect),
      "24% Crit DMG");
  for (int type = 0; type < PotentialLineType_ARRAYSIZE; ++type) {
    Potential potential;
    for (int i = 0; i < kPotentialLines; ++i) {
      AddLine(potential, static_cast<PotentialLineType>(type),
              POTENTIAL_RANK_LEGENDARY);
    }
    EXPECT_EQ(TextColumns(PotentialCell(potential, 200, STAT_FIELD_LUK,
                                        STAT_FIELD_DEX, kOneEffect)),
              kOneEffect)
        << "type " << type;
  }
}

// A column shows what the item grants, not what each line says: two lines of
// one stat show as one number.
TEST(PotentialLineTextTest, CellSumsEveryLineGrantingTheStatItReports) {
  Potential potential;
  AddLine(potential, POTENTIAL_LINE_TYPE_INT_PCT, POTENTIAL_RANK_LEGENDARY);
  // A line of something else, which doesn't change the total.
  AddLine(potential, POTENTIAL_LINE_TYPE_MAX_HP_PCT, POTENTIAL_RANK_LEGENDARY);
  AddLine(potential, POTENTIAL_LINE_TYPE_INT_PCT, POTENTIAL_RANK_UNIQUE);
  EXPECT_EQ(
      PotentialCell(potential, 150, STAT_FIELD_INT, STAT_FIELD_LUK, kOneEffect),
      "21% INT     ");

  // All Stat% grants the character's primary stat, so it is added into that
  // number rather than listed separately.
  AddLine(potential, POTENTIAL_LINE_TYPE_ALL_STATS_PCT,
          POTENTIAL_RANK_LEGENDARY);
  EXPECT_EQ(
      PotentialCell(potential, 150, STAT_FIELD_INT, STAT_FIELD_LUK, kOneEffect),
      "30% INT     ");

  // Ignored defence stacks multiplicatively, as it does everywhere else: 15%
  // and 30% together leave 59.5% of the defence.
  Potential ied =
      OneLine(POTENTIAL_LINE_TYPE_IGNORE_DEFENSE_15, POTENTIAL_RANK_EPIC);
  AddLine(ied, POTENTIAL_LINE_TYPE_IGNORE_DEFENSE_30, POTENTIAL_RANK_UNIQUE);
  EXPECT_EQ(PotentialCell(ied, 150, STAT_FIELD_STR, STAT_FIELD_DEX, kOneEffect),
            "41% IED     ");

  // Boss damage comes in three sizes, and all of them add up.
  Potential boss =
      OneLine(POTENTIAL_LINE_TYPE_BOSS_DAMAGE_30, POTENTIAL_RANK_UNIQUE);
  AddLine(boss, POTENTIAL_LINE_TYPE_BOSS_DAMAGE_40, POTENTIAL_RANK_LEGENDARY);
  EXPECT_EQ(
      PotentialCell(boss, 150, STAT_FIELD_STR, STAT_FIELD_DEX, kOneEffect),
      "70% Boss    ");
}

// A column with room for one effect shows the one worth most to the character,
// not the one the item lists first.
TEST(PotentialLineTextTest, CellReportsTheEffectWorthMost) {
  Potential potential;
  AddLine(potential, POTENTIAL_LINE_TYPE_STR_PCT, POTENTIAL_RANK_LEGENDARY);
  AddLine(potential, POTENTIAL_LINE_TYPE_DAMAGE_PCT, POTENTIAL_RANK_LEGENDARY);
  AddLine(potential, POTENTIAL_LINE_TYPE_ATTACK_PCT, POTENTIAL_RANK_UNIQUE);
  EXPECT_EQ(
      PotentialCell(potential, 150, STAT_FIELD_STR, STAT_FIELD_DEX, kOneEffect),
      "9% ATT      ");

  // Boss damage and ignored defence share a rank, so the one the item rolled
  // more of is shown.
  Potential boss =
      OneLine(POTENTIAL_LINE_TYPE_BOSS_DAMAGE_40, POTENTIAL_RANK_LEGENDARY);
  AddLine(boss, POTENTIAL_LINE_TYPE_IGNORE_DEFENSE_15, POTENTIAL_RANK_EPIC);
  EXPECT_EQ(
      PotentialCell(boss, 150, STAT_FIELD_STR, STAT_FIELD_DEX, kOneEffect),
      "40% Boss    ");
  AddLine(boss, POTENTIAL_LINE_TYPE_IGNORE_DEFENSE_30, POTENTIAL_RANK_UNIQUE);
  EXPECT_EQ(
      PotentialCell(boss, 150, STAT_FIELD_STR, STAT_FIELD_DEX, kOneEffect),
      "41% IED     ");
}

// Half of what a potential can roll is worthless to a given character, and a
// column that showed it would only reveal the item's rank.
TEST(PotentialLineTextTest, CellSkipsWhatThisCharacterDoesNotRead) {
  // A magician's damage uses magic attack, so the weapon attack on a wand
  // doesn't count.
  Potential weapon =
      OneLine(POTENTIAL_LINE_TYPE_ATTACK_PCT, POTENTIAL_RANK_LEGENDARY);
  AddLine(weapon, POTENTIAL_LINE_TYPE_MAGIC_ATTACK_PCT, POTENTIAL_RANK_UNIQUE);
  EXPECT_EQ(
      PotentialCell(weapon, 150, STAT_FIELD_STR, STAT_FIELD_DEX, kOneEffect),
      "12% ATT     ");
  EXPECT_EQ(
      PotentialCell(weapon, 150, STAT_FIELD_INT, STAT_FIELD_LUK, kOneEffect),
      "9% MATT     ");

  // The two stats the character doesn't use as primary or secondary, the flat
  // lines and %HP are all left out.
  Potential armor =
      OneLine(POTENTIAL_LINE_TYPE_LUK_PCT, POTENTIAL_RANK_LEGENDARY);
  AddLine(armor, POTENTIAL_LINE_TYPE_MAX_HP_PCT, POTENTIAL_RANK_LEGENDARY);
  AddLine(armor, POTENTIAL_LINE_TYPE_STR, POTENTIAL_RANK_RARE);
  EXPECT_EQ(
      PotentialCell(armor, 150, STAT_FIELD_STR, STAT_FIELD_DEX, kOneEffect),
      "Junk        ");
  EXPECT_EQ(
      PotentialCell(armor, 150, STAT_FIELD_LUK, STAT_FIELD_DEX, kOneEffect),
      "12% LUK     ");

  // An item with no potential can't be junk.
  EXPECT_EQ(PotentialCell(Potential(), 150, STAT_FIELD_STR, STAT_FIELD_DEX,
                          kOneEffect),
            "-           ");
}

// The secondary stat is worth a quarter of the primary. That is worth showing
// on an item that grants nothing else, and not worth showing beside a line that
// does.
TEST(PotentialLineTextTest, CellNamesTheSecondaryStatOnlyAsALastResort) {
  Potential armor =
      OneLine(POTENTIAL_LINE_TYPE_DEX_PCT, POTENTIAL_RANK_LEGENDARY);
  AddLine(armor, POTENTIAL_LINE_TYPE_MAX_HP_PCT, POTENTIAL_RANK_LEGENDARY);
  EXPECT_EQ(
      PotentialCell(armor, 150, STAT_FIELD_STR, STAT_FIELD_DEX, kOneEffect),
      "12% DEX     ");
  // Every line granting it is summed, like the other effects.
  AddLine(armor, POTENTIAL_LINE_TYPE_DEX_PCT, POTENTIAL_RANK_UNIQUE);
  EXPECT_EQ(
      PotentialCell(armor, 150, STAT_FIELD_STR, STAT_FIELD_DEX, kOneEffect),
      "21% DEX     ");

  // Add one line the character uses, and the secondary disappears at any width.
  // Even a column wide enough for both shows only what makes the item worth
  // keeping.
  AddLine(armor, POTENTIAL_LINE_TYPE_BOSS_DAMAGE_30, POTENTIAL_RANK_UNIQUE);
  EXPECT_EQ(PotentialCell(armor, 150, STAT_FIELD_STR, STAT_FIELD_DEX, 30),
            "30% Boss                      ");

  // All Stat% grants the secondary too, but it is already counted in the
  // primary's number, so it isn't shown twice.
  Potential all =
      OneLine(POTENTIAL_LINE_TYPE_ALL_STATS_PCT, POTENTIAL_RANK_LEGENDARY);
  EXPECT_EQ(PotentialCell(all, 150, STAT_FIELD_STR, STAT_FIELD_DEX, 30),
            "9% STR                        ");

  // A character with no branch has no secondary stat either.
  EXPECT_EQ(PotentialCell(armor, 150, STAT_FIELD_UNSPECIFIED,
                          STAT_FIELD_UNSPECIFIED, kOneEffect),
            "30% Boss    ");
}

// With enough room, the column lists more effects in rank order, each one whole
// or not at all.
TEST(PotentialLineTextTest, WideColumnListsEveryEffectThatFits) {
  Potential potential =
      OneLine(POTENTIAL_LINE_TYPE_STR_PCT, POTENTIAL_RANK_LEGENDARY);
  AddLine(potential, POTENTIAL_LINE_TYPE_BOSS_DAMAGE_30, POTENTIAL_RANK_UNIQUE);
  AddLine(potential, POTENTIAL_LINE_TYPE_ATTACK_PCT, POTENTIAL_RANK_LEGENDARY);
  EXPECT_EQ(PotentialCell(potential, 150, STAT_FIELD_STR, STAT_FIELD_DEX, 30),
            "12% ATT, 30% Boss, 12% STR    ");
  EXPECT_EQ(PotentialCell(potential, 150, STAT_FIELD_STR, STAT_FIELD_DEX, 20),
            "12% ATT, 30% Boss   ");
  // One column too narrow for the third effect, so it is left out.
  EXPECT_EQ(PotentialCell(potential, 150, STAT_FIELD_STR, STAT_FIELD_DEX, 25),
            "12% ATT, 30% Boss        ");

  // Effects this character doesn't use are skipped rather than taking a place:
  // a magician uses neither weapon attack nor STR.
  EXPECT_EQ(PotentialCell(potential, 150, STAT_FIELD_INT, STAT_FIELD_LUK, 30),
            "30% Boss                      ");

  // A zero-width column shows nothing.
  EXPECT_EQ(PotentialCell(potential, 150, STAT_FIELD_STR, STAT_FIELD_DEX, 0),
            "");
}

TEST(PotentialLineTextTest, NamesEveryRank) {
  EXPECT_EQ(PotentialRankName(POTENTIAL_RANK_RARE), "Rare");
  EXPECT_EQ(PotentialRankName(POTENTIAL_RANK_EPIC), "Epic");
  EXPECT_EQ(PotentialRankName(POTENTIAL_RANK_UNIQUE), "Unique");
  EXPECT_EQ(PotentialRankName(POTENTIAL_RANK_LEGENDARY), "Legendary");
  EXPECT_EQ(PotentialRankName(POTENTIAL_RANK_UNSPECIFIED), "");
}

// --- VNodesFor ---

Skill Node(const std::string& name, VNodeKind kind, JobAdvancement book,
           int order) {
  Skill skill;
  skill.set_name(name);
  skill.set_v_node(kind);
  PlaceIn(skill, book, order);
  return skill;
}

// The matrix page runs from the rarest nodes to the most common: the job's
// actives, their boosts, the line's nodes, then the commons. Each block keeps
// its own skill_order, since the blocks come from different books.
TEST(VNodesForTest, TheJobsOwnLeadAndTheCommonsSitAtTheFoot) {
  std::map<std::string, Skill> catalog = {
      {"lift",
       Node("Rope Lift", V_NODE_KIND_COMMON, JOB_ADVANCEMENT_COMMON, 9)},
      {"erda",
       Node("Erda Fountain", V_NODE_KIND_COMMON, JOB_ADVANCEMENT_COMMON, 1)},
      {"skin", Node("Impenetrable Skin", V_NODE_KIND_ARCHETYPE,
                    JOB_ADVANCEMENT_DARK_KNIGHT_V, 8)},
      {"boost_b",
       Node("Boost B", V_NODE_KIND_BOOST, JOB_ADVANCEMENT_DARK_KNIGHT_V, 6)},
      {"radiant",
       Node("Radiant Evil", V_NODE_KIND_JOB, JOB_ADVANCEMENT_DARK_KNIGHT_V, 2)},
      {"aura", Node("Weapon Aura", V_NODE_KIND_ARCHETYPE,
                    JOB_ADVANCEMENT_DARK_KNIGHT_V, 7)},
      {"boost_a",
       Node("Boost A", V_NODE_KIND_BOOST, JOB_ADVANCEMENT_DARK_KNIGHT_V, 5)},
      {"dark", Node("Dark Synthesis", V_NODE_KIND_JOB,
                    JOB_ADVANCEMENT_DARK_KNIGHT_V, 1)},
  };
  EXPECT_EQ(
      NamesOf(VNodesFor(catalog, JOB_ADVANCEMENT_DARK_KNIGHT_V)),
      (std::vector<std::string>{"Dark Synthesis", "Radiant Evil", "Boost A",
                                "Boost B", "Weapon Aura", "Impenetrable Skin",
                                "Erda Fountain", "Rope Lift"}));
}

// The page draws a rule between blocks, so the four read as separate lists.
TEST(VNodesForTest, TheSectionsOfThePageAreReported) {
  std::map<std::string, Skill> catalog = {
      {"erda",
       Node("Erda Fountain", V_NODE_KIND_COMMON, JOB_ADVANCEMENT_COMMON, 1)},
      {"lift",
       Node("Rope Lift", V_NODE_KIND_COMMON, JOB_ADVANCEMENT_COMMON, 2)},
      {"skin", Node("Impenetrable Skin", V_NODE_KIND_ARCHETYPE,
                    JOB_ADVANCEMENT_DARK_KNIGHT_V, 8)},
      {"boost_a",
       Node("Boost A", V_NODE_KIND_BOOST, JOB_ADVANCEMENT_DARK_KNIGHT_V, 5)},
      {"boost_b",
       Node("Boost B", V_NODE_KIND_BOOST, JOB_ADVANCEMENT_DARK_KNIGHT_V, 6)},
      {"dark", Node("Dark Synthesis", V_NODE_KIND_JOB,
                    JOB_ADVANCEMENT_DARK_KNIGHT_V, 1)},
  };
  std::vector<const Skill*> nodes =
      VNodesFor(catalog, JOB_ADVANCEMENT_DARK_KNIGHT_V);
  EXPECT_EQ(VNodeSectionBreaks(nodes), (std::vector<int>{1, 3, 4}));

  // A page missing a block gets no rule for it. A job with no nodes of its own
  // starts with the common nodes and needs no rules at all.
  std::map<std::string, Skill> commons_only = {
      {"erda",
       Node("Erda Fountain", V_NODE_KIND_COMMON, JOB_ADVANCEMENT_COMMON, 1)},
      {"lift",
       Node("Rope Lift", V_NODE_KIND_COMMON, JOB_ADVANCEMENT_COMMON, 2)},
  };
  EXPECT_TRUE(
      VNodeSectionBreaks(VNodesFor(commons_only, JOB_ADVANCEMENT_BISHOP_V))
          .empty());
}

// Only 5th jobs of an archetype node's line list it, so a magician's matrix
// never has Weapon Aura.
TEST(VNodesForTest, AnotherLinesArchetypeNodeIsNotOnThePage) {
  std::map<std::string, Skill> catalog = {
      {"erda",
       Node("Erda Fountain", V_NODE_KIND_COMMON, JOB_ADVANCEMENT_COMMON, 1)},
      {"aura", Node("Weapon Aura", V_NODE_KIND_ARCHETYPE,
                    JOB_ADVANCEMENT_DARK_KNIGHT_V, 7)},
  };
  EXPECT_EQ(NamesOf(VNodesFor(catalog, JOB_ADVANCEMENT_BISHOP_V)),
            (std::vector<std::string>{"Erda Fountain"}));
  EXPECT_EQ(NamesOf(VNodesFor(catalog, JOB_ADVANCEMENT_DARK_KNIGHT_V)),
            (std::vector<std::string>{"Weapon Aura", "Erda Fountain"}));
}

// A job with no nodes of its own written yet still has a matrix of common
// nodes.
TEST(VNodesForTest, AJobWithNoNodesOfItsOwnStillHoldsTheCommons) {
  std::map<std::string, Skill> catalog = {
      {"erda",
       Node("Erda Fountain", V_NODE_KIND_COMMON, JOB_ADVANCEMENT_COMMON, 1)},
      {"slash", PageSkill("Slash Blast", 1)},
  };
  EXPECT_EQ(NamesOf(VNodesFor(catalog, JOB_ADVANCEMENT_HERO_V)),
            (std::vector<std::string>{"Erda Fountain"}));
}

// A chip must not change width when a preset is put in use, or the row shifts
// under the cursor.
TEST(PresetSlotLabelTest, TheMarkKeepsItsColumnEitherWay) {
  const std::string idle =
      PresetSlotLabel(StatPreset::kFirst, /*autoswap=*/false,
                      /*in_use=*/false);
  const std::string used = PresetSlotLabel(StatPreset::kFirst,
                                           /*autoswap=*/false,
                                           /*in_use=*/true);
  EXPECT_EQ(idle, "1  ");
  EXPECT_EQ(used, "1 ✓");
  EXPECT_EQ(ftxui::string_width(idle), ftxui::string_width(used));
  EXPECT_EQ(PresetSlotLabel(StatPreset::kThird, /*autoswap=*/false,
                            /*in_use=*/false),
            "3  ");
}

// With the autoswap on, the two presets it uses are named for their use, the
// third keeps its number, and none has a mark.
TEST(PresetSlotLabelTest, TheAutoswapNamesTheTwoItReads) {
  EXPECT_EQ(PresetSlotName(StatPreset::kFirst, /*autoswap=*/true), "Farm");
  EXPECT_EQ(PresetSlotName(StatPreset::kSecond, /*autoswap=*/true), "Boss");
  EXPECT_EQ(PresetSlotName(StatPreset::kThird, /*autoswap=*/true), "3");
  EXPECT_EQ(PresetSlotName(StatPreset::kSecond, /*autoswap=*/false), "2");
  EXPECT_EQ(PresetSlotLabel(StatPreset::kFirst, /*autoswap=*/true,
                            /*in_use=*/true),
            "Farm");
}

}  // namespace
}  // namespace ms
