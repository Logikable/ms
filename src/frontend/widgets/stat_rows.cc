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
#include "src/frontend/widgets/format.h"
#include "src/frontend/widgets/game_names.h"
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

// The stage the character swings at: the stage their job starts from plus
// whatever the passives add, capped at the fastest tier the game models. A
// dash where there is no swing to name -- nothing in hand, or a weapon that
// names no stage. A magician reads Average whatever staff they hold, which is
// the row saying what BaseAttackSpeedStage does.
std::string AttackSpeedText(Job job,
                            const std::map<EquipSlot, EquipInstance>& equipped,
                            int bonus) {
  std::map<EquipSlot, EquipInstance>::const_iterator it =
      equipped.find(EQUIP_SLOT_PRIMARY_WEAPON);
  if (it == equipped.end() ||
      it->second.prototype().attack_speed() == ATTACK_SPEED_UNSPECIFIED) {
    return "-";
  }
  int stage = std::min(
      static_cast<int>(ATTACK_SPEED_FASTEST_3),
      BaseAttackSpeedStage(job, it->second.prototype().attack_speed()) + bonus);
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
    bool with_advanced, StatPreset preset) {
  DerivedStats derived = DerivedStatsFor(character, skills, /*buffs_up=*/{},
                                         /*allies=*/{}, preset);
  const EquipStats e = TotalEquipStats(character, derived);
  // What the character wears and was granted, then whatever a percentage added
  // on top. Read off the unscaled sum rather than held as a stat, because the
  // split exists only for this row.
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
      // Its mirror, and only a Hyper Stat grants it. Beside boss damage
      // because the pair says which half of the game a build is aimed at.
      lines.push_back({"Normal Damage", Percent(derived.normal_pct)});
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
    // Under the crit pair because it qualifies neither: what it lengthens is
    // whatever the character keeps up, which no other row here shows.
    lines.push_back({"Buff Duration", Percent(derived.buff_duration_pct)});
  }
  lines.push_back(
      {"Attack Speed",
       AttackSpeedText(character.proto().job(), character.equipped(),
                       derived.attack_speed_bonus)});
  // Under a rule, because none of these is about the swing: the first three
  // buy the purse and the climb, and the last is the toll Arcane River takes
  // for letting a character hurt what lives there. Meso Drop Rate is the size
  // of a drop and Item Drop Rate the odds of one, so they read as a pair and
  // sit together.
  if (with_advanced) {
    lines.push_back(StatRule());
    lines.push_back({"Meso Drop Rate", Percent(derived.meso_pct)});
    lines.push_back({"Item Drop Rate", Percent(derived.item_drop_pct)});
    lines.push_back({"Additional EXP", Percent(derived.exp_pct)});
    lines.push_back({"Arcane Force", std::to_string(character.arcane_force())});
  }
  return lines;
}

}  // namespace

std::vector<StatLine> ExtraStatLines(const CharacterInstance& character,
                                     const std::map<std::string, Skill>& skills,
                                     StatPreset preset) {
  return CombatStatLines(character, skills, /*with_percents=*/true,
                         /*with_advanced=*/true, preset);
}

std::vector<StatLine> PanelExtraStatLines(
    const CharacterInstance& character, const AccountInstance& account,
    const std::map<std::string, Skill>& skills, StatPreset preset) {
  if (!Unlocked(Feature::kCombatStats, character, account)) {
    return {};
  }
  return CombatStatLines(
      character, skills, Unlocked(Feature::kDamageStats, character, account),
      Unlocked(Feature::kAdvancedStats, character, account), preset);
}

std::vector<StatLine> MainStatLines(const CharacterInstance& character,
                                    const std::map<std::string, Skill>& skills,
                                    StatPreset preset) {
  DerivedStats derived = DerivedStatsFor(character, skills, /*buffs_up=*/{},
                                         /*allies=*/{}, preset);
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
                         const std::map<std::string, Skill>& skills,
                         StatPreset preset) {
  const Character& p = character.proto();
  DerivedStats derived = DerivedStatsFor(character, skills, /*buffs_up=*/{},
                                         /*allies=*/{}, preset);
  OffenseStats offense = OffenseStatsFor(
      p.job(), p.level(), p.allocated_stats(),
      TotalEquipStats(character, derived), character.weapon_type(),
      /*attack_skill=*/nullptr,
      /*attack_level=*/0, PassiveOffenseFor(derived));
  return CombatPower(offense, preset == StatPreset::kBossing);
}

std::string CombatPowerText(int power) {
  std::string value = FormatWithCommas(power);
  return (power > 999999 ? "CP " : "Combat Power ") + value;
}

}  // namespace ms
