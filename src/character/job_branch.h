/* Which of the four lines a job belongs to.
 *
 * A job's branch is what the level-up gains, the equip category, the beginner
 * book, the primary stat, the mastery floor and the stat the swing reads all
 * key off. Each of those used to spell the whole roster out again, so a new
 * job meant editing seven case ladders and hoping none was missed.
 *
 * Its own library rather than part of character, for the same reason
 * job_name.h is: combat asks the question too, and nothing about the answer
 * wants a whole character to come along.
 */
#ifndef MS_SRC_CHARACTER_JOB_BRANCH_H_
#define MS_SRC_CHARACTER_JOB_BRANCH_H_

#include "src/protos/character.pb.h"

namespace ms {

// The four lines, plus the beginner every character starts as; kNone is only
// JOB_UNSPECIFIED. The beginner is its own branch because the callers disagree
// about it: they swing on STR like a warrior, level like nobody, and wear what
// only a beginner can.
enum class JobBranch { kNone, kBeginner, kWarrior, kMagician, kArcher, kRogue };

// The branch `job` belongs to, however far along its line it is.
JobBranch BranchOf(Job job);

// The LINE within that branch, named by the second job it starts at:
// JOB_FIGHTER for a Fighter, a Crusader and a Hero alike. The 3rd and 4th
// advancements narrow rather than fork, so the second job names the whole of
// the rest of the climb.
//
// JOB_UNSPECIFIED for a character who has not taken a second advancement,
// theirs being undecided. Link skills are counted per line -- see
// //src/character:link.
Job LineOf(Job job);

}  // namespace ms

#endif  // MS_SRC_CHARACTER_JOB_BRANCH_H_
