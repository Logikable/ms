/* Which of the four lines a job belongs to.
 *
 * Level-up gains, equip category, beginner book, primary stat, mastery floor
 * and the stat the swing uses all depend on the branch. Each of those used to
 * list every job separately, so adding a job meant editing seven switch
 * statements and hoping none was missed.
 *
 * A separate library from character, for the same reason as job_name.h: combat
 * needs the answer too, without depending on a whole character.
 */
#ifndef MS_SRC_CHARACTER_JOB_BRANCH_H_
#define MS_SRC_CHARACTER_JOB_BRANCH_H_

#include "src/protos/character.pb.h"

namespace ms {

// The four lines, plus the beginner every character starts as; kNone is only
// for JOB_UNSPECIFIED. The beginner is its own branch because callers disagree
// about it: beginners swing on STR like a warrior, level like no other branch,
// and wear gear only a beginner can.
enum class JobBranch { kNone, kBeginner, kWarrior, kMagician, kArcher, kRogue };

// The branch `job` belongs to, at any stage of its line.
JobBranch BranchOf(Job job);

// The line within that branch, named by its 2nd job: JOB_FIGHTER for a Fighter,
// a Crusader and a Hero alike. The 3rd and 4th advancements continue a line
// instead of branching, so the 2nd job determines the rest.
//
// JOB_UNSPECIFIED for a character who hasn't taken a 2nd advancement yet. Link
// skills are counted per line; see //src/character:link.
Job LineOf(Job job);

}  // namespace ms

#endif  // MS_SRC_CHARACTER_JOB_BRANCH_H_
