#include "src/farming.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "src/character.h"
#include "src/combat.h"
#include "src/equip_instance.h"
#include "src/game_state.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/map.pb.h"
#include "src/protos/mob.pb.h"

namespace ms {

void AdvanceFarming(GameState& state, double elapsed_seconds) {
  std::map<std::string, MapData>::const_iterator map_it =
      state.maps.find(state.current_map);
  if (map_it == state.maps.end()) {
    return;  // No map selected, or it isn't loaded.
  }
  const MapData& map = map_it->second;
  CharacterInstance& character = state.character;

  // Farming needs a weapon; skip when none is equipped.
  const std::map<EquipSlot, EquipInstance>& equipped = character.equipped();
  std::map<EquipSlot, EquipInstance>::const_iterator weapon_it =
      equipped.find(EQUIP_SLOT_PRIMARY_WEAPON);
  if (weapon_it == equipped.end()) {
    return;
  }
  const EquipPrototype& weapon = weapon_it->second.prototype();

  // Resolve the map's mob names to loaded protos, keeping the names as keys.
  std::vector<const Mob*> map_mobs;
  std::vector<std::string> mob_keys;
  for (const std::string& mob_name : map.mobs()) {
    std::map<std::string, Mob>::const_iterator mob_it =
        state.mobs.find(mob_name);
    if (mob_it != state.mobs.end()) {
      map_mobs.push_back(&mob_it->second);
      mob_keys.push_back(mob_name);
    }
  }

  const Character& proto = character.proto();
  OffenseStats offense = OffenseStatsFor(proto.job(), proto.allocated_stats(),
                                         character.equip_stats());
  std::vector<double> periods =
      MapKillPeriods(offense, weapon, map_mobs, map.spawn_count());

  int player_level = proto.level();
  int64_t exp_gained = 0;
  for (std::size_t i = 0; i < map_mobs.size(); ++i) {
    int64_t kills = FlushKills(periods[i], elapsed_seconds,
                               &state.kill_progress[mob_keys[i]]);
    exp_gained += kills * map_mobs[i]->exp();
    // Meso pools across every mob type into one balance, so all kills bank into
    // the shared meso_progress accumulator.
    int64_t meso = FlushDrops(ExpectedMesoPerKill(*map_mobs[i], player_level),
                              kills, &state.meso_progress);
    if (meso > 0) {
      character.AddMeso(meso);
    }
    for (const MobDrop& drop : map_mobs[i]->drops()) {
      int64_t dropped =
          FlushDrops(drop.per_kill(), kills, &state.drop_progress[drop.item()]);
      if (dropped <= 0) {
        continue;
      }
      std::map<std::string, ItemPrototype>::const_iterator item_it =
          state.items.find(drop.item());
      if (item_it == state.items.end()) {
        continue;  // Drop references an unloaded item; skip it.
      }
      character.AddStackable(item_it->second, static_cast<int>(dropped));
    }
  }
  if (exp_gained > 0) {
    character.AddExp(exp_gained);
  }
}

}  // namespace ms
