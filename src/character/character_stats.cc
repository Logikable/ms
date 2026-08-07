#include "src/character/character_stats.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <utility>

#include "absl/types/span.h"
#include "src/item/equip_stats.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {
namespace {

// Slack for the floor below, far smaller than any percentage a skill grants.
constexpr double kPercentEpsilon = 1e-9;

// DEF every character carries for their primary stats, before anything is
// worn: 1.5 for each point of STR and 0.4 for each point of DEX and of LUK.
// INT buys none -- a magician's bulk comes from elsewhere.
constexpr double kDefPerStr = 1.5;
constexpr double kDefPerDexLuk = 0.4;

// What one learned passive is worth at `level`, in the shape they are summed
// in. Every lever is base + per_level * (L - 1).
struct PassiveTotals {
  int hp_per_level = 0;
  double max_hp_pct = 0.0;
  int mp_per_level = 0;
  double max_mp_pct = 0.0;
  int def = 0;
  int str = 0;
  int dex = 0;
  int luk = 0;
  int attack = 0;
  double damage_taken_pct = 0.0;
  double damage_reflect_pct = 0.0;
  double crit_rate = 0.0;
  double mastery = 0.0;
  double final_attack_pct = 0.0;
  double damage_pct = 0.0;
  double final_dmg_pct = 0.0;
  int attack_speed = 0;
};

// Folds one skill's levers in at `level`, on top of whatever is already there.
// Split out from AddPassive because a weapon bonus is a second helping of the
// same levers, gated on the weapon rather than on the skill.
void AddEffect(const SkillEffect& base, const SkillEffect& per, int level,
               PassiveTotals& totals) {
  totals.hp_per_level +=
      base.max_hp_per_level() + per.max_hp_per_level() * (level - 1);
  totals.max_hp_pct += base.max_hp_pct() + per.max_hp_pct() * (level - 1);
  totals.mp_per_level +=
      base.max_mp_per_level() + per.max_mp_per_level() * (level - 1);
  totals.max_mp_pct += base.max_mp_pct() + per.max_mp_pct() * (level - 1);
  totals.def += base.def() + per.def() * (level - 1);
  totals.str += base.str() + per.str() * (level - 1);
  totals.dex += base.dex() + per.dex() * (level - 1);
  totals.luk += base.luk() + per.luk() * (level - 1);
  totals.attack += base.attack() + per.attack() * (level - 1);
  totals.damage_taken_pct +=
      base.damage_taken_pct() + per.damage_taken_pct() * (level - 1);
  totals.damage_reflect_pct +=
      base.damage_reflect_pct() + per.damage_reflect_pct() * (level - 1);
  totals.crit_rate += base.crit_rate() + per.crit_rate() * (level - 1);
  totals.damage_pct += base.damage_pct() + per.damage_pct() * (level - 1);
  totals.attack_speed += base.attack_speed() + per.attack_speed() * (level - 1);
  // The one lever taken at its best rather than summed: two masteries are not
  // twice as steady a swing, they are the better of the two.
  totals.mastery =
      std::max(totals.mastery, base.mastery() + per.mastery() * (level - 1));
  // Final damage is the one that multiplies: two sources of 10% are worth 21%.
  // Kept as the combined fraction, since that is the single number the damage
  // chain applies.
  double final_dmg = base.final_dmg_pct() + per.final_dmg_pct() * (level - 1);
  totals.final_dmg_pct = (1.0 + totals.final_dmg_pct) * (1.0 + final_dmg) - 1.0;
  // Chance times damage: what the proc is worth on an average swing, which is
  // all an expected-value damage chain can use. See DerivedStats.
  totals.final_attack_pct +=
      (base.final_attack_chance() + per.final_attack_chance() * (level - 1)) *
      (base.final_attack_pct() + per.final_attack_pct() * (level - 1));
}

void AddPassive(const Skill& skill, int level, PassiveTotals& totals) {
  AddEffect(skill.base(), skill.per_level(), level, totals);
  // The orbs are taken as full, so what a per-orb grant is worth is the whole
  // ring of them. See SkillEffect::attack_per_combo_orb.
  totals.attack += (skill.base().attack_per_combo_orb() +
                    skill.per_level().attack_per_combo_orb() * (level - 1)) *
                   skill.combo_orbs();
}

// Sums every passive the character has learned. HP has to know its whole flat
// total before any percentage lands on it, so nothing is folded here.
PassiveTotals LearnedPassives(const CharacterInstance& character,
                              const std::map<std::string, Skill>& skills) {
  PassiveTotals totals;
  EquipType weapon = character.weapon_type();
  for (const std::pair<const std::string, Skill>& entry : skills) {
    const Skill& skill = entry.second;
    // Learned levels are keyed by display name, and the warrior branches share
    // several names -- so only the character's own book counts, or the other
    // branch's copy would fold in beside it.
    if (skill.kind() != SKILL_KIND_PASSIVE ||
        !character.HasAdvancement(skill.job_advancement())) {
      continue;
    }
    // A passive that demands a weapon grants nothing while another is held --
    // Final Attack does not fire off a wand. The skill stays learned; it is
    // the effect that lapses, and comes back with the right weapon in hand.
    if (!SkillAllowsWeapon(skill, weapon)) {
      continue;
    }
    int level = character.skill_level(skill);
    if (level > 0) {
      AddPassive(skill, level, totals);
    }
  }
  return totals;
}

// Flat HP or MP, then the percentage over the whole pile, with the fraction
// dropped. The nudge before the floor is for the percentage: summing a skill's
// per-level steps lands a hair under the round figure (16 levels of +1% is
// 0.15999...), which would otherwise cost a whole point.
int FoldPool(int flat, double pct) {
  return static_cast<int>(std::floor(flat * (1.0 + pct) + kPercentEpsilon));
}

}  // namespace

bool SkillAllowsWeapon(const Skill& skill, EquipType weapon) {
  if (skill.required_equip_type_size() == 0) {
    return true;
  }
  for (int i = 0; i < skill.required_equip_type_size(); ++i) {
    if (skill.required_equip_type(i) == weapon) {
      return true;
    }
  }
  return false;
}

DerivedStats DerivedStatsFor(const CharacterInstance& character,
                             const std::map<std::string, Skill>& skills) {
  const Character& proto = character.proto();
  const AllocatedStats& allocated = proto.allocated_stats();
  const EquipStats& equipped = character.equip_stats();
  PassiveTotals passives = LearnedPassives(character, skills);

  DerivedStats stats;
  stats.max_hp = FoldPool(allocated.hp() + equipped.max_hp() +
                              passives.hp_per_level * proto.level(),
                          passives.max_hp_pct);
  stats.max_mp = FoldPool(allocated.mp() + equipped.max_mp() +
                              passives.mp_per_level * proto.level(),
                          passives.max_mp_pct);
  stats.skill_stats.set_def(passives.def);
  stats.skill_stats.set_str(passives.str);
  stats.skill_stats.set_dex(passives.dex);
  stats.skill_stats.set_luk(passives.luk);
  stats.skill_stats.set_attack(passives.attack);
  // Base DEF reads the totals rather than the allocation: a ring's LUK and a
  // passive's LUK are worth the same DEF. Floored once at the end, as GMS
  // shows it -- the worn and granted DEF are whole numbers already.
  int str = allocated.str() + equipped.str() + passives.str;
  int dex = allocated.dex() + equipped.dex() + passives.dex;
  int luk = allocated.luk() + equipped.luk() + passives.luk;
  int base_def = static_cast<int>(
      std::floor(kDefPerStr * str + kDefPerDexLuk * (dex + luk)));
  stats.def = base_def + equipped.def() + passives.def;
  stats.damage_taken_pct = passives.damage_taken_pct;
  stats.damage_reflect_pct = passives.damage_reflect_pct;
  stats.crit_rate = passives.crit_rate;
  stats.damage_pct = passives.damage_pct;
  stats.final_dmg_pct = passives.final_dmg_pct;
  stats.mastery = passives.mastery;
  stats.final_attack_pct = passives.final_attack_pct;
  stats.attack_speed_bonus = passives.attack_speed;
  return stats;
}

PassiveOffense PassiveOffenseFor(const DerivedStats& derived) {
  PassiveOffense passives;
  passives.crit_rate = derived.crit_rate;
  passives.mastery = derived.mastery;
  passives.damage_pct = derived.damage_pct;
  passives.final_dmg_pct = derived.final_dmg_pct;
  return passives;
}

EquipStats TotalEquipStats(const CharacterInstance& character,
                           const DerivedStats& derived) {
  const EquipStats sources[] = {character.equip_stats(), derived.skill_stats};
  return SumEquipStats(absl::MakeConstSpan(sources));
}

}  // namespace ms
