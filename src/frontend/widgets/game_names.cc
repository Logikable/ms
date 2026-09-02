#include "src/frontend/widgets/game_names.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "src/character/hyper_stats.h"
#include "src/character/inner_ability.h"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/format.h"
#include "src/item/item.h"
#include "src/item/potential.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/equip_set.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {

const DisplayStat* DisplayStatFor(StatField field) {
  // Both tables spell a stat the same way, so the label is the join between
  // them and neither needs to know the other's order.
  std::string name = StatFieldName(field);
  if (name.empty()) {
    return nullptr;
  }
  for (const DisplayStat& stat : kDisplayStats) {
    if (name == stat.label) {
      return &stat;
    }
  }
  return nullptr;
}

std::string FormatWeaponList(const std::vector<EquipType>& types) {
  // A weapon that comes in both hands' versions. Naming the two of them is how
  // the data says "any sword", but "One-Handed Sword / Two-Handed Sword" is
  // neither how a description writes it nor narrow enough for a column.
  struct WeaponPair {
    EquipType one_handed;
    EquipType two_handed;
    const char* name;
  };
  const WeaponPair kWeaponPairs[] = {
      {EQUIP_TYPE_ONE_HANDED_SWORD, EQUIP_TYPE_TWO_HANDED_SWORD, "Sword"},
      {EQUIP_TYPE_ONE_HANDED_AXE, EQUIP_TYPE_TWO_HANDED_AXE, "Axe"},
      {EQUIP_TYPE_ONE_HANDED_BLUNT, EQUIP_TYPE_TWO_HANDED_BLUNT, "Blunt"},
  };
  std::set<EquipType> listed(types.begin(), types.end());

  // Walked in the order they were given, so a collapsed pair lands where its
  // first half was named.
  std::string result;
  std::set<EquipType> written;
  for (EquipType type : types) {
    if (written.count(type) > 0) {
      continue;
    }
    written.insert(type);
    std::string name = FormatEquipType(type);
    for (const WeaponPair& pair : kWeaponPairs) {
      // Only a list holding the whole pair collapses: one hand's version alone
      // stays the weapon it names.
      if ((type == pair.one_handed || type == pair.two_handed) &&
          listed.count(pair.one_handed) > 0 &&
          listed.count(pair.two_handed) > 0) {
        name = pair.name;
        written.insert(pair.one_handed);
        written.insert(pair.two_handed);
        break;
      }
    }
    if (name.empty()) {
      continue;
    }
    if (!result.empty()) {
      result += " / ";
    }
    result += name;
  }
  return result;
}

namespace {

// Appends `skill` to `out`, but only after whatever it waits on. Keyed by
// display name, which is what a requirement names and what a learned level is
// held under. Marking before the recursion rather than after is what stops a
// cycle in the data from recurring forever.
void EmitAfterRequirement(const Skill& skill,
                          const std::map<std::string, const Skill*>& by_name,
                          std::set<std::string>& emitted,
                          std::vector<const Skill*>& out) {
  if (!emitted.insert(skill.name()).second) {
    return;
  }
  if (skill.has_required_skill()) {
    std::map<std::string, const Skill*>::const_iterator it =
        by_name.find(skill.required_skill().skill_name());
    // A requirement naming a skill from another page is nothing this list can
    // order around, and the player will find it in the book it belongs to.
    if (it != by_name.end()) {
      EmitAfterRequirement(*it->second, by_name, emitted, out);
    }
  }
  out.push_back(&skill);
}

// The Vengeance forms standing right now, keyed by the skill each takes the
// place of. A form whose toggle is switched off is not here, and so is not on
// the page at all.
std::map<std::string, const Skill*> FormsShowing(
    const std::map<std::string, Skill>& catalog,
    const std::set<std::string>& toggles_on) {
  std::map<std::string, const Skill*> showing;
  for (const std::pair<const std::string, Skill>& entry : catalog) {
    const Skill& skill = entry.second;
    if (!skill.replaces_skill_name().empty() &&
        toggles_on.count(skill.toggle_skill_name()) > 0) {
      showing[skill.replaces_skill_name()] = &skill;
    }
  }
  return showing;
}

}  // namespace

std::vector<const Skill*> SkillsForAdvancement(
    const std::map<std::string, Skill>& catalog, JobAdvancement advancement,
    bool hyper, const std::set<std::string>& toggles_on) {
  std::vector<const Skill*> result;
  if (advancement == JOB_ADVANCEMENT_UNSPECIFIED) {
    return result;
  }
  for (const std::pair<const std::string, Skill>& entry : catalog) {
    // A form takes its parent's row below rather than a row of its own: it
    // carries that skill's skill_order, so listing both would be two skills
    // at one place in the book.
    if (entry.second.job_advancement() == advancement &&
        entry.second.hyper() == hyper &&
        entry.second.replaces_skill_name().empty()) {
      result.push_back(&entry.second);
    }
  }
  std::stable_sort(result.begin(), result.end(),
                   [](const Skill* a, const Skill* b) {
                     return a->skill_order() < b->skill_order();
                   });

  std::map<std::string, const Skill*> by_name;
  for (const Skill* skill : result) {
    by_name[skill->name()] = skill;
  }
  std::vector<const Skill*> ordered;
  std::set<std::string> emitted;
  for (const Skill* skill : result) {
    EmitAfterRequirement(*skill, by_name, emitted, ordered);
  }
  std::map<std::string, const Skill*> showing =
      FormsShowing(catalog, toggles_on);
  for (const Skill*& skill : ordered) {
    std::map<std::string, const Skill*>::const_iterator form =
        showing.find(skill->name());
    if (form != showing.end()) {
      skill = form->second;
    }
  }
  return ordered;
}

KindTag TagFor(const Skill& skill) {
  // Orange rather than red for the attack tag: red is the colour that says a
  // thing is refused (colors.h), and every attack skill carrying it on a
  // screen that dims what cannot be learned spent the alarm on something that
  // is never a problem.
  switch (skill.kind()) {
    case SKILL_KIND_ATTACK:
    case SKILL_KIND_ACTIVE:
      return {"A:  ", kGold};
    case SKILL_KIND_AUTO_ATTACK:
      // Purple rather than another yellow: an auto-attack is not a shade of
      // active, and two tags a step apart in the same hue read as one.
      return {"AA: ", kPurple};
    case SKILL_KIND_PASSIVE:
      return {"P:  ", kGreen};
    default:
      return {"    ", kGray};
  }
}

std::string FormatEquipSet(EquipSetName set) {
  switch (set) {
    case EQUIP_SET_NAME_FROZEN:
      return "Frozen Set";
    case EQUIP_SET_NAME_BOSS_ACCESSORY:
      return "Boss Accessory Set";
    default:
      return "";
  }
}

std::string FormatSlot(EquipSlot slot) {
  switch (slot) {
    case EQUIP_SLOT_PRIMARY_WEAPON:
      return "Weapon";
    case EQUIP_SLOT_PROJECTILE:
      return "Projectile";
    case EQUIP_SLOT_SECONDARY:
      return "Secondary";
    case EQUIP_SLOT_HAT:
      return "Hat";
    case EQUIP_SLOT_TOP:
      return "Top";
    case EQUIP_SLOT_BOTTOM:
      return "Bottom";
    case EQUIP_SLOT_CAPE:
      return "Cape";
    // Short of the full "Face Accessory": the slot column is ten columns wide.
    case EQUIP_SLOT_FACE_ACCESSORY:
      return "Face";
    case EQUIP_SLOT_EYE_ACCESSORY:
      return "Eye";
    // The family, not the slot: this is what a ring in the bag and a ring
    // named by a set are. Which of the four one is worn in is FormatWornSlot.
    case EQUIP_SLOT_RING:
    case EQUIP_SLOT_RING_2:
    case EQUIP_SLOT_RING_3:
    case EQUIP_SLOT_RING_4:
      return "Ring";
    case EQUIP_SLOT_PENDANT:
    case EQUIP_SLOT_PENDANT_2:
      return "Pendant";
    case EQUIP_SLOT_BELT:
      return "Belt";
    case EQUIP_SLOT_SHOULDER:
      return "Shoulder";
    case EQUIP_SLOT_POCKET:
      return "Pocket";
    case EQUIP_SLOT_EARRINGS:
      return "Earrings";
    case EQUIP_SLOT_GLOVES:
      return "Gloves";
    case EQUIP_SLOT_SHOES:
      return "Shoes";
    case EQUIP_SLOT_BADGE:
      return "Badge";
    case EQUIP_SLOT_EMBLEM:
      return "Emblem";
    case EQUIP_SLOT_MEDAL:
      return "Medal";
    case EQUIP_SLOT_HEART:
      return "Heart";
    // All six read alike: the item's own name is what says which area it is
    // from, and the slot column has ten columns to say the rest.
    case EQUIP_SLOT_SYMBOL_VANISHING_JOURNEY:
    case EQUIP_SLOT_SYMBOL_CHU_CHU_ISLAND:
    case EQUIP_SLOT_SYMBOL_LACHELEIN:
    case EQUIP_SLOT_SYMBOL_ARCANA:
    case EQUIP_SLOT_SYMBOL_MORASS:
    case EQUIP_SLOT_SYMBOL_ESFERA:
      return "Symbol";
    default:
      return "";
  }
}

std::string FormatWornSlot(EquipSlot slot) {
  std::string name = FormatSlot(slot);
  if (SlotFamily(slot).size() == 1 || name.empty()) {
    return name;
  }
  return name + " " + std::to_string(SlotIndex(slot) + 1);
}

std::string AttackSpeedName(AttackSpeed speed) {
  switch (speed) {
    case ATTACK_SPEED_SLOWER:
      return "Slower";
    case ATTACK_SPEED_SLOW_1:
      return "Slow 1";
    case ATTACK_SPEED_SLOW_2:
      return "Slow 2";
    case ATTACK_SPEED_AVERAGE:
      return "Average";
    case ATTACK_SPEED_FAST_1:
      return "Fast 1";
    case ATTACK_SPEED_FAST_2:
      return "Fast 2";
    case ATTACK_SPEED_FASTER:
      return "Faster";
    case ATTACK_SPEED_FASTEST_1:
      return "Fastest 1";
    case ATTACK_SPEED_FASTEST_2:
      return "Fastest 2";
    case ATTACK_SPEED_FASTEST_3:
      return "Fastest 3";
    default:
      return "";
  }
}

std::string FormatEquipType(EquipType type) {
  switch (type) {
    case EQUIP_TYPE_ONE_HANDED_SWORD:
      return "One-Handed Sword";
    case EQUIP_TYPE_BOW:
      return "Bow";
    case EQUIP_TYPE_CROSSBOW:
      return "Crossbow";
    case EQUIP_TYPE_STAFF:
      return "Staff";
    case EQUIP_TYPE_DAGGER:
      return "Dagger";
    case EQUIP_TYPE_CLAW:
      return "Claw";
    case EQUIP_TYPE_THROWING_STAR:
      return "Throwing Star";
    case EQUIP_TYPE_ARROW_FOR_BOW:
      return "Arrow for Bow";
    case EQUIP_TYPE_ARROW_FOR_CROSSBOW:
      return "Arrow for Crossbow";
    case EQUIP_TYPE_TWO_HANDED_SWORD:
      return "Two-Handed Sword";
    case EQUIP_TYPE_ONE_HANDED_AXE:
      return "One-Handed Axe";
    case EQUIP_TYPE_TWO_HANDED_AXE:
      return "Two-Handed Axe";
    case EQUIP_TYPE_ONE_HANDED_BLUNT:
      return "One-Handed Blunt";
    case EQUIP_TYPE_TWO_HANDED_BLUNT:
      return "Two-Handed Blunt";
    case EQUIP_TYPE_SPEAR:
      return "Spear";
    case EQUIP_TYPE_POLEARM:
      return "Polearm";
    case EQUIP_TYPE_MEDALLION:
      return "Medallion";
    case EQUIP_TYPE_ROSARY:
      return "Rosary";
    case EQUIP_TYPE_IRON_CHAIN:
      return "Iron Chain";
    // Three types, one name. Which branch's book it is shows in its own name
    // -- calling it a "Fire/Poison Magic Book" here would say it twice.
    case EQUIP_TYPE_MAGIC_BOOK_FIRE_POISON:
    case EQUIP_TYPE_MAGIC_BOOK_ICE_LIGHTNING:
    case EQUIP_TYPE_MAGIC_BOOK_HOLY:
      return "Magic Book";
    case EQUIP_TYPE_ARROW_FLETCHING:
      return "Arrow Fletching";
    case EQUIP_TYPE_BOW_THIMBLE:
      return "Bow Thimble";
    case EQUIP_TYPE_CHARM:
      return "Charm";
    case EQUIP_TYPE_DAGGER_SCABBARD:
      return "Dagger Scabbard";
    default:
      return "";  // not yet implemented for other types
  }
}

bool IsActive(const Skill& skill) {
  return skill.kind() != SKILL_KIND_PASSIVE;
}

std::string FormatJobCategories(const EquipPrototype& proto) {
  for (int i = 0; i < proto.equip_job_categories_size(); ++i) {
    if (static_cast<EquipJobCategory>(proto.equip_job_categories(i)) ==
        EQUIP_JOB_CATEGORY_UNIVERSAL) {
      return "All";
    }
  }
  std::string result;
  for (int i = 0; i < proto.equip_job_categories_size(); ++i) {
    if (!result.empty()) {
      result += "/";
    }
    switch (static_cast<EquipJobCategory>(proto.equip_job_categories(i))) {
      case EQUIP_JOB_CATEGORY_BEGINNER:
        result += "Beginner";
        break;
      case EQUIP_JOB_CATEGORY_WARRIOR:
        result += "Warrior";
        break;
      case EQUIP_JOB_CATEGORY_BOWMAN:
        result += "Bowman";
        break;
      case EQUIP_JOB_CATEGORY_MAGICIAN:
        result += "Magician";
        break;
      case EQUIP_JOB_CATEGORY_THIEF:
        result += "Thief";
        break;
      case EQUIP_JOB_CATEGORY_PIRATE:
        result += "Pirate";
        break;
      default:
        break;
    }
  }
  if (result.empty()) {
    return "All";
  }
  return result;
}

std::string StatFieldName(StatField field) {
  switch (field) {
    case STAT_FIELD_STR:
      return "STR";
    case STAT_FIELD_DEX:
      return "DEX";
    case STAT_FIELD_INT:
      return "INT";
    case STAT_FIELD_LUK:
      return "LUK";
    case STAT_FIELD_HP:
      return "HP";
    case STAT_FIELD_MP:
      return "MP";
    default:
      return "";
  }
}

const HyperStatField kHyperStatOrder[] = {
    HYPER_STAT_FIELD_STR,           HYPER_STAT_FIELD_DEX,
    HYPER_STAT_FIELD_INT,           HYPER_STAT_FIELD_LUK,
    HYPER_STAT_FIELD_MAX_HP,        HYPER_STAT_FIELD_CRIT_RATE,
    HYPER_STAT_FIELD_CRIT_DAMAGE,   HYPER_STAT_FIELD_IED,
    HYPER_STAT_FIELD_DAMAGE,        HYPER_STAT_FIELD_BOSS_DAMAGE,
    HYPER_STAT_FIELD_NORMAL_DAMAGE, HYPER_STAT_FIELD_ATTACK,
    HYPER_STAT_FIELD_EXP,           HYPER_STAT_FIELD_ARCANE_FORCE,
};
const int kNumHyperStats = sizeof(kHyperStatOrder) / sizeof(kHyperStatOrder[0]);

std::string AbilityLineName(AbilityLineType type) {
  static_assert(AbilityLineType_ARRAYSIZE == 17,
                "a new Inner Ability line needs a name");
  switch (type) {
    case ABILITY_LINE_TYPE_STR:
      return "STR";
    case ABILITY_LINE_TYPE_DEX:
      return "DEX";
    case ABILITY_LINE_TYPE_INT:
      return "INT";
    case ABILITY_LINE_TYPE_LUK:
      return "LUK";
    case ABILITY_LINE_TYPE_ALL_STATS:
      return "All Stats";
    case ABILITY_LINE_TYPE_MAX_HP:
    case ABILITY_LINE_TYPE_MAX_HP_PCT:
      return "Max HP";
    case ABILITY_LINE_TYPE_ATTACK:
      return "Attack";
    case ABILITY_LINE_TYPE_MAGIC_ATTACK:
      return "Magic Attack";
    case ABILITY_LINE_TYPE_CRIT_RATE:
      return "Critical Rate";
    case ABILITY_LINE_TYPE_BOSS_DAMAGE:
      return "Boss Damage";
    case ABILITY_LINE_TYPE_NORMAL_DAMAGE:
      return "Normal Damage";
    case ABILITY_LINE_TYPE_BUFF_DURATION:
      return "Buff Duration";
    case ABILITY_LINE_TYPE_ITEM_DROP:
      return "Item Drop Rate";
    case ABILITY_LINE_TYPE_MESO:
      return "Meso Drop Rate";
    case ABILITY_LINE_TYPE_ATTACK_SPEED:
      return "Attack Speed";
    default:
      return "";
  }
}

std::string AbilityLineValueText(const AbilityLine& line) {
  // The types stated in whole percents. Everything else is flat, Attack
  // Speed's one swing stage included.
  const bool percent = line.type() == ABILITY_LINE_TYPE_MAX_HP_PCT ||
                       line.type() == ABILITY_LINE_TYPE_CRIT_RATE ||
                       line.type() == ABILITY_LINE_TYPE_BOSS_DAMAGE ||
                       line.type() == ABILITY_LINE_TYPE_NORMAL_DAMAGE ||
                       line.type() == ABILITY_LINE_TYPE_BUFF_DURATION ||
                       line.type() == ABILITY_LINE_TYPE_ITEM_DROP ||
                       line.type() == ABILITY_LINE_TYPE_MESO;
  const int value = AbilityLineValue(line.type(), line.rank());
  return "+" + std::to_string(value) + (percent ? "%" : "");
}

std::string AbilityRankName(AbilityRank rank) {
  static_assert(AbilityRank_ARRAYSIZE == 5, "a new ability rank needs a name");
  switch (rank) {
    case ABILITY_RANK_RARE:
      return "Rare";
    case ABILITY_RANK_EPIC:
      return "Epic";
    case ABILITY_RANK_UNIQUE:
      return "Unique";
    case ABILITY_RANK_LEGENDARY:
      return "Legendary";
    default:
      return "";
  }
}

std::string CubeName(CubeType cube) {
  switch (cube) {
    case CubeType::kRed:
      return "Red Cube";
  }
  return "";
}

std::string CubeTrackName(PotentialTrack track) {
  switch (track) {
    case PotentialTrack::kMain:
      return "Main";
    case PotentialTrack::kBonus:
      return "Bonus";
  }
  return "";
}

std::string PotentialRankName(PotentialRank rank) {
  static_assert(PotentialRank_ARRAYSIZE == 5,
                "a new potential rank needs a name");
  switch (rank) {
    case POTENTIAL_RANK_RARE:
      return "Rare";
    case POTENTIAL_RANK_EPIC:
      return "Epic";
    case POTENTIAL_RANK_UNIQUE:
      return "Unique";
    case POTENTIAL_RANK_LEGENDARY:
      return "Legendary";
    default:
      return "";
  }
}

std::string PotentialLineName(PotentialLineType type) {
  static_assert(PotentialLineType_ARRAYSIZE == 28,
                "a new potential line needs a name");
  switch (type) {
    case POTENTIAL_LINE_TYPE_STR:
    case POTENTIAL_LINE_TYPE_STR_PCT:
      return "STR";
    case POTENTIAL_LINE_TYPE_DEX:
    case POTENTIAL_LINE_TYPE_DEX_PCT:
      return "DEX";
    case POTENTIAL_LINE_TYPE_INT:
    case POTENTIAL_LINE_TYPE_INT_PCT:
      return "INT";
    case POTENTIAL_LINE_TYPE_LUK:
    case POTENTIAL_LINE_TYPE_LUK_PCT:
      return "LUK";
    case POTENTIAL_LINE_TYPE_ALL_STATS:
    case POTENTIAL_LINE_TYPE_ALL_STATS_PCT:
      return "All Stats";
    case POTENTIAL_LINE_TYPE_MAX_HP:
    case POTENTIAL_LINE_TYPE_MAX_HP_PCT:
      return "Max HP";
    case POTENTIAL_LINE_TYPE_ATTACK_PCT:
      return "ATT";
    case POTENTIAL_LINE_TYPE_MAGIC_ATTACK_PCT:
      return "MATT";
    case POTENTIAL_LINE_TYPE_DAMAGE_PCT:
      return "Damage";
    case POTENTIAL_LINE_TYPE_IGNORE_DEFENSE_15:
    case POTENTIAL_LINE_TYPE_IGNORE_DEFENSE_30:
    case POTENTIAL_LINE_TYPE_IGNORE_DEFENSE_35:
    case POTENTIAL_LINE_TYPE_IGNORE_DEFENSE_40:
      return "Ignore DEF";
    case POTENTIAL_LINE_TYPE_BOSS_DAMAGE_30:
    case POTENTIAL_LINE_TYPE_BOSS_DAMAGE_35:
    case POTENTIAL_LINE_TYPE_BOSS_DAMAGE_40:
      return "Boss Damage";
    case POTENTIAL_LINE_TYPE_CRIT_DAMAGE_PCT:
      return "Critical Damage";
    case POTENTIAL_LINE_TYPE_MESO_RATE:
      return "Meso Drop Rate";
    case POTENTIAL_LINE_TYPE_ITEM_DROP_RATE:
      return "Item Drop Rate";
    case POTENTIAL_LINE_TYPE_COOLDOWN_1:
    case POTENTIAL_LINE_TYPE_COOLDOWN_2:
      return "Cooldown";
    default:
      return "";
  }
}

namespace {

// Whether a line takes something away rather than granting it, which is the
// two cooldown lines and nothing else.
bool TakesAway(PotentialLineType type) {
  return type == POTENTIAL_LINE_TYPE_COOLDOWN_1 ||
         type == POTENTIAL_LINE_TYPE_COOLDOWN_2;
}

// A value with the unit its line is stated in, and no sign: "12", "9%", "2s".
std::string PotentialValueText(PotentialLineType type, int value) {
  switch (type) {
    case POTENTIAL_LINE_TYPE_COOLDOWN_1:
    case POTENTIAL_LINE_TYPE_COOLDOWN_2:
      return std::to_string(value) + "s";
    // The flat grants. Everything else is a share of something.
    case POTENTIAL_LINE_TYPE_STR:
    case POTENTIAL_LINE_TYPE_DEX:
    case POTENTIAL_LINE_TYPE_INT:
    case POTENTIAL_LINE_TYPE_LUK:
    case POTENTIAL_LINE_TYPE_ALL_STATS:
    case POTENTIAL_LINE_TYPE_MAX_HP:
      return std::to_string(value);
    default:
      return std::to_string(value) + "%";
  }
}

// The type two lines of one effect both count under. GMS states ignored
// defence, boss damage and cooldown at several fixed sizes, and each size is
// its own type here; adding two of them up asks for the effect, not the size.
PotentialLineType PotentialLineFamily(PotentialLineType type) {
  static_assert(PotentialLineType_ARRAYSIZE == 28,
                "a new potential line needs a family");
  switch (type) {
    case POTENTIAL_LINE_TYPE_IGNORE_DEFENSE_30:
    case POTENTIAL_LINE_TYPE_IGNORE_DEFENSE_35:
    case POTENTIAL_LINE_TYPE_IGNORE_DEFENSE_40:
      return POTENTIAL_LINE_TYPE_IGNORE_DEFENSE_15;
    case POTENTIAL_LINE_TYPE_BOSS_DAMAGE_35:
    case POTENTIAL_LINE_TYPE_BOSS_DAMAGE_40:
      return POTENTIAL_LINE_TYPE_BOSS_DAMAGE_30;
    case POTENTIAL_LINE_TYPE_COOLDOWN_2:
      return POTENTIAL_LINE_TYPE_COOLDOWN_1;
    default:
      return type;
  }
}

// What the lines of `family` in `potential` come to together. Everything adds
// up except ignored defence, which meets in reverse the way it does
// everywhere else -- see AddPotential.
int PotentialFamilyTotal(const Potential& potential, PotentialLineType family,
                         int item_level) {
  bool ied = family == POTENTIAL_LINE_TYPE_IGNORE_DEFENSE_15;
  int total = 0;
  double left = 1.0;
  for (const PotentialLine& line : potential.lines()) {
    if (PotentialLineFamily(line.type()) != family) {
      continue;
    }
    int value = PotentialLineValue(line.type(), line.rank(), item_level);
    total += value;
    left *= 1.0 - value / 100.0;
  }
  return ied ? static_cast<int>(std::lround((1.0 - left) * 100.0)) : total;
}

}  // namespace

std::string PotentialLineValueText(const PotentialLine& line, int item_level) {
  const int value = PotentialLineValue(line.type(), line.rank(), item_level);
  return (TakesAway(line.type()) ? "-" : "+") +
         PotentialValueText(line.type(), value);
}

std::string PotentialLineShortName(PotentialLineType type) {
  static_assert(PotentialLineType_ARRAYSIZE == 28,
                "a new potential line needs a short name");
  switch (type) {
    case POTENTIAL_LINE_TYPE_ALL_STATS:
    case POTENTIAL_LINE_TYPE_ALL_STATS_PCT:
      return "All Stat";
    case POTENTIAL_LINE_TYPE_MAX_HP:
    case POTENTIAL_LINE_TYPE_MAX_HP_PCT:
      return "HP";
    case POTENTIAL_LINE_TYPE_IGNORE_DEFENSE_15:
    case POTENTIAL_LINE_TYPE_IGNORE_DEFENSE_30:
    case POTENTIAL_LINE_TYPE_IGNORE_DEFENSE_35:
    case POTENTIAL_LINE_TYPE_IGNORE_DEFENSE_40:
      return "IED";
    case POTENTIAL_LINE_TYPE_BOSS_DAMAGE_30:
    case POTENTIAL_LINE_TYPE_BOSS_DAMAGE_35:
    case POTENTIAL_LINE_TYPE_BOSS_DAMAGE_40:
      return "Boss";
    case POTENTIAL_LINE_TYPE_CRIT_DAMAGE_PCT:
      return "Crit DMG";
    case POTENTIAL_LINE_TYPE_MESO_RATE:
      return "Meso";
    case POTENTIAL_LINE_TYPE_ITEM_DROP_RATE:
      return "Drop";
    case POTENTIAL_LINE_TYPE_COOLDOWN_1:
    case POTENTIAL_LINE_TYPE_COOLDOWN_2:
      return "CD";
    // Everything left reads the same in a column as on a card: STR, ATT,
    // Damage.
    default:
      return PotentialLineName(type);
  }
}

std::string PotentialCell(const Potential& potential, int item_level) {
  if (potential.lines().empty()) {
    return PadRight("", kPotentialCellWidth);
  }
  // The first line names the effect the column reports: it is the one that
  // carries the potential's own rank, and so the one that says what the item
  // rolled.
  PotentialLineType family = PotentialLineFamily(potential.lines(0).type());
  int total = PotentialFamilyTotal(potential, family, item_level);
  std::string text = TakesAway(family) ? "-" : "";
  text +=
      PotentialValueText(family, total) + " " + PotentialLineShortName(family);
  return PadRight(text, kPotentialCellWidth);
}

std::string HyperStatName(HyperStatField field) {
  static_assert(HyperStatField_ARRAYSIZE == 16,
                "a new Hyper Stat needs a name and a place in the order");
  switch (field) {
    case HYPER_STAT_FIELD_STR:
      return "STR";
    case HYPER_STAT_FIELD_DEX:
      return "DEX";
    case HYPER_STAT_FIELD_INT:
      return "INT";
    case HYPER_STAT_FIELD_LUK:
      return "LUK";
    case HYPER_STAT_FIELD_MAX_HP:
      return "HP";
    case HYPER_STAT_FIELD_CRIT_RATE:
      return "Critical Rate";
    case HYPER_STAT_FIELD_CRIT_DAMAGE:
      return "Critical Damage";
    case HYPER_STAT_FIELD_IED:
      return "Ignore Defense";
    case HYPER_STAT_FIELD_DAMAGE:
      return "Damage";
    case HYPER_STAT_FIELD_BOSS_DAMAGE:
      return "Boss Damage";
    case HYPER_STAT_FIELD_NORMAL_DAMAGE:
      return "Normal Damage";
    case HYPER_STAT_FIELD_ATTACK:
      return "Attack & MATT";
    case HYPER_STAT_FIELD_EXP:
      return "Experience";
    case HYPER_STAT_FIELD_ARCANE_FORCE:
      return "Arcane Force";
    default:
      return "";
  }
}

std::string HyperStatBonusText(HyperStatField field, int level) {
  // Whole percents, except EXP's half-point steps, so the trailing zeros go.
  bool percent =
      field != HYPER_STAT_FIELD_STR && field != HYPER_STAT_FIELD_DEX &&
      field != HYPER_STAT_FIELD_INT && field != HYPER_STAT_FIELD_LUK &&
      field != HYPER_STAT_FIELD_ATTACK &&
      field != HYPER_STAT_FIELD_ARCANE_FORCE;
  double bonus = HyperStatBonus(field, level);
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%.1f", bonus);
  std::string text(buffer);
  if (text.size() > 2 && text.compare(text.size() - 2, 2, ".0") == 0) {
    text.resize(text.size() - 2);
  }
  return "+" + text + (percent ? "%" : "");
}

}  // namespace ms
