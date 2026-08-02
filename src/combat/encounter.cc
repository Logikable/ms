#include "src/combat/encounter.h"

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "src/character/character.h"
#include "src/character/character_stats.h"
#include "src/character/progression.h"
#include "src/combat/constants.h"
#include "src/combat/damage.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/map.pb.h"
#include "src/protos/mob.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {
namespace {

// One attack's damage against every mob type on the map. `skill` is null for
// the bare poke, which hits one target for the character's plain 100% swing.
// `equipped` is everything the character wears plus everything their passives
// grant, already summed -- the two are indistinguishable to the damage chain.
AttackOption AttackFor(const Character& proto, const EquipStats& equipped,
                       const Skill* skill, int level,
                       const std::vector<CombatType>& types,
                       const DerivedStats& derived) {
  AttackOption attack;
  if (skill != nullptr) {
    attack.name = skill->name();
    attack.max_enemies = std::max(1, skill->max_enemies());
  }
  OffenseStats offense =
      OffenseStatsFor(proto.job(), proto.level(), proto.allocated_stats(),
                      equipped, skill, level, derived.crit_rate);
  for (const CombatType& type : types) {
    attack.damage_per_hit.push_back(ExpectedAttackDamage(offense, *type.mob));
  }
  return attack;
}

// Whether `skill` can be swung with `weapon`. A skill that names no weapon
// type is swingable with anything, which is the usual case.
bool SwingableWith(const Skill& skill, EquipType weapon) {
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
  // The pace the whole encounter runs at, and the only thing here that asks
  // the character's level directly: the game stretches out as they climb.
  double speed_factor = GameSpeedFactor(state.character.proto().level());
  params.swing_seconds =
      SwingIntervalSeconds(BaseAttackDelayMs(weapon.equip_type()),
                           attack_speed) *
      speed_factor;
  params.respawn_seconds = kRespawnIntervalSeconds * speed_factor;
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
  const EquipStats total_stats = TotalEquipStats(state.character, derived);
  params.attacks.push_back(
      AttackFor(proto, total_stats, nullptr, 0, params.types, derived));
  for (const std::pair<const std::string, Skill>& entry : state.skills) {
    if (entry.second.kind() != SKILL_KIND_ATTACK) {
      continue;
    }
    // A skill the weapon in hand cannot swing is not an option, however well
    // learned. The bare poke always is, so the character is never left with
    // nothing to attack with.
    if (!SwingableWith(entry.second, weapon.equip_type())) {
      continue;
    }
    int learned = state.character.skill_level(entry.second);
    if (learned > 0) {
      params.attacks.push_back(AttackFor(proto, total_stats, &entry.second,
                                         learned, params.types, derived));
    }
  }
  params.active = true;
  return params;
}

}  // namespace ms
