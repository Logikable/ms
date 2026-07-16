#include "src/combat/encounter.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "src/character.h"
#include "src/combat/constants.h"
#include "src/combat/damage.h"
#include "src/equip_instance.h"
#include "src/game_state.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/map.pb.h"
#include "src/protos/mob.pb.h"

namespace ms {

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

  const Character& proto = state.character.proto();
  OffenseStats offense =
      OffenseStatsFor(proto.job(), proto.level(), proto.allocated_stats(),
                      state.character.equip_stats());
  params.swing_seconds =
      SwingIntervalSeconds(BaseAttackDelayMs(weapon.equip_type()),
                           weapon.attack_speed()) *
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
    type.damage_per_hit = ExpectedAttackDamage(offense, mob_it->second);
    type.simultaneous = spawn.count();
    params.types.push_back(std::move(type));
  }
  if (params.types.empty()) {
    return params;
  }
  params.active = true;
  return params;
}

}  // namespace ms
