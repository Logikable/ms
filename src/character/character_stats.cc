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

// Whether a list of weapon types admits `weapon`. An empty list admits every
// weapon, which is what a skill naming none means.
bool ListAllowsWeapon(const google::protobuf::RepeatedField<int>& types,
                      EquipType weapon) {
  if (types.empty()) {
    return true;
  }
  for (int type : types) {
    if (type == weapon) {
      return true;
    }
  }
  return false;
}

// Whether this is the skill that raises other skills' levels. Asked of the
// data rather than of the name, so the rule that it does not raise itself
// holds for any later skill written the same way.
bool GrantsSkillLevels(const Skill& skill) {
  return skill.base().skill_level_bonus() > 0.0;
}

// What one learned passive is worth at `level`, in the shape they are summed
// in. Every lever is base + per_level * (L - 1).
struct PassiveTotals {
  int hp_per_level = 0;
  double max_hp_pct = 0.0;
  int mp_per_level = 0;
  double max_mp_pct = 0.0;
  int def = 0;
  double def_pct = 0.0;
  int str = 0;
  int dex = 0;
  int int_ = 0;
  int luk = 0;
  int attack = 0;
  int magic_attack = 0;
  double damage_taken_pct = 0.0;
  double dodge_chance = 0.0;
  double damage_reflect_pct = 0.0;
  double crit_rate = 0.0;
  double crit_dmg = 0.0;
  double mastery = 0.0;
  double hp_recover_pct = 0.0;
  double status_resistance = 0.0;
  double elemental_resistance = 0.0;
  // Keyed by the swings it follows: two sources that follow the same swings
  // are one Final Attack. Ordered, so the result does not depend on which
  // skill was read first.
  std::map<int, double> final_attacks;
  double damage_pct = 0.0;
  double final_dmg_pct = 0.0;
  int attack_speed = 0;
  // Combo Orbs, and the two bargains priced per orb. The count is the best any
  // learned passive grants rather than the sum -- a character carries one ring
  // of orbs however many skills describe it -- and the bargains are folded
  // against it only once every passive has been read, because the skill
  // offering one is not the skill that says how many orbs there are.
  int combo_orbs = 0;
  int attack_per_combo_orb = 0;
  double final_dmg_pct_per_combo_orb = 0.0;
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
  totals.def_pct += base.def_pct() + per.def_pct() * (level - 1);
  totals.str += base.str() + per.str() * (level - 1);
  totals.dex += base.dex() + per.dex() * (level - 1);
  totals.int_ += base.int_() + per.int_() * (level - 1);
  totals.luk += base.luk() + per.luk() * (level - 1);
  totals.attack += base.attack() + per.attack() * (level - 1);
  totals.magic_attack += base.magic_attack() + per.magic_attack() * (level - 1);
  // Damage sent to the MP pool is damage the HP pool never sees, and nothing
  // here tracks MP -- so Magic Guard reads as reduction, which is its whole
  // effect. Reduction multiplies rather than adds: two halves leave a quarter
  // of the hit, where summing them would leave none of it and then go on to
  // heal the character.
  double taken = base.damage_taken_pct() + per.damage_taken_pct() * (level - 1);
  double to_mp = base.damage_to_mp_pct() + per.damage_to_mp_pct() * (level - 1);
  totals.damage_taken_pct =
      1.0 - (1.0 - totals.damage_taken_pct) * (1.0 - taken) * (1.0 - to_mp);
  // Dodging combines the same way and for the same reason: what two sources
  // leave standing is the product of what each leaves standing.
  double dodge = base.dodge_chance() + per.dodge_chance() * (level - 1);
  totals.dodge_chance = 1.0 - (1.0 - totals.dodge_chance) * (1.0 - dodge);
  totals.damage_reflect_pct +=
      base.damage_reflect_pct() + per.damage_reflect_pct() * (level - 1);
  totals.crit_rate += base.crit_rate() + per.crit_rate() * (level - 1);
  totals.crit_dmg += base.crit_dmg() + per.crit_dmg() * (level - 1);
  totals.hp_recover_pct +=
      base.hp_recover_pct() + per.hp_recover_pct() * (level - 1);
  totals.status_resistance +=
      base.status_resistance() + per.status_resistance() * (level - 1);
  totals.elemental_resistance +=
      base.elemental_resistance() + per.elemental_resistance() * (level - 1);
  totals.damage_pct += base.damage_pct() + per.damage_pct() * (level - 1);
  totals.attack_per_combo_orb +=
      base.attack_per_combo_orb() + per.attack_per_combo_orb() * (level - 1);
  totals.final_dmg_pct_per_combo_orb +=
      base.final_dmg_pct_per_combo_orb() +
      per.final_dmg_pct_per_combo_orb() * (level - 1);
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
}

// Folds one skill's Final Attack in. Split from AddEffect because what sets a
// Final Attack off belongs to the SKILL, not to the level's levers -- and
// AddEffect is handed levers with no skill behind them.
void AddFinalAttack(const Skill& skill, const SkillEffect& base,
                    const SkillEffect& per, int level, PassiveTotals& totals) {
  // Chance times damage: what the proc is worth on an average swing, which is
  // all an expected-value damage chain can use. See DerivedStats.
  double pct =
      (base.final_attack_chance() + per.final_attack_chance() * (level - 1)) *
      (base.final_attack_pct() + per.final_attack_pct() * (level - 1));
  if (pct <= 0.0) {
    return;
  }
  totals.final_attacks[skill.follows_skill_tag()] += pct;
}

void AddPassive(const Skill& skill, int level, EquipType weapon,
                PassiveTotals& totals) {
  AddEffect(skill.base(), skill.per_level(), level, totals);
  AddFinalAttack(skill, skill.base(), skill.per_level(), level, totals);
  totals.combo_orbs = std::max(totals.combo_orbs, skill.combo_orbs());
  // A weapon bonus is a second helping of the same levers for a subset of the
  // weapons the skill accepts. Read at level 1: it is flat by construction.
  for (const WeaponBonus& bonus : skill.weapon_bonus()) {
    if (bonus.required_equip_type_size() > 0 &&
        ListAllowsWeapon(bonus.required_equip_type(), weapon)) {
      AddEffect(bonus.effect(), SkillEffect::default_instance(), 1, totals);
      AddFinalAttack(skill, bonus.effect(), SkillEffect::default_instance(), 1,
                     totals);
    }
  }
}

// Cashes in the per-orb bargains against the ring of orbs the character
// carries. The orbs are taken as full: a fight paid out in expected damage has
// nowhere to put a counter, and GMS builds them up over 40% of the swings.
// The final damage lands as ONE source however many skills priced it, which is
// what "total applied between combo orbs" means.
void FoldComboOrbs(PassiveTotals& totals) {
  totals.attack += totals.attack_per_combo_orb * totals.combo_orbs;
  double orbs = totals.final_dmg_pct_per_combo_orb * totals.combo_orbs;
  totals.final_dmg_pct = (1.0 + totals.final_dmg_pct) * (1.0 + orbs) - 1.0;
}

// Sums every passive the character has learned. HP has to know its whole flat
// total before any percentage lands on it, so nothing is folded here.
PassiveTotals LearnedPassives(const CharacterInstance& character,
                              const std::map<std::string, Skill>& skills) {
  PassiveTotals totals;
  EquipType weapon = character.weapon_type();
  int bonus = BonusSkillLevels(character, skills);
  for (const std::pair<const std::string, Skill>& entry : skills) {
    const Skill& skill = entry.second;
    // Learned levels are keyed by display name, and the warrior branches share
    // several names -- so only the character's own book counts, or the other
    // branch's copy would fold in beside it.
    if (skill.kind() != SKILL_KIND_PASSIVE ||
        !character.HasAdvancement(skill.job_advancement())) {
      continue;
    }
    // A passive that demands gear grants nothing without it -- Final Attack
    // does not fire off a wand, and Shield Mastery does nothing with an empty
    // off hand. The skill stays learned; it is the effect that lapses.
    if (!SkillGearMet(character, skill)) {
      continue;
    }
    int level = EffectiveSkillLevel(character, skill, bonus);
    if (level > 0) {
      AddPassive(skill, level, weapon, totals);
    }
  }
  FoldComboOrbs(totals);
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
  return ListAllowsWeapon(skill.required_equip_type(), weapon);
}

int BonusSkillLevels(const CharacterInstance& character,
                     const std::map<std::string, Skill>& skills) {
  double bonus = 0.0;
  for (const std::pair<const std::string, Skill>& entry : skills) {
    const Skill& skill = entry.second;
    if (!GrantsSkillLevels(skill) ||
        !character.HasAdvancement(skill.job_advancement())) {
      continue;
    }
    int level = character.skill_level(skill);
    if (level > 0) {
      bonus += skill.base().skill_level_bonus() +
               skill.per_level().skill_level_bonus() * (level - 1);
    }
  }
  // Floored, and nudged first: the per-level step is a fraction that cannot be
  // written exactly, so the top of the ladder lands a hair under the whole
  // level it is meant to reach.
  return static_cast<int>(std::floor(bonus + kPercentEpsilon));
}

int EffectiveSkillLevel(const CharacterInstance& character, const Skill& skill,
                        int bonus) {
  int level = character.skill_level(skill);
  if (level <= 0 || GrantsSkillLevels(skill)) {
    return level;
  }
  return std::min(level + bonus, skill.max_level());
}

bool SkillGearMet(const CharacterInstance& character, const Skill& skill) {
  if (skill.requires_secondary() && !character.has_secondary()) {
    return false;
  }
  return SkillAllowsWeapon(skill, character.weapon_type());
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
  stats.skill_stats.set_int_(passives.int_);
  stats.skill_stats.set_luk(passives.luk);
  stats.skill_stats.set_attack(passives.attack);
  stats.skill_stats.set_magic_attack(passives.magic_attack);
  // Base DEF reads the totals rather than the allocation: a ring's LUK and a
  // passive's LUK are worth the same DEF. Floored once at the end, as GMS
  // shows it -- the worn and granted DEF are whole numbers already.
  int str = allocated.str() + equipped.str() + passives.str;
  int dex = allocated.dex() + equipped.dex() + passives.dex;
  int luk = allocated.luk() + equipped.luk() + passives.luk;
  stats.base_def = static_cast<int>(
      std::floor(kDefPerStr * str + kDefPerDexLuk * (dex + luk)));
  // The percentage lands over the whole pile, exactly as it does on the HP
  // pool: what a character wears and what their stats buy are the same DEF.
  stats.def = FoldPool(stats.base_def + equipped.def() + passives.def,
                       passives.def_pct);
  stats.damage_taken_pct = passives.damage_taken_pct;
  stats.dodge_chance = passives.dodge_chance;
  stats.damage_reflect_pct = passives.damage_reflect_pct;
  stats.crit_rate = passives.crit_rate;
  stats.crit_dmg = passives.crit_dmg;
  stats.hp_recover_pct = passives.hp_recover_pct;
  stats.status_resistance = passives.status_resistance;
  stats.elemental_resistance = passives.elemental_resistance;
  stats.damage_pct = passives.damage_pct;
  stats.final_dmg_pct = passives.final_dmg_pct;
  stats.mastery = passives.mastery;
  for (const std::pair<const int, double>& entry : passives.final_attacks) {
    FinalAttackSource source;
    source.pct = entry.second;
    source.required_tag = static_cast<SkillTag>(entry.first);
    stats.final_attacks.push_back(source);
  }
  stats.attack_speed_bonus = passives.attack_speed;
  return stats;
}

PassiveOffense PassiveOffenseFor(const DerivedStats& derived) {
  PassiveOffense passives;
  passives.crit_rate = derived.crit_rate;
  passives.crit_dmg = derived.crit_dmg;
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
