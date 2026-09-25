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
#include "src/character/skill_placement.h"
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
  // Both tables use the same label for a stat, so the label links them and
  // neither depends on the other's order.
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
  // A weapon with a one-handed and a two-handed version. The data names both to
  // mean "any sword", but "One-Handed Sword / Two-Handed Sword" is too long for
  // a column and not how a description would put it.
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

  // Walk the types in the order given, so a collapsed pair appears where its
  // first half was.
  std::string result;
  std::set<EquipType> written;
  for (EquipType type : types) {
    if (written.count(type) > 0) {
      continue;
    }
    written.insert(type);
    std::string name = FormatEquipType(type);
    for (const WeaponPair& pair : kWeaponPairs) {
      // A pair collapses only when the list holds both halves. One hand's
      // version on its own keeps its full name.
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

// Appends `skill` to `out`, after any skill it requires. Skills are keyed by
// display name, which is what a requirement names. The skill is marked before
// recursing so a cycle in the data can't loop forever.
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
    // A requirement from another page can't be ordered here. The player finds
    // it in its own book.
    if (it != by_name.end()) {
      EmitAfterRequirement(*it->second, by_name, emitted, out);
    }
  }
  out.push_back(&skill);
}

// Which block of the V page a node kind goes in, from the most exclusive to the
// least. The ranks are written out so renumbering VNodeKind can't reorder the
// page.
int VNodeRank(VNodeKind kind) {
  switch (kind) {
    case V_NODE_KIND_JOB:
      return 0;
    case V_NODE_KIND_BOOST:
      return 1;
    case V_NODE_KIND_ARCHETYPE:
      return 2;
    default:
      return 3;
  }
}

// The Vengeance forms whose toggle is on, keyed by the skill each replaces. A
// form that is toggled off is left off the page.
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
    // A form takes its parent's row instead of a row of its own. It shares that
    // skill's skill_order, so listing both would put two skills in one place.
    if (ListedIn(entry.second, advancement) && entry.second.hyper() == hyper &&
        entry.second.replaces_skill_name().empty()) {
      result.push_back(&entry.second);
    }
  }
  std::stable_sort(result.begin(), result.end(),
                   [advancement](const Skill* a, const Skill* b) {
                     return SkillOrderIn(*a, advancement) <
                            SkillOrderIn(*b, advancement);
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

std::vector<const Skill*> VNodesFor(const std::map<std::string, Skill>& catalog,
                                    JobAdvancement advancement) {
  std::vector<const Skill*> nodes;
  for (const Skill* skill : SkillsForAdvancement(catalog, advancement)) {
    if (skill->v_node() != V_NODE_KIND_UNSPECIFIED) {
      nodes.push_back(skill);
    }
  }
  std::stable_sort(nodes.begin(), nodes.end(),
                   [](const Skill* a, const Skill* b) {
                     return VNodeRank(a->v_node()) < VNodeRank(b->v_node());
                   });
  for (const Skill* node :
       SkillsForAdvancement(catalog, JOB_ADVANCEMENT_COMMON)) {
    nodes.push_back(node);
  }
  return nodes;
}

std::vector<int> VNodeSectionBreaks(const std::vector<const Skill*>& nodes) {
  std::vector<int> breaks;
  for (int i = 1; i < static_cast<int>(nodes.size()); ++i) {
    if (VNodeRank(nodes[i]->v_node()) != VNodeRank(nodes[i - 1]->v_node())) {
      breaks.push_back(i);
    }
  }
  return breaks;
}

KindTag TagFor(const Skill& skill) {
  // The attack tag is orange, not red. Red means refused (colors.h), and attack
  // skills are never a problem, so red on every one of them would waste the
  // warning.
  switch (skill.kind()) {
    case SKILL_KIND_ATTACK:
    case SKILL_KIND_ACTIVE:
      return {"A:  ", kGold};
    case SKILL_KIND_AUTO_ATTACK:
      // Purple, not another yellow: an auto-attack isn't a kind of active, and
      // two tags in the same hue read as one.
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
    case EQUIP_SET_NAME_ROOT_ABYSS_WARRIOR:
      return "Root Abyss Set (Warrior)";
    case EQUIP_SET_NAME_ROOT_ABYSS_BOWMAN:
      return "Root Abyss Set (Bowman)";
    case EQUIP_SET_NAME_ROOT_ABYSS_MAGICIAN:
      return "Root Abyss Set (Magician)";
    case EQUIP_SET_NAME_ROOT_ABYSS_THIEF:
      return "Root Abyss Set (Thief)";
    case EQUIP_SET_NAME_SENGOKU_TREASURE:
      return "Sengoku Treasure Set";
    case EQUIP_SET_NAME_ABSOLAB_WARRIOR:
      return "AbsoLab Set (Warrior)";
    case EQUIP_SET_NAME_ABSOLAB_BOWMAN:
      return "AbsoLab Set (Bowman)";
    case EQUIP_SET_NAME_ABSOLAB_MAGICIAN:
      return "AbsoLab Set (Magician)";
    case EQUIP_SET_NAME_ABSOLAB_THIEF:
      return "AbsoLab Set (Thief)";
    case EQUIP_SET_NAME_DAWN_BOSS:
      return "Dawn Boss Set";
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
    // Shortened from "Face Accessory" to fit the ten-column slot column.
    case EQUIP_SLOT_FACE_ACCESSORY:
      return "Face";
    case EQUIP_SLOT_EYE_ACCESSORY:
      return "Eye";
    // The family, not the slot: a ring in the bag or in a set is just a ring.
    // FormatWornSlot says which of the four it is worn in.
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
    // All six share one name. The item's own name says which area it is from.
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
    // Three types share one name. The item's own name already says which
    // branch's book it is.
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
      return "";  // other types have no name
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
  // These types are whole percents. The rest are flat, including Attack Speed's
  // one stage.
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

// Whether a line takes something away instead of granting it. Only the two
// cooldown lines do.
bool TakesAway(PotentialLineType type) {
  return type == POTENTIAL_LINE_TYPE_COOLDOWN_1 ||
         type == POTENTIAL_LINE_TYPE_COOLDOWN_2;
}

// A value with its unit and no sign: "12", "9%", "2s".
std::string PotentialValueText(PotentialLineType type, int value) {
  switch (type) {
    case POTENTIAL_LINE_TYPE_COOLDOWN_1:
    case POTENTIAL_LINE_TYPE_COOLDOWN_2:
      return std::to_string(value) + "s";
    // Flat grants. The rest are percentages.
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

// The line granting a percentage of `stat`. HP and MP have none.
PotentialLineType StatPercentLine(StatField stat) {
  switch (stat) {
    case STAT_FIELD_STR:
      return POTENTIAL_LINE_TYPE_STR_PCT;
    case STAT_FIELD_DEX:
      return POTENTIAL_LINE_TYPE_DEX_PCT;
    case STAT_FIELD_INT:
      return POTENTIAL_LINE_TYPE_INT_PCT;
    case STAT_FIELD_LUK:
      return POTENTIAL_LINE_TYPE_LUK_PCT;
    default:
      return POTENTIAL_LINE_TYPE_UNSPECIFIED;
  }
}

// The %attack line that raises this character's damage. A magician's damage
// uses magic attack, so a wand's weapon attack doesn't count. The stat column
// asks the same question.
PotentialLineType PrimaryAttackPercent(StatField primary) {
  return primary == STAT_FIELD_INT ? POTENTIAL_LINE_TYPE_MAGIC_ATTACK_PCT
                                   : POTENTIAL_LINE_TYPE_ATTACK_PCT;
}

// The type the column counts `type` under. GMS gives ignored defence, boss
// damage and cooldown several fixed sizes, each its own type, and this maps
// them to one effect. All Stat% maps to the character's primary stat.
PotentialLineType SummaryFamily(PotentialLineType type, StatField primary) {
  static_assert(PotentialLineType_ARRAYSIZE == 28,
                "a new potential line needs a family");
  switch (type) {
    case POTENTIAL_LINE_TYPE_IGNORE_DEFENSE_15:
    case POTENTIAL_LINE_TYPE_IGNORE_DEFENSE_30:
    case POTENTIAL_LINE_TYPE_IGNORE_DEFENSE_35:
    case POTENTIAL_LINE_TYPE_IGNORE_DEFENSE_40:
      return POTENTIAL_LINE_TYPE_IGNORE_DEFENSE_15;
    case POTENTIAL_LINE_TYPE_BOSS_DAMAGE_30:
    case POTENTIAL_LINE_TYPE_BOSS_DAMAGE_35:
    case POTENTIAL_LINE_TYPE_BOSS_DAMAGE_40:
      return POTENTIAL_LINE_TYPE_BOSS_DAMAGE_30;
    case POTENTIAL_LINE_TYPE_COOLDOWN_1:
    case POTENTIAL_LINE_TYPE_COOLDOWN_2:
      return POTENTIAL_LINE_TYPE_COOLDOWN_1;
    case POTENTIAL_LINE_TYPE_CRIT_DAMAGE_PCT:
    case POTENTIAL_LINE_TYPE_DAMAGE_PCT:
    case POTENTIAL_LINE_TYPE_MESO_RATE:
    case POTENTIAL_LINE_TYPE_ITEM_DROP_RATE:
      return type;
    case POTENTIAL_LINE_TYPE_ALL_STATS_PCT:
      return StatPercentLine(primary);
    case POTENTIAL_LINE_TYPE_ATTACK_PCT:
    case POTENTIAL_LINE_TYPE_MAGIC_ATTACK_PCT:
      return type == PrimaryAttackPercent(primary)
                 ? type
                 : POTENTIAL_LINE_TYPE_UNSPECIFIED;
    case POTENTIAL_LINE_TYPE_STR_PCT:
    case POTENTIAL_LINE_TYPE_DEX_PCT:
    case POTENTIAL_LINE_TYPE_INT_PCT:
    case POTENTIAL_LINE_TYPE_LUK_PCT:
      return type == StatPercentLine(primary) ? type
                                              : POTENTIAL_LINE_TYPE_UNSPECIFIED;
    // Flat lines and %HP. They roll on Rare items and stop mattering once an
    // item passes Rare, so the column leaves them out.
    default:
      return POTENTIAL_LINE_TYPE_UNSPECIFIED;
  }
}

// The rank of an effect the column doesn't show.
constexpr int kUnreported = -1;

// Where `family` ranks in the column, best first: crit damage, cooldown,
// %attack, boss damage and ignored defence, %damage, the meso and drop rates,
// then the character's own stat. Ties go to whichever the item rolled more of.
int SummaryRank(PotentialLineType family) {
  switch (family) {
    case POTENTIAL_LINE_TYPE_CRIT_DAMAGE_PCT:
      return 0;
    case POTENTIAL_LINE_TYPE_COOLDOWN_1:
      return 1;
    case POTENTIAL_LINE_TYPE_ATTACK_PCT:
    case POTENTIAL_LINE_TYPE_MAGIC_ATTACK_PCT:
      return 2;
    case POTENTIAL_LINE_TYPE_BOSS_DAMAGE_30:
    case POTENTIAL_LINE_TYPE_IGNORE_DEFENSE_15:
      return 3;
    case POTENTIAL_LINE_TYPE_DAMAGE_PCT:
      return 4;
    case POTENTIAL_LINE_TYPE_MESO_RATE:
    case POTENTIAL_LINE_TYPE_ITEM_DROP_RATE:
      return 5;
    case POTENTIAL_LINE_TYPE_STR_PCT:
    case POTENTIAL_LINE_TYPE_DEX_PCT:
    case POTENTIAL_LINE_TYPE_INT_PCT:
    case POTENTIAL_LINE_TYPE_LUK_PCT:
      return 6;
    default:
      return kUnreported;
  }
}

// The combined value of the `family` lines in `potential`. Everything adds
// except ignored defence, which stacks multiplicatively as it does everywhere
// else (see AddPotential).
int PotentialFamilyTotal(const Potential& potential, PotentialLineType family,
                         int item_level, StatField primary) {
  bool ied = family == POTENTIAL_LINE_TYPE_IGNORE_DEFENSE_15;
  int total = 0;
  double left = 1.0;
  for (const PotentialLine& line : potential.lines()) {
    if (SummaryFamily(line.type(), primary) != family) {
      continue;
    }
    int value = PotentialLineValue(line.type(), line.rank(), item_level);
    total += value;
    left *= 1.0 - value / 100.0;
  }
  return ied ? static_cast<int>(std::lround((1.0 - left) * 100.0)) : total;
}

// The gap between effects in a cell.
constexpr int kEffectGap = 2;

// One effect of a potential, summed over every line that grants it.
struct PotentialEffect {
  PotentialLineType family;
  int rank;
  int total;
};

bool WorthMore(const PotentialEffect& a, const PotentialEffect& b) {
  if (a.rank != b.rank) {
    return a.rank < b.rank;
  }
  return a.total > b.total;
}

// What `potential` grants a character whose primary stat is `primary`, best
// first: "12% ATT", "41% IED". At most one entry per family, and only what this
// character uses.
std::vector<std::string> PotentialEffects(const Potential& potential,
                                          int item_level, StatField primary) {
  std::vector<PotentialEffect> effects;
  for (const PotentialLine& line : potential.lines()) {
    PotentialLineType family = SummaryFamily(line.type(), primary);
    int rank = SummaryRank(family);
    bool listed = false;
    for (const PotentialEffect& effect : effects) {
      listed = listed || effect.family == family;
    }
    if (rank == kUnreported || listed) {
      continue;
    }
    effects.push_back(
        {family, rank,
         PotentialFamilyTotal(potential, family, item_level, primary)});
  }
  std::stable_sort(effects.begin(), effects.end(), WorthMore);
  std::vector<std::string> text;
  for (const PotentialEffect& effect : effects) {
    std::string entry;
    if (TakesAway(effect.family)) {
      entry = "-";
    }
    entry += PotentialValueText(effect.family, effect.total) + " " +
             PotentialLineShortName(effect.family);
    text.push_back(std::move(entry));
  }
  return text;
}

// The secondary stat, which the damage formula counts at a quarter of the
// primary. The column shows it only when the item grants this character nothing
// else: useful alone, noise beside a better line.
std::vector<std::string> SecondaryStatEffect(const Potential& potential,
                                             int item_level,
                                             StatField secondary) {
  PotentialLineType family = StatPercentLine(secondary);
  if (family == POTENTIAL_LINE_TYPE_UNSPECIFIED) {
    return {};
  }
  int total = 0;
  for (const PotentialLine& line : potential.lines()) {
    if (line.type() == family) {
      total += PotentialLineValue(line.type(), line.rank(), item_level);
    }
  }
  if (total == 0) {
    return {};
  }
  return {PotentialValueText(family, total) + " " +
          PotentialLineShortName(family)};
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
    // Everything else reads the same in a column as on a card: STR, ATT,
    // Damage.
    default:
      return PotentialLineName(type);
  }
}

std::string PotentialCell(const Potential& potential, int item_level,
                          StatField primary, StatField secondary, int width) {
  std::vector<std::string> effects =
      PotentialEffects(potential, item_level, primary);
  if (effects.empty()) {
    effects = SecondaryStatEffect(potential, item_level, secondary);
  }
  if (effects.empty()) {
    // "Junk" for a cubed item that still grants this character nothing, and "-"
    // for one that was never cubed.
    return PadRight(potential.lines().empty() ? "-" : "Junk", width);
  }
  // The best effect always shows. The others are added only while they fit
  // whole, so a cut-off number never looks like a smaller one.
  std::string text = effects.front();
  for (size_t i = 1; i < effects.size(); ++i) {
    int room = static_cast<int>(text.size() + effects[i].size()) + kEffectGap;
    if (room > width) {
      break;
    }
    text += ", " + effects[i];
  }
  return PadRight(text, width);
}

std::string PresetSlotName(StatPreset slot, bool autoswap, PresetKind kind) {
  // With the autoswap on, the two presets it uses are named for their use. The
  // third is storage it never touches, except for gear, where the boss drop
  // roll reads it regardless.
  if (autoswap) {
    switch (slot) {
      case StatPreset::kFirst:
        return "Farm";
      case StatPreset::kSecond:
        return "Boss";
      case StatPreset::kThird:
        if (kind == PresetKind::kEquip) {
          return "Drop";
        }
        break;
    }
  }
  return std::to_string(IndexOf(slot) + 1);
}

std::string PresetSlotLabel(StatPreset slot, bool autoswap, bool in_use,
                            PresetKind kind) {
  const std::string name = PresetSlotName(slot, autoswap, kind);
  // No mark with the autoswap on: the fight decides which preset is in use, and
  // it changes.
  if (autoswap) {
    return name;
  }
  // The mark's column is kept either way, so putting a preset in use doesn't
  // shift the row.
  return name + (in_use ? " \u2713" : "  ");
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
  // Percentages are whole except EXP, which moves in half points, so trailing
  // zeros are trimmed.
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
