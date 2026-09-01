/* Where a sim's checkpoints live, and the stamp that keeps them honest.
 *
 * A checkpoint is a shortcut, and a shortcut that outlives what it was cut
 * through is worse than no shortcut at all: a run that resumes a climb taken
 * before the change being measured answers the wrong question and says
 * nothing about it. So a file is only ever read back by the binary that wrote
 * it. Every textproto the game ships is compiled INTO that binary, so its own
 * identity covers the data as well as the code -- change a mob's HP and the
 * relink invalidates every checkpoint there is.
 *
 * They are kept under the system temp directory rather than in the tree, and
 * the whole directory is emptied the moment its stamp stops matching, so a
 * stale one is never left lying around to be picked up by hand.
 */
#ifndef MS_ANALYSIS_CHECKPOINT_H_
#define MS_ANALYSIS_CHECKPOINT_H_

#include <string>

#include "analysis/sim_checkpoint.pb.h"

namespace ms {

// What the running binary is, as a string a file can be compared against.
// Empty if it cannot be told, which turns checkpointing off rather than
// guessing.
std::string CheckpointStamp();

// The directory `sim`'s checkpoints belong in, made if it is not there and
// emptied if what is in it was written by another build. Returns the path, or
// empty if it cannot be had -- in which case the run simply climbs.
std::string PrepareCheckpointDir(const std::string& sim,
                                 const std::string& stamp);

// Reads the checkpoint `dir` holds for `key`, or returns false for one that is
// not there, will not parse, or was not written by this build.
bool ReadCheckpoint(const std::string& dir, const std::string& key,
                    const std::string& stamp, SimCheckpoint* out);

// Writes `saved` as `key`. Failing to write is not fatal: the run has the
// answer either way, and only the next one loses anything.
void WriteCheckpoint(const std::string& dir, const std::string& key,
                     const SimCheckpoint& saved);

}  // namespace ms

#endif  // MS_ANALYSIS_CHECKPOINT_H_
