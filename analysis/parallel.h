/* Runs a sim's loops across all CPU cores: the outer one, where each iteration
 * builds its own character and shares only the read-only catalogs, and inner
 * ones that read a character without changing it.
 */
#ifndef MS_ANALYSIS_PARALLEL_H_
#define MS_ANALYSIS_PARALLEL_H_

#include <functional>

namespace ms {

// Runs body(i) for every i in [0, count) on the calling thread and any idle
// worker of a pool of one per core, and returns once all have finished. A call
// from inside a body is fine: it gets whatever cores the outer call leaves.
// There is no lock, so each body must write only to its own slot of
// caller-owned storage. Print after this returns: bodies finish out of order.
void ParallelFor(int count, const std::function<void(int)>& body);

}  // namespace ms

#endif  // MS_ANALYSIS_PARALLEL_H_
