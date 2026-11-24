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

// How many monsters' HP a spawn adds to its phase: none for one sharing
// another's HP (Spawn.shares_hp), whose pool is counted once.
inline int HpCount(const Spawn& spawn) {
  return spawn.shares_hp().empty() ? SpawnCount(spawn) : 0;
}

}  // namespace ms

#endif  // MS_SRC_SPAWN_H_
