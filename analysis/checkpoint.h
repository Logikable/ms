/* Where a sim's checkpoints live, and the stamp that keeps them valid.
 *
 * A checkpoint from before the change being measured would silently answer the
 * wrong question. So a file is only read back by the binary that wrote it.
 * Every shipped textproto is compiled into that binary, so its identity covers
 * the data as well as the code: changing a mob's HP invalidates every
 * checkpoint.
 *
 * Checkpoints live under the system temp directory, not the tree. The whole
 * directory is emptied as soon as its stamp stops matching, so a stale file is
 * never left around to be picked up by hand.
 */
#ifndef MS_ANALYSIS_CHECKPOINT_H_
#define MS_ANALYSIS_CHECKPOINT_H_

#include <string>

#include "analysis/sim_checkpoint.pb.h"

namespace ms {

// Identifies the running binary as a string a file can be compared against.
// Empty if it can't be determined, which turns checkpointing off.
std::string CheckpointStamp();

// Returns the directory for `sim`'s checkpoints, creating it if missing and
// emptying it if another build wrote its contents. Returns empty if the
// directory can't be made, in which case the run climbs from scratch.
std::string PrepareCheckpointDir(const std::string& sim,
                                 const std::string& stamp);

// Reads the checkpoint in `dir` for `key`. Returns false if it's missing, won't
// parse, or was written by another build.
bool ReadCheckpoint(const std::string& dir, const std::string& key,
                    const std::string& stamp, SimCheckpoint* out);

// Writes `saved` as `key`. A failed write isn't fatal: this run has its answer
// either way, and only the next run loses anything.
void WriteCheckpoint(const std::string& dir, const std::string& key,
                     const SimCheckpoint& saved);

}  // namespace ms

#endif  // MS_ANALYSIS_CHECKPOINT_H_
