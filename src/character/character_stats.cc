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
  // Held as the factor the DEF pile is multiplied by rather than as a sum of
  // percentages, because two sources multiply: Phoenix's +30% and Reckless
  // Hunt's -25% leave 97.5% of the armour, not 105% of it.
  double def_factor = 1.0;
  int str = 0;
  int dex = 0;
  int int_ = 0;
  int luk = 0;
  int attack = 0;
  double attack_pct = 0.0;
  int magic_attack = 0;
  double damage_taken_pct = 0.0;
  double dodge_chance = 0.0;
  double damage_reflect_pct = 0.0;
  double crit_rate = 0.0;
  double crit_dmg = 0.0;
  double mastery = 0.0;
  double hp_recover_pct = 0.0;
  double exp_pct = 0.0;
  double regen_pct_per_second = 0.0;
  double status_resistance = 0.0;
  double elemental_resistance = 0.0;
  // Keyed by the swings it follows: two sources that follow the same swings
  // are one Final Attack. Ordered, so the result does not depend on which
  // skill was read first.
  std::map<int, double> final_attacks;
  // Pick Pocket's chance and Meso Explosion's damage, which live on two
  // different skills and are worth nothing apart -- totalled here and paired
  // once the fold is done.
  double meso_drop_chance = 0.0;
  // Per line until FoldMesoExplosion multiplies the count in.
  double meso_hit_pct = 0.0;
  int meso_lines = 1;
  std::string meso_skill;
  double meso_pct = 0.0;
  // Damage added to one named skill apiece. Summed per name, so two passives
  // strengthening the same swing both count.
  std::map<std::string, double> skill_pct_bonus;
  double damage_pct = 0.0;
  double boss_pct = 0.0;
  double mirror_line_pct = 0.0;
  double final_dmg_pct = 0.0;
  double ied = 0.0;
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
  totals.def_factor *= 1.0 + base.def_pct() + per.def_pct() * (level - 1);
  totals.str += base.str() + per.str() * (level - 1);
  totals.dex += base.dex() + per.dex() * (level - 1);
  totals.int_ += base.int_() + per.int_() * (level - 1);
  totals.luk += base.luk() + per.luk() * (level - 1);
  totals.attack += base.attack() + per.attack() * (level - 1);
  totals.attack_pct += base.attack_pct() + per.attack_pct() * (level - 1);
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
  totals.exp_pct += base.exp_pct() + per.exp_pct() * (level - 1);
  // What reaches the fight is the rate, so the pulse and its interval are read
  // together here and never separately.
  double regen_interval = base.regen_interval_seconds() +
                          per.regen_interval_seconds() * (level - 1);
  if (regen_interval > 0.0) {
    totals.regen_pct_per_second +=
        (base.regen_pct() + per.regen_pct() * (level - 1)) / regen_interval;
  }
  totals.status_resistance +=
      base.status_resistance() + per.status_resistance() * (level - 1);
  totals.elemental_resistance +=
      base.elemental_resistance() + per.elemental_resistance() * (level - 1);
  totals.damage_pct += base.damage_pct() + per.damage_pct() * (level - 1);
  totals.boss_pct += base.boss_pct() + per.boss_pct() * (level - 1);
  totals.meso_pct += base.meso_pct() + per.meso_pct() * (level - 1);
  totals.meso_drop_chance +=
      base.meso_drop_chance() + per.meso_drop_chance() * (level - 1);
  totals.mirror_line_pct +=
      base.mirror_line_pct() + per.mirror_line_pct() * (level - 1);
  totals.attack_per_combo_orb +=
      base.attack_per_combo_orb() + per.attack_per_combo_orb() * (level - 1);
  totals.final_dmg_pct_per_combo_orb +=
      base.final_dmg_pct_per_combo_orb() +
      per.final_dmg_pct_per_combo_orb() * (level - 1);
  totals.attack_speed += base.attack_speed() + per.attack_speed() * (level - 1);
  totals.ied = CombineIgnoredDefense(
      totals.ied, base.ied_pct() + per.ied_pct() * (level - 1));
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

// Notes Meso Explosion down. Recorded rather than folded: Meso Mastery's
// points land on each of its lines, the two skills fold in catalog order, and
// so the pair cannot be settled until every passive is in. See
// FoldMesoExplosion.
void AddMesoExplosion(const Skill& skill, int level, PassiveTotals& totals) {
  double per_line = skill.base().meso_hit_pct() +
                    skill.per_level().meso_hit_pct() * (level - 1);
  if (per_line <= 0.0) {
    return;
  }
  totals.meso_skill = skill.name();
  totals.meso_hit_pct += per_line;
  totals.meso_lines = std::max(1, skill.lines());
}

void AddPassive(const Skill& skill, int level, EquipType weapon,
                PassiveTotals& totals) {
  AddEffect(skill.base(), skill.per_level(), level, totals);
  // Folded here rather than in AddEffect, which is handed levers with no skill
  // behind them -- and which skill is strengthened is written on the skill.
  double boost = skill.base().boosted_skill_pct() +
                 skill.per_level().boosted_skill_pct() * (level - 1);
  if (boost > 0.0 && !skill.boosts_skill_name().empty()) {
    totals.skill_pct_bonus[skill.boosts_skill_name()] += boost;
  }
  AddFinalAttack(skill, skill.base(), skill.per_level(), level, totals);
  AddMesoExplosion(skill, level, totals);
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
// Turns what one line of a thrown meso is worth into what one whole meso is,
// now that Meso Mastery's points are certain to be in.
void FoldMesoExplosion(PassiveTotals& totals) {
  if (totals.meso_hit_pct <= 0.0) {
    return;
  }
  std::map<std::string, double>::const_iterator boost =
      totals.skill_pct_bonus.find(totals.meso_skill);
  if (boost != totals.skill_pct_bonus.end()) {
    totals.meso_hit_pct += boost->second;
  }
  totals.meso_hit_pct *= totals.meso_lines;
}

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
    // Every kind is read, not only the passives: GMS hangs permanent grants off
    // active skills too, and marks them "[Passive Effects: ...]" when it does.
    // Phoenix is the first here -- a summon that also raises DEF for good. A
    // skill with no lever contributes nothing whatever kind it is, so this
    // costs the rest of the catalog nothing.
    //
    // Learned levels are keyed by display name, and the warrior branches share
    // several names -- so only the character's own book counts, or the other
    // branch's copy would fold in beside it.
    if (!character.HasAdvancement(skill.job_advancement())) {
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
  FoldMesoExplosion(totals);
  FoldComboOrbs(totals);
  return totals;
}

// A flat total, then the percentage over the whole of it, with the fraction
// dropped. Every pile that takes a percentage folds through here: the HP and
// MP pools, DEF, and what the character swings with. The nudge before the
// floor is for the percentage: summing a skill's per-level steps lands a hair
// under the round figure (16 levels of +1% is 0.15999...), which would
// otherwise cost a whole point.
int FoldPercent(int flat, double pct) {
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
  stats.max_hp = FoldPercent(allocated.hp() + equipped.max_hp() +
                                 passives.hp_per_level * proto.level(),
                             passives.max_hp_pct);
  stats.max_mp = FoldPercent(allocated.mp() + equipped.max_mp() +
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
  // It can also be a loss -- Reckless Hunt buys attack by giving DEF up -- and
  // a character deep enough in the red ends with less DEF than their stats
  // alone bought them.
  stats.def = FoldPercent(stats.base_def + equipped.def() + passives.def,
                          passives.def_factor - 1.0);
  stats.damage_taken_pct = passives.damage_taken_pct;
  stats.dodge_chance = passives.dodge_chance;
  stats.damage_reflect_pct = passives.damage_reflect_pct;
  stats.crit_rate = passives.crit_rate;
  stats.crit_dmg = passives.crit_dmg;
  stats.hp_recover_pct = passives.hp_recover_pct;
  stats.exp_pct = passives.exp_pct;
  stats.regen_pct_per_second = passives.regen_pct_per_second;
  stats.status_resistance = passives.status_resistance;
  stats.elemental_resistance = passives.elemental_resistance;
  stats.damage_pct = passives.damage_pct;
  stats.boss_pct = passives.boss_pct;
  stats.meso_pct = passives.meso_pct;
  stats.mirror_line_pct = passives.mirror_line_pct;
  stats.final_dmg_pct = passives.final_dmg_pct;
  stats.ied = passives.ied;
  stats.mastery = passives.mastery;
  for (const std::pair<const int, double>& entry : passives.final_attacks) {
    FinalAttackSource source;
    source.pct = entry.second;
    source.required_tag = static_cast<SkillTag>(entry.first);
    stats.final_attacks.push_back(source);
  }
  // Pick Pocket and Meso Explosion, worth nothing apart: a meso falls out of
  // an enemy and is thrown straight back at them. It rides the swing exactly
  // as a Final Attack does, except that the roll is per line -- so it is one
  // more source in the same list rather than a mechanism of its own.
  if (passives.meso_drop_chance > 0.0 && passives.meso_hit_pct > 0.0) {
    FinalAttackSource meso;
    meso.pct = passives.meso_drop_chance * passives.meso_hit_pct;
    meso.per_line = true;
    stats.final_attacks.push_back(meso);
  }
  stats.attack_speed_bonus = passives.attack_speed;
  stats.attack_pct = passives.attack_pct;
  stats.skill_pct_bonus = passives.skill_pct_bonus;
  return stats;
}

PassiveOffense PassiveOffenseFor(const DerivedStats& derived) {
  PassiveOffense passives;
  passives.crit_rate = derived.crit_rate;
  passives.crit_dmg = derived.crit_dmg;
  passives.mastery = derived.mastery;
  passives.damage_pct = derived.damage_pct;
  passives.boss_pct = derived.boss_pct;
  passives.mirror_line_pct = derived.mirror_line_pct;
  passives.final_dmg_pct = derived.final_dmg_pct;
  passives.ied = derived.ied;
  passives.skill_pct_bonus = derived.skill_pct_bonus;
  return passives;
}

EquipStats TotalEquipStats(const CharacterInstance& character,
                           const DerivedStats& derived) {
  const EquipStats sources[] = {character.equip_stats(), derived.skill_stats};
  EquipStats total = SumEquipStats(absl::MakeConstSpan(sources));
  // The percentage lands here rather than in skill_stats, because what it
  // scales is the weapon in the character's hand as much as the skill's own
  // grant. Both attack fields take it: a magician swings on magic attack, and
  // a percentage of what you swing on means the same thing either way.
  total.set_attack(FoldPercent(total.attack(), derived.attack_pct));
  total.set_magic_attack(FoldPercent(total.magic_attack(), derived.attack_pct));
  return total;
}

}  // namespace ms
