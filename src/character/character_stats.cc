#include "src/character/character_stats.h"

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

}  // namespace

DerivedStats DerivedStatsFor(const CharacterInstance& character,
                             const std::map<std::string, Skill>& skills) {
  const Character& proto = character.proto();
  const AllocatedStats& allocated = proto.allocated_stats();
  const EquipStats& equipped = character.equip_stats();

  // Sum every learned passive first: HP has to know its whole flat total
  // before any percentage lands on it.
  int hp_per_level = 0;
  double max_hp_pct = 0.0;
  int mp_per_level = 0;
  double max_mp_pct = 0.0;
  int skill_def = 0;
  int skill_str = 0;
  int skill_dex = 0;
  int skill_luk = 0;
  double damage_taken_pct = 0.0;
  double crit_rate = 0.0;
  double mastery = 0.0;
  int attack_speed_bonus = 0;
  for (const std::pair<const std::string, Skill>& entry : skills) {
    const Skill& skill = entry.second;
    if (skill.kind() != SKILL_KIND_PASSIVE) {
      continue;
    }
    int level = character.skill_level(skill);
    if (level <= 0) {
      continue;
    }
    // Effect at level L is base + per_level * (L - 1).
    const SkillEffect& base = skill.base();
    const SkillEffect& per = skill.per_level();
    hp_per_level +=
        base.max_hp_per_level() + per.max_hp_per_level() * (level - 1);
    max_hp_pct += base.max_hp_pct() + per.max_hp_pct() * (level - 1);
    mp_per_level +=
        base.max_mp_per_level() + per.max_mp_per_level() * (level - 1);
    max_mp_pct += base.max_mp_pct() + per.max_mp_pct() * (level - 1);
    skill_def += base.def() + per.def() * (level - 1);
    skill_str += base.str() + per.str() * (level - 1);
    skill_dex += base.dex() + per.dex() * (level - 1);
    skill_luk += base.luk() + per.luk() * (level - 1);
    damage_taken_pct +=
        base.damage_taken_pct() + per.damage_taken_pct() * (level - 1);
    crit_rate += base.crit_rate() + per.crit_rate() * (level - 1);
    // The one lever here that takes the best rather than the sum: two weapon
    // masteries are not twice as steady a swing, they are the better of the
    // two. See SkillEffect::mastery.
    mastery = std::max(mastery, base.mastery() + per.mastery() * (level - 1));
    attack_speed_bonus +=
        base.attack_speed() + per.attack_speed() * (level - 1);
  }

  DerivedStats stats;
  // Per-level HP is flat HP like any other, just scaled by how far the
  // character has levelled; the percentage applies to the whole pile, and a
  // fractional point of HP is dropped. The nudge before the floor is for the
  // percentage itself: summing a skill's per-level steps lands a hair under
  // the round figure (16 levels of +1% is 0.15999...), which would otherwise
  // cost a whole point of HP.
  int flat_hp =
      allocated.hp() + equipped.max_hp() + hp_per_level * proto.level();
  stats.max_hp = static_cast<int>(
      std::floor(flat_hp * (1.0 + max_hp_pct) + kPercentEpsilon));
  // MP folds the same way, from the same three kinds of source.
  int flat_mp =
      allocated.mp() + equipped.max_mp() + mp_per_level * proto.level();
  stats.max_mp = static_cast<int>(
      std::floor(flat_mp * (1.0 + max_mp_pct) + kPercentEpsilon));
  stats.skill_stats.set_def(skill_def);
  stats.skill_stats.set_str(skill_str);
  stats.skill_stats.set_dex(skill_dex);
  stats.skill_stats.set_luk(skill_luk);
  // Base DEF is a function of the primary stats the character actually holds,
  // so it reads the totals rather than the allocation: a ring's LUK and a
  // passive's LUK are worth the same DEF, and neither is worth less than an AP
  // spent on it. That is why it waits until skill_stats above is filled --
  // reading it back is what keeps a future skill_str from being missed here.
  //
  // Floored once at the end, as GMS shows it. Only the base needs it; the worn
  // and granted DEF are whole numbers already.
  int str = allocated.str() + equipped.str() + stats.skill_stats.str();
  int dex = allocated.dex() + equipped.dex() + stats.skill_stats.dex();
  int luk = allocated.luk() + equipped.luk() + stats.skill_stats.luk();
  int base_def = static_cast<int>(
      std::floor(kDefPerStr * str + kDefPerDexLuk * (dex + luk)));
  stats.def = base_def + equipped.def() + skill_def;
  stats.damage_taken_pct = damage_taken_pct;
  stats.crit_rate = crit_rate;
  stats.mastery = mastery;
  stats.attack_speed_bonus = attack_speed_bonus;
  return stats;
}

EquipStats TotalEquipStats(const CharacterInstance& character,
                           const DerivedStats& derived) {
  const EquipStats sources[] = {character.equip_stats(), derived.skill_stats};
  return SumEquipStats(absl::MakeConstSpan(sources));
}

}  // namespace ms
