#include "analysis/sim_world.h"

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "src/embedded_data.h"
#include "src/game_state.h"
#include "src/map_level.h"
#include "src/proto_loader.h"

namespace ms {

Catalogs LoadCatalogs() {
  Catalogs c;
  c.equips = LoadTextProtoMap<EquipPrototype>(EmbeddedEquips());
  c.scrolls = LoadTextProtoMap<Scroll>(EmbeddedScrolls());
  c.items = LoadTextProtoMap<ItemPrototype>(EmbeddedItems());
  c.mobs = LoadTextProtoMap<Mob>(EmbeddedMobs());
  c.maps = LoadTextProtoMap<MapData>(EmbeddedMaps());
  c.skills = LoadTextProtoMap<Skill>(EmbeddedSkills());
  c.bosses = LoadTextProtoMap<Boss>(EmbeddedBosses());
  c.sets = LoadTextProtoMap<EquipSet>(EmbeddedSets());
  return c;
}

GameState NewState(const Catalogs& catalogs, unsigned int seed) {
  return GameState(catalogs.equips, catalogs.scrolls, catalogs.items,
                   catalogs.mobs, catalogs.maps, catalogs.skills,
                   GameMode::kPlay, TestOptions{}, seed, catalogs.sets);
}

GameState NewMaxState(const Catalogs& catalogs, JobAdvancement advancement,
                      int level, unsigned int seed) {
  TestOptions options;
  options.job = advancement;
  options.level = level;
  return GameState(catalogs.equips, catalogs.scrolls, catalogs.items,
                   catalogs.mobs, catalogs.maps, catalogs.skills,
                   GameMode::kMax, options, seed, catalogs.sets);
}

std::vector<std::string> HuntingGrounds(const Catalogs& catalogs) {
  std::vector<std::pair<double, std::string>> sorted;
  for (const std::pair<const std::string, MapData>& entry : catalogs.maps) {
    if (entry.second.spawns().empty()) {
      continue;  // a town, with nothing on it to kill or be killed by
    }
    sorted.push_back({MapLevel(catalogs.mobs, entry.second), entry.first});
  }
  std::sort(sorted.begin(), sorted.end());
  std::vector<std::string> maps;
  for (const std::pair<double, std::string>& entry : sorted) {
    maps.push_back(entry.second);
  }
  return maps;
}

}  // namespace ms
