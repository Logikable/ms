/* Runs a sim's outer loop across all CPU cores. Each iteration builds its own
 * character and shares only the read-only catalogs.
 */
#ifndef MS_ANALYSIS_PARALLEL_H_
#define MS_ANALYSIS_PARALLEL_H_

#include <functional>

namespace ms {

// Runs body(i) for every i in [0, count) on one thread per core and returns
// once all have finished. There is no lock, so each body must write only to its
// own slot of caller-owned storage. Print after this returns: bodies finish out
// of order.
void ParallelFor(int count, const std::function<void(int)>& body);

}  // namespace ms

#endif  // MS_ANALYSIS_PARALLEL_H_
