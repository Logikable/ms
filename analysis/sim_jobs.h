/* The jobs a sim sweeps, and the climb that brings one of them to a level.
 *
 * Every sim here asks its question of a finished character, and every one of
 * them got there the same way: down a branch's own advancement path, spending
 * each level's AP on the job's stat and each level's SP on whatever the book
 * will sell. That climb lives here rather than in four sims that drift.
 *
 * No list of jobs appears in this file. A branch is a job that stands at the
 * end of its own path, which is a question the game's stage table already
 * answers -- so a branch written tomorrow is swept the day it exists.
 */
#ifndef MS_ANALYSIS_SIM_JOBS_H_
#define MS_ANALYSIS_SIM_JOBS_H_

#include <string>
#include <vector>

#include "src/game_state.h"
#include "src/protos/character.pb.h"

namespace ms {

// What a branch is called on a table. Spelled out here rather than borrowed
// from the frontend's JobName, which a combat tool has no business reaching
// into.
std::string BranchName(Job job);

// The advancements a branch is reached through, in order, so a sweep climbs
// the same path a player does and collects each book's skills on the way.
// Empty for a job that takes no advancement of its own.
std::vector<Job> PathTo(Job branch);

// How many advancements deep a branch is: 1 for a Swordman, 4 for a Hero.
// 0 for anything that is not a branch.
int StageOf(Job branch);

// Every branch the game defines, in the enum's own order.
std::vector<Job> EveryBranch();

// The branches a character at `level` could be standing in: those of the
// deepest stage the level has reached, since nobody at 130 is still a
// Crusader. A level past every branch the game ships takes the deepest that
// has any -- level 200 measures the 4th jobs, there being no 5th.
std::vector<Job> BranchesAt(int level);

// The branch `name` names, as --job spells it ("dark_knight"). Dies on
// anything else rather than sweeping the wrong character quietly. `min_stage`
// rejects a branch too shallow for the sweep asking.
Job ParseBranch(const std::string& name, int min_stage = 1);

// Brings the character up to `level` the way a player gets there: each
// advancement of `path` as it is offered, every AP on the primary stat, every
// SP on whatever it will buy. Which skill goes first is the catalog's
// arbitrary order, but a book costs exactly what its levels pay out, so the
// end of a stage looks the same either way.
//
// `spend_sp` false leaves every point in the pool, for a caller that means to
// place them itself. That matters from the 4th job up, where the book costs
// more than the levels below the cap pay out and which points get spent is
// most of what the character is.
void GrowTo(GameState& state, int level, const std::vector<Job>& path,
            bool spend_sp = true);

}  // namespace ms

#endif  // MS_ANALYSIS_SIM_JOBS_H_
