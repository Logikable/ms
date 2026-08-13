#include "src/frontend/widgets/stat_rows.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "src/character/character_stats.h"
#include "src/character/progression.h"
#include "src/combat/constants.h"
#include "src/combat/damage.h"
#include "src/frontend/widgets/panel_util.h"
#include "src/item/equip_instance.h"
#include "src/protos/equip.pb.h"

namespace ms {
namespace {

// A fraction as a percentage, two decimals: 0.055 reads "5.50%".
std::string Percent(double fraction) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%.2f%%", fraction * 100.0);
  return buf;
}

// The stage the character swings at: the weapon's own plus whatever the
// passives add, capped at the fastest tier the game models. A dash where there
// is no swing to name -- nothing in hand, or a weapon that names no stage.
std::string AttackSpeedText(const std::map<EquipSlot, EquipInstance>& equipped,
                            int bonus) {
  std::map<EquipSlot, EquipInstance>::const_iterator it =
      equipped.find(EQUIP_SLOT_PRIMARY_WEAPON);
  if (it == equipped.end() ||
      it->second.prototype().attack_speed() == ATTACK_SPEED_UNSPECIFIED) {
    return "-";
  }
  int stage = std::min(static_cast<int>(ATTACK_SPEED_FASTEST_3),
                       it->second.prototype().attack_speed() + bonus);
  return AttackSpeedName(static_cast<AttackSpeed>(stage));
}

// "358", or "(308+50) 358" when gear or a skill contributes. A skill can also
// take a stat away -- Reckless Hunt buys attack by giving DEF up -- and that
// reads "(308-50) 258": the sign belongs in the breakdown, so the player can
// see the trade they bought rather than only the number it left them.
std::string TotalWithBreakdown(int base, int bonus) {
  std::string total = std::to_string(base + bonus);
  if (bonus == 0) {
    return total;
  }
  std::string sign = bonus > 0 ? "+" : "-";
  return "(" + std::to_string(base) + sign + std::to_string(std::abs(bonus)) +
         ") " + total;
}

// The combat stats, in two tiers the panel can hold back. Both sit in the
// middle of the order rather than on the end, so leaving either out is a gap
// to close and not a tail to cut.
std::vector<StatLine> CombatStatLines(
    const CharacterInstance& character,
    const std::map<std::string, Skill>& skills, bool with_percents,
    bool with_advanced) {
  DerivedStats derived = DerivedStatsFor(character, skills);
  const EquipStats e = TotalEquipStats(character, derived);
  // Split like DEF: what the character wears and was granted, then whatever a
  // percentage added on top. Read off the unscaled sum rather than held as a
  // stat, because the split exists only for this row.
  int flat_attack =
      character.equip_stats().attack() + derived.skill_stats.attack();
  int flat_magic = character.equip_stats().magic_attack() +
                   derived.skill_stats.magic_attack();
  std::vector<StatLine> lines = {
      {"Attack", TotalWithBreakdown(flat_attack, e.attack() - flat_attack)},
      {"Magic Attack",
       TotalWithBreakdown(flat_magic, e.magic_attack() - flat_magic)},
  };
  if (with_percents) {
    lines.push_back({"Damage", Percent(derived.damage_pct)});
    lines.push_back({"Final Damage", Percent(derived.final_dmg_pct)});
    // The two that only ever matter against something the character has not
    // met yet: boss damage pays on no monster in the game, and ignoring DEF
    // pays little until the monsters have some. Both sit above the crit pair
    // because they qualify the damage rows over them.
    if (with_advanced) {
      lines.push_back(
          {"Boss Damage", Percent(e.boss_damage() / 100.0 + derived.boss_pct)});
      // What gear and skills come to between them, which is not their sum.
      // Shortened to seat the 16-column label: "Ignore Enemy Defense" is 20.
      lines.push_back(
          {"Ignore DEF", Percent(CombineIgnoredDefense(
                             e.ignore_enemy_defense() / 100.0, derived.ied))});
    }
    // The base pair every character carries, plus what they bought. The stats
    // a skill writes to hold only its own contribution, so a page reading
    // 0.00% for both would be telling a character with a 5% chance of a 35%
    // bonus that they never crit at all.
    lines.push_back(
        {"Critical Rate", Percent(kBaseCritRate + derived.crit_rate)});
    lines.push_back(
        {"Critical Damage", Percent(kBaseCritDamage + derived.crit_dmg)});
  }
  lines.push_back(
      {"Attack Speed",
       AttackSpeedText(character.equipped(), derived.attack_speed_bonus)});
  // Split like the primary stats: what the character's own stats buy, then
  // everything worn, granted or multiplied on top of it.
  lines.push_back(
      {"Defense",
       TotalWithBreakdown(derived.base_def, derived.def - derived.base_def)});
  // Last, because nothing reads either of them yet -- no mob inflicts a status
  // or an element. They are here so a player who spent SP on Endure can see
  // what they bought. Shortened to seat the 16-column label: "Elemental
  // Resistance" is 20 and would be cut mid-word.
  lines.push_back({"Elemental Resist", Percent(derived.elemental_resistance)});
  lines.push_back({"Status Resist", std::to_string(static_cast<int>(
                                        derived.status_resistance))});
  // Last of all, the two rows that are not about a fight at all: what they buy
  // is the purse and the climb rather than the swing.
  if (with_advanced) {
    lines.push_back({"Meso Drop Rate", Percent(derived.meso_pct)});
    lines.push_back({"Additional EXP", Percent(derived.exp_pct)});
  }
  return lines;
}

}  // namespace

std::vector<StatLine> ExtraStatLines(
    const CharacterInstance& character,
    const std::map<std::string, Skill>& skills) {
  return CombatStatLines(character, skills, /*with_percents=*/true,
                         /*with_advanced=*/true);
}

std::vector<StatLine> PanelExtraStatLines(
    const CharacterInstance& character,
    const std::map<std::string, Skill>& skills) {
  if (!Unlocked(Feature::kCombatStats, character)) {
    return {};
  }
  return CombatStatLines(character, skills,
                         Unlocked(Feature::kDamageStats, character),
                         Unlocked(Feature::kAdvancedStats, character));
}

std::vector<StatLine> MainStatLines(
    const CharacterInstance& character,
    const std::map<std::string, Skill>& skills) {
  DerivedStats derived = DerivedStatsFor(character, skills);
  const EquipStats e = TotalEquipStats(character, derived);
  const AllocatedStats& a = character.proto().allocated_stats();
  return {
      {"HP", std::to_string(derived.max_hp)},
      {"MP", std::to_string(derived.max_mp)},
      {"STR", TotalWithBreakdown(a.str(), e.str())},
      {"INT", TotalWithBreakdown(a.int_(), e.int_())},
      {"DEX", TotalWithBreakdown(a.dex(), e.dex())},
      {"LUK", TotalWithBreakdown(a.luk(), e.luk())},
  };
}

int CharacterCombatPower(const CharacterInstance& character,
                         const std::map<std::string, Skill>& skills) {
  const Character& p = character.proto();
  DerivedStats derived = DerivedStatsFor(character, skills);
  OffenseStats offense = OffenseStatsFor(
      p.job(), p.level(), p.allocated_stats(),
      TotalEquipStats(character, derived), character.weapon_type(),
      /*attack_skill=*/nullptr,
      /*attack_level=*/0, PassiveOffenseFor(derived));
  return CombatPower(offense);
}

std::string CombatPowerText(int power) {
  std::string value = FormatWithCommas(power);
  return (power > 999999 ? "CP " : "Combat Power ") + value;
}

}  // namespace ms
