#include "src/frontend/widgets/stat_rows.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "src/character/character_stats.h"
#include "src/character/hyper_stats.h"
#include "src/character/job_branch.h"
#include "src/character/progression.h"
#include "src/character/sacred_power.h"
#include "src/combat/constants.h"
#include "src/combat/damage.h"
#include "src/frontend/widgets/format.h"
#include "src/frontend/widgets/game_names.h"
#include "src/item/equip_instance.h"
#include "src/protos/equip.pb.h"

namespace ms {
namespace {

// A fraction as a percentage with two decimals: 0.055 reads "5.50%".
std::string Percent(double fraction) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%.2f%%", fraction * 100.0);
  return buf;
}

// The text for the Critical Rate row, which isn't always the amount bought.
// Every attack rolls against a rate capped at 100%, so a bigger number would
// promise damage no attack can deal. The archer's 5th job uses the excess, so
// only that build shows it.
std::string CritRateText(const CharacterInstance& character, double rate) {
  bool spends_excess =
      BranchOf(character.proto().job()) == JobBranch::kArcher &&
      character.proto().job_stage() >= kFifthJobStage;
  return Percent(spends_excess ? rate : std::min(1.0, rate));
}

// The attack-speed stage the character attacks at: their job's, plus what
// passives add, capped at the soft cap and past it where allowed. A dash when
// there is no weapon speed to name. A magician reads Average whatever staff
// they hold, as BaseAttackSpeedStage decides.
std::string AttackSpeedText(Job job, const WornGear& equipped, int bonus,
                            int uncapped) {
  WornGear::const_iterator it = equipped.find(EQUIP_SLOT_PRIMARY_WEAPON);
  if (it == equipped.end() ||
      it->second->prototype().attack_speed() == ATTACK_SPEED_UNSPECIFIED) {
    return "-";
  }
  int stage = AttackSpeedStage(
      BaseAttackSpeedStage(job, it->second->prototype().attack_speed()), bonus,
      uncapped);
  return AttackSpeedName(static_cast<AttackSpeed>(stage));
}

// "358", or "(308+50) 358" when gear or a skill adds to it. A skill can also
// lower a stat (Reckless Hunt trades DEF for attack), which reads "(308-50)
// 258". The breakdown shows the sign so the player can see the trade, not only
// the result.
std::string TotalWithBreakdown(int base, int bonus) {
  std::string total = std::to_string(base + bonus);
  if (bonus == 0) {
    return total;
  }
  std::string sign = bonus > 0 ? "+" : "-";
  return "(" + std::to_string(base) + sign + std::to_string(std::abs(bonus)) +
         ") " + total;
}

// The combat stats, in two tiers the panel can hide. Both tiers sit in the
// middle of the list rather than at the end, so leaving one out closes a gap
// instead of cutting off the tail.
std::vector<StatLine> CombatStatLines(
    const CharacterInstance& character,
    const std::map<std::string, Skill>& skills, bool with_percents,
    bool with_advanced, Activity preset) {
  DerivedStats derived = DerivedStatsFor(character, skills, /*buffs_up=*/{},
                                         /*allies=*/{}, preset);
  const EquipStats e = TotalEquipStats(character, derived);
  // The gear the row shows, which is the preset for the activity.
  const EquipStats& worn =
      character.equip_stats(character.SlotFor(PresetKind::kEquip, preset));
  // What the character wears and was granted, then what percentages added on
  // top. It is read from the unscaled sum instead of kept as a stat, because
  // only this row needs the split.
  int flat_attack = worn.attack() + derived.skill_stats.attack();
  int flat_magic = worn.magic_attack() + derived.skill_stats.magic_attack();
  std::vector<StatLine> lines = {
      {"Attack", TotalWithBreakdown(flat_attack, e.attack() - flat_attack)},
      {"Magic Attack",
       TotalWithBreakdown(flat_magic, e.magic_attack() - flat_magic)},
  };
  if (with_percents) {
    // Final Damage leads the damage rows because it multiplies everything the
    // rows below it feed into, which is the order they are read in.
    lines.push_back({"Final Damage", Percent(derived.final_dmg_pct)});
    lines.push_back({"Damage", Percent(derived.damage_pct)});
    // Boss damage applies only in boss fights, and ignoring DEF only against
    // the monster's defence. Both sit above the crit pair because they qualify
    // the damage rows above them.
    if (with_advanced) {
      lines.push_back(
          {"Boss Damage", Percent(e.boss_damage() / 100.0 + derived.boss_pct)});
      // The counterpart of boss damage. It sits beside boss damage because the
      // pair shows which half of the game a build aims at.
      lines.push_back({"Normal Damage", Percent(derived.normal_pct)});
      // The base, gear and skills combine into this value, which is not their
      // sum. Shortened because "Ignore Enemy Defense" overflows the label.
      lines.push_back({"Ignore DEF", Percent(CombineIgnoredDefense(
                                         kBaseIgnoreDefense,
                                         CombineIgnoredDefense(
                                             e.ignore_enemy_defense() / 100.0,
                                             derived.ied)))});
    }
    // The base pair every character has, plus what they bought. The stats a
    // skill writes hold only its own share, so showing 0.00% for both would
    // tell a character with a 5% chance of a 35% bonus that they never crit.
    lines.push_back(
        {"Critical Rate",
         CritRateText(character, kBaseCritRate + derived.crit_rate)});
    lines.push_back(
        {"Critical Damage", Percent(kBaseCritDamage + derived.crit_dmg)});
    // Below the crit pair because it affects neither. It lengthens the buffs
    // the character keeps up, which no other row here shows.
    lines.push_back({"Buff Duration",
                     Percent(kBaseBuffDuration + derived.buff_duration_pct)});
  }
  lines.push_back(
      {"Attack Speed",
       AttackSpeedText(
           character.proto().job(),
           character.equipped(character.SlotFor(PresetKind::kEquip, preset)),
           derived.attack_speed_bonus, derived.uncapped_attack_speed_bonus)});
  // Below a rule, since none of these are about attacking. The first three
  // affect income and levelling, and the rest are the Arcane River and Grandis
  // requirements. Meso Drop Rate is the size of a drop and Item Drop Rate the
  // chance of one, so they sit together.
  if (with_advanced) {
    lines.push_back(StatRule());
    // How much more a kill pays than it would with no bonus, so a potion that
    // multiplies meso and a passive that adds to it show on the same row. A 20%
    // bonus under a 1.2x multiplier is the 44% the player earns.
    lines.push_back(
        {"Meso Drop Rate",
         Percent((1.0 + MesoBonus(derived)) * derived.meso_final_mult - 1.0)});
    lines.push_back({"Item Drop Rate", Percent(derived.item_drop_pct)});
    lines.push_back({"Additional EXP", Percent(derived.exp_pct)});
    lines.push_back({"Arcane Force", std::to_string(character.arcane_force())});
    if (character.proto().level() >= kGrandisLevel) {
      lines.push_back(
          {"Sacred Power", std::to_string(character.sacred_power())});
    }
  }
  return lines;
}

}  // namespace

std::vector<StatLine> ExtraStatLines(const CharacterInstance& character,
                                     const std::map<std::string, Skill>& skills,
                                     Activity preset) {
  return CombatStatLines(character, skills, /*with_percents=*/true,
                         /*with_advanced=*/true, preset);
}

std::vector<StatLine> PanelExtraStatLines(
    const CharacterInstance& character, const AccountInstance& account,
    const std::map<std::string, Skill>& skills, Activity preset) {
  if (!Unlocked(Feature::kCombatStats, character, account)) {
    return {};
  }
  return CombatStatLines(
      character, skills, Unlocked(Feature::kDamageStats, character, account),
      Unlocked(Feature::kAdvancedStats, character, account), preset);
}

std::vector<StatLine> MainStatLines(const CharacterInstance& character,
                                    const std::map<std::string, Skill>& skills,
                                    Activity preset) {
  DerivedStats derived = DerivedStatsFor(character, skills, /*buffs_up=*/{},
                                         /*allies=*/{}, preset);
  const EquipStats e = TotalEquipStats(character, derived);
  const AllocatedStats& a = character.proto().allocated_stats();
  return {
      {"STR", TotalWithBreakdown(a.str(), e.str())},
      {"DEX", TotalWithBreakdown(a.dex(), e.dex())},
      {"INT", TotalWithBreakdown(a.int_(), e.int_())},
      {"LUK", TotalWithBreakdown(a.luk(), e.luk())},
  };
}

std::string CombatPowerText(int power) {
  std::string value = FormatWithCommas(power);
  return (power > 999999 ? "CP " : "Combat Power ") + value;
}

}  // namespace ms
