/* When a cut on a monster's damage taken lifts (Spawn.guard), as the boss
 * rules tell the fight so a burst can wait for it.
 */
#ifndef MS_SRC_COMBAT_GUARD_TIMES_H_
#define MS_SRC_COMBAT_GUARD_TIMES_H_

#include <limits>

namespace ms {

// Open for `open_seconds` from each opening, `interval_seconds` apart. An
// opening its HP brings early can't be seen coming.
struct GuardTimes {
  // The damage taken's factor while shut; 1 for no guard.
  double cut = 1.0;
  // Seconds it stays open from now; 0 while shut.
  double open_left = 0.0;
  // Seconds to the next opening after any open now.
  double opens_in = std::numeric_limits<double>::infinity();
  double open_seconds = 0.0;
  double interval_seconds = 0.0;
};

}  // namespace ms

#endif  // MS_SRC_COMBAT_GUARD_TIMES_H_
