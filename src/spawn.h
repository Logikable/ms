/* How many monsters a spawn puts out.
 *
 * A map's spawn says a count; a boss phase's names a spot per monster and
 * lets the spots be the count, so no phase can write two numbers that
 * disagree. Everything that asks "how many" asks here.
 */
#ifndef MS_SRC_SPAWN_H_
#define MS_SRC_SPAWN_H_

#include "src/protos/mob.pb.h"

namespace ms {

inline int SpawnCount(const Spawn& spawn) {
  return spawn.spots_size() > 0 ? spawn.spots_size() : spawn.count();
}

}  // namespace ms

#endif  // MS_SRC_SPAWN_H_
