#include "src/character_stats.h"

#include <map>
#include <string>
#include <utility>

#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {

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
  }

  DerivedStats stats;
  // Per-level HP is flat HP like any other, just scaled by how far the
  // character has levelled; the percentage applies to the whole pile.
  int flat_hp =
      allocated.hp() + equipped.max_hp() + hp_per_level * proto.level();
  stats.max_hp = static_cast<int>(flat_hp * (1.0 + max_hp_pct));
  stats.def = equipped.def() + skill_def;
  stats.damage_taken_pct = damage_taken_pct;
  return stats;
}

}  // namespace ms
