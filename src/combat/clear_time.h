/* How long a character takes to clear a map, measured by fighting it with the
 * map refilling the moment it empties. On its timer the map would hide a slow
 * clear behind the wait for the next respawn.
 */
#ifndef MS_SRC_COMBAT_CLEAR_TIME_H_
#define MS_SRC_COMBAT_CLEAR_TIME_H_

#include <vector>

#include "src/combat/encounter.h"

namespace ms {

// The game's own repaint step, so a measured clear matches a watched one.
inline constexpr double kClearStepSeconds = 0.15;

// Each clear's length over `seconds` of fighting on `params`, in the stretched
// seconds every CombatParams duration uses. A clear still running at the end
// counts only if it is already the longest. Never clearing, or dying, gives
// one clear of `seconds`. Empty if `params` isn't active.
std::vector<double> ClearTimes(CombatParams params, double seconds,
                               double step = kClearStepSeconds);

// The clear time that a `fraction` of `times` take at most, by nearest rank:
// 0.5 is the median and 0.9 the 90th percentile. 0 for no times.
double ClearPercentile(std::vector<double> times, double fraction);

}  // namespace ms

#endif  // MS_SRC_COMBAT_CLEAR_TIME_H_
