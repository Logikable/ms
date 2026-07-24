#include "src/character_stats.h"

#include <cmath>
#include <map>
#include <string>
#include <utility>

#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {
namespace {

// Slack for the floor below, far smaller than any percentage a skill grants.
constexpr double kPercentEpsilon = 1e-9;

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
  int skill_def = 0;
  double damage_taken_pct = 0.0;
  double crit_rate = 0.0;
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
    skill_def += base.def() + per.def() * (level - 1);
    damage_taken_pct +=
        base.damage_taken_pct() + per.damage_taken_pct() * (level - 1);
    crit_rate += base.crit_rate() + per.crit_rate() * (level - 1);
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
  stats.def = equipped.def() + skill_def;
  stats.damage_taken_pct = damage_taken_pct;
  stats.crit_rate = crit_rate;
  return stats;
}

}  // namespace ms
