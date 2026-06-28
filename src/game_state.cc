#include "src/game_state.h"

#include <cstddef>
#include <cstdint>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "src/combat.h"
#include "src/protos/character.pb.h"

namespace ms {

namespace {

Character MakeStartingCharacterProto() {
  Character proto;
  proto.set_level(2);
  proto.set_job(JOB_BEGINNER);
  proto.set_ap(5);
  proto.mutable_allocated_stats()->set_str(13);
  proto.mutable_allocated_stats()->set_dex(4);
  proto.mutable_allocated_stats()->set_int_(4);
  proto.mutable_allocated_stats()->set_luk(4);
  proto.mutable_allocated_stats()->set_hp(50);
  proto.mutable_allocated_stats()->set_mp(15);
  return proto;
}

}  // namespace

GameState::GameState(std::map<std::string, EquipPrototype> equips_arg,
                     std::map<std::string, Scroll> scrolls_arg,
                     std::map<std::string, ItemPrototype> items_arg,
                     std::map<std::string, Mob> mobs_arg,
                     std::map<std::string, MapData> maps_arg)
    : equips(std::move(equips_arg)),
      scrolls(std::move(scrolls_arg)),
      items(std::move(items_arg)),
      mobs(std::move(mobs_arg)),
      maps(std::move(maps_arg)),
      rng(std::random_device{}()),
      character(rng, MakeStartingCharacterProto()) {
}

void GameState::AdvanceFarming(double elapsed_seconds) {
  std::map<std::string, MapData>::const_iterator map_it =
      maps.find(current_map);
  if (map_it == maps.end()) {
    return;  // No map selected, or it isn't loaded.
  }
  const MapData& map = map_it->second;

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
    std::map<std::string, Mob>::const_iterator mob_it = mobs.find(mob_name);
    if (mob_it != mobs.end()) {
      map_mobs.push_back(&mob_it->second);
      mob_keys.push_back(mob_name);
    }
  }

  const Character& proto = character.proto();
  OffenseStats offense = OffenseStatsFor(proto.job(), proto.allocated_stats(),
                                         character.equip_stats());
  std::vector<double> periods =
      MapKillPeriods(offense, weapon, map_mobs, map.spawn_count());

  int64_t exp_gained = 0;
  for (std::size_t i = 0; i < map_mobs.size(); ++i) {
    int64_t kills =
        FlushKills(periods[i], elapsed_seconds, &kill_progress[mob_keys[i]]);
    exp_gained += kills * map_mobs[i]->exp();
    for (const MobDrop& drop : map_mobs[i]->drops()) {
      drop_counts[drop.item()] +=
          FlushDrops(drop.per_kill(), kills, &drop_progress[drop.item()]);
    }
  }
  if (exp_gained > 0) {
    character.AddExp(exp_gained);
  }
}

}  // namespace ms
