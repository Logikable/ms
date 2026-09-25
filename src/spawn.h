/* How many monsters a spawn creates.
 *
 * A map's spawn gives a count; a boss phase's lists a spot per monster and uses
 * the number of spots as the count, so a phase can't state two numbers that
 * disagree. Everything that needs the count gets it here.
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
