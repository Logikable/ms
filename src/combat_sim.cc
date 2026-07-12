#include "src/combat_sim.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "src/character.h"
#include "src/combat.h"
#include "src/equip_instance.h"
#include "src/game_state.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/map.pb.h"
#include "src/protos/mob.pb.h"

namespace ms {

CombatParams ComputeCombatParams(const GameState& state) {
  CombatParams params;
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

  std::vector<const Mob*> mobs;
  for (const std::string& mob_name : map.mobs()) {
    std::map<std::string, Mob>::const_iterator mob_it =
        state.mobs.find(mob_name);
    if (mob_it != state.mobs.end()) {
      mobs.push_back(&mob_it->second);
    }
  }
  if (mobs.empty()) {
    return params;
  }

  const Character& proto = state.character.proto();
  OffenseStats offense = OffenseStatsFor(proto.job(), proto.allocated_stats(),
                                         state.character.equip_stats());
  params.swing_seconds =
      SwingIntervalSeconds(BaseAttackDelayMs(weapon.equip_type()),
                           weapon.attack_speed()) *
      kGameSpeedFactor;
  params.respawn_seconds = kRespawnIntervalSeconds * kGameSpeedFactor;
  int per_type = map.spawn_count() / static_cast<int>(mobs.size());
  for (const Mob* mob : mobs) {
    CombatType type;
    type.mob = mob;
    type.damage_per_hit = ExpectedAttackDamage(offense, *mob);
    type.simultaneous = per_type;
    params.types.push_back(std::move(type));
  }
  params.active = true;
  return params;
}

}  // namespace ms
