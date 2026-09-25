/* How far along the game a map is meant for, as one number from its spawn list.
 *
 * Used by the map list the player picks from and by the sims that sort maps
 * into the order a player meets them, so it lives here instead of in either.
 */
#ifndef MS_SRC_MAP_LEVEL_H_
#define MS_SRC_MAP_LEVEL_H_

#include <map>
#include <string>

#include "src/protos/map.pb.h"
#include "src/protos/mob.pb.h"

namespace ms {

// Mean level of what `map` spawns, weighted by how many of each. Weighting by
// count reflects where the player's time goes: a couple of stragglers shouldn't
// pull a map's level away from the crowd that fills it.
//
// 0 for a town, and for a map whose spawns name no mob in the catalog.
double MapLevel(const std::map<std::string, Mob>& mobs, const MapData& map);

}  // namespace ms

#endif  // MS_SRC_MAP_LEVEL_H_
