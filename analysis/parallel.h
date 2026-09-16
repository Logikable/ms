/* Spreading a sim's outer loop over the machine's cores.
 *
 * A sim's outer loop is nearly always a grid of independent measurements: a
 * branch's whole climb, or one map fought at one level. Each builds its own
 * character and fights its own fight, sharing nothing but the read-only
 * catalogs -- so they can simply run at once.
 */
#ifndef MS_ANALYSIS_PARALLEL_H_
#define MS_ANALYSIS_PARALLEL_H_

#include <functional>

namespace ms {

// Runs body(i) for every i in [0, count) on as many threads as there are cores,
// returning once all have finished. NO LOCK: each body is expected to write to
// its own slot of storage the caller owns. PRINT AFTER THIS RETURNS -- bodies
// do not finish in the order they started, and a sim's table has to read the
// same way every run.
void ParallelFor(int count, const std::function<void(int)>& body);

}  // namespace ms

#endif  // MS_ANALYSIS_PARALLEL_H_
