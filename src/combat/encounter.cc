#include "src/combat/encounter.h"

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "src/character.h"
#include "src/character_stats.h"
#include "src/combat/constants.h"
#include "src/combat/damage.h"
#include "src/equip_instance.h"
#include "src/game_state.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/map.pb.h"
#include "src/protos/mob.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {
namespace {

// One attack's damage against every mob type on the map. `skill` is null for
// the bare poke, which hits one target for the character's plain 100% swing.
AttackOption AttackFor(const GameState& state, const Character& proto,
                       const Skill* skill, int level,
                       const std::vector<CombatType>& types,
                       const DerivedStats& derived) {
  AttackOption attack;
  if (skill != nullptr) {
    attack.name = skill->name();
    attack.max_enemies = std::max(1, skill->max_enemies());
  }
  OffenseStats offense = OffenseStatsFor(
      proto.job(), proto.level(), proto.allocated_stats(),
      state.character.equip_stats(), skill, level, derived.crit_rate);
  for (const CombatType& type : types) {
    attack.damage_per_hit.push_back(ExpectedAttackDamage(offense, *type.mob));
  }
  return attack;
}

}  // namespace

CombatParams ComputeCombatParams(const GameState& state) {
  CombatParams params;
  params.map = state.current_map;
  std::map<std::string, MapData>::const_iterator map_it =
      state.maps.find(state.current_map);
  if (map_it == state.maps.end()) {
    return params;
  }
  const MapData& map = map_it->second;

  const std::map<EquipSlot, EquipInstance>& equipped =
      state.character.equipped();
  std::map<EquipSlot, EquipInstance>::const_iterator weapon_it =
      equipped.find(EQUIP_SLOT_PRIMARY_WEAPON);
  if (weapon_it == equipped.end()) {
    return params;
  }
  const EquipPrototype& weapon = weapon_it->second.prototype();

  // Passive skills that speed the swing add stages on top of the weapon's own
  // attack speed, capped at the fastest tier we model. Faster swings are a
  // flat DPS gain, so this is resolved once and folded into the swing clock.
  DerivedStats derived = DerivedStatsFor(state.character, state.skills);
  int attack_speed =
      std::min(static_cast<int>(ATTACK_SPEED_FASTEST_3),
               weapon.attack_speed() + derived.attack_speed_bonus);
  params.swing_seconds =
      SwingIntervalSeconds(BaseAttackDelayMs(weapon.equip_type()),
                           attack_speed) *
      kGameSpeedFactor;
  params.respawn_seconds = kRespawnIntervalSeconds * kGameSpeedFactor;
  for (const MapData::Spawn& spawn : map.spawns()) {
    std::map<std::string, Mob>::const_iterator mob_it =
        state.mobs.find(spawn.mob());
    if (mob_it == state.mobs.end()) {
      continue;
    }
    CombatType type;
    type.mob = &mob_it->second;
    type.simultaneous = spawn.count();
    params.types.push_back(std::move(type));
  }
  if (params.types.empty()) {
    return params;
  }

  // Every attack the character could swing with: the bare poke first, then one
  // per learned attack skill. The fight picks between them each swing. Learned
  // passives (crit rate and the like) apply to whichever attack is chosen, so
  // the already-resolved `derived` is handed to each option.
  const Character& proto = state.character.proto();
  params.attacks.push_back(
      AttackFor(state, proto, nullptr, 0, params.types, derived));
  for (const std::pair<const std::string, Skill>& entry : state.skills) {
    if (entry.second.kind() != SKILL_KIND_ATTACK) {
      continue;
    }
    int learned = state.character.skill_level(entry.second);
    if (learned > 0) {
      params.attacks.push_back(AttackFor(state, proto, &entry.second, learned,
                                         params.types, derived));
    }
  }
  params.active = true;
  return params;
}

}  // namespace ms
