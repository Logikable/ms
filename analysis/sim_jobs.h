/* The jobs a sim sweeps, and the climb that brings one to a level: along the
 * branch's own advancement path, AP on the job's stat, SP on whatever the book
 * sells. A branch is a job at the end of its own path, so a new one is swept as
 * soon as it exists.
 */
#ifndef MS_ANALYSIS_SIM_JOBS_H_
#define MS_ANALYSIS_SIM_JOBS_H_

#include <string>
#include <vector>

#include "src/game_state.h"
#include "src/protos/character.pb.h"

namespace ms {

// Name of a branch for a table. Spelled out here rather than borrowed from the
// frontend's JobName, which a combat tool shouldn't depend on.
std::string BranchName(Job job);

// Branch name as a column header of at most four characters, so ten fit side by
// side. Only the swept branches have abbreviations; others use the first four
// characters of BranchName.
std::string BranchAbbrev(Job job);

// The advancements leading to a branch, in order, so a sweep climbs the same
// path a player does and collects each book's skills. Empty for a job with no
// advancement of its own.
std::vector<Job> PathTo(Job branch);

// How many advancements deep a branch is: 1 for a Swordman, 4 for a Hero. 0 for
// anything that isn't a branch.
int StageOf(Job branch);

// Every branch the game defines, in enum order.
std::vector<Job> EveryBranch();

// The branches a character at `level` could be in: those at the deepest stage
// the level has reached. Level 200 measures the 4th jobs.
std::vector<Job> BranchesAt(int level);

// Parses a branch as --job spells it ("dark_knight"). Dies on anything else
// rather than quietly sweeping the wrong character. `min_stage` rejects a
// branch too shallow for the calling sweep.
Job ParseBranch(const std::string& name, int min_stage = 1);

// Levels the character to `level` the way a player does: each advancement as
// offered, every AP on the primary stat, and every SP on whatever it buys.
//
// With `spend_sp` false, points stay in the pool for a caller to place itself.
// That matters partway through a stage, where the pool can't buy the whole
// book.
void GrowTo(GameState& state, int level, const std::vector<Job>& path,
            bool spend_sp = true);

}  // namespace ms

#endif  // MS_ANALYSIS_SIM_JOBS_H_
