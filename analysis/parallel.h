/* Runs a sim's outer loop across all CPU cores.
 *
 * A sim's outer loop is usually a grid of independent measurements, such as one
 * branch's whole climb or one map fought at one level. Each builds its own
 * character and shares only the read-only catalogs, so they can run at once.
 */
#ifndef MS_ANALYSIS_PARALLEL_H_
#define MS_ANALYSIS_PARALLEL_H_

#include <functional>

namespace ms {

// Runs body(i) for every i in [0, count) on one thread per core and returns
// once all have finished. There is no lock, so each body must write only to its
// own slot of caller-owned storage. Print after this returns: bodies finish out
// of order, and a sim's table must read the same every run.
void ParallelFor(int count, const std::function<void(int)>& body);

}  // namespace ms

#endif  // MS_ANALYSIS_PARALLEL_H_
