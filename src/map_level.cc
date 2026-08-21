#include "src/map_level.h"

#include <map>
#include <string>

#include "src/protos/map.pb.h"
#include "src/protos/mob.pb.h"

namespace ms {

double MapLevel(const std::map<std::string, Mob>& mobs, const MapData& map) {
  double levels = 0.0;
  double spawned = 0.0;
  for (const Spawn& spawn : map.spawns()) {
    std::map<std::string, Mob>::const_iterator it = mobs.find(spawn.mob());
    if (it == mobs.end()) {
      continue;
    }
    levels += it->second.level() * spawn.count();
    spawned += spawn.count();
  }
  return spawned == 0.0 ? 0.0 : levels / spawned;
}

}  // namespace ms
