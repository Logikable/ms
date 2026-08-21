/* How far along a map is meant for: one number off its spawn list.
 *
 * Read by the map list the player picks from and by the sims that sort maps
 * into the order a player meets them, which is why it is here rather than
 * inside either.
 */
#ifndef MS_SRC_MAP_LEVEL_H_
#define MS_SRC_MAP_LEVEL_H_

#include <map>
#include <string>

#include "src/protos/map.pb.h"
#include "src/protos/mob.pb.h"

namespace ms {

// Mean level of what `map` spawns, weighted by how many of each. Weighting by
// count puts the number where the player's time actually goes: a couple of
// stragglers should not pull a map up away from the crowd that fills it.
//
// 0 for a town, and for a map whose spawns name no mob the catalog defines.
double MapLevel(const std::map<std::string, Mob>& mobs, const MapData& map);

}  // namespace ms

#endif  // MS_SRC_MAP_LEVEL_H_
