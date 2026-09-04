/* What a job is called on screen.
 *
 * Its own library rather than part of character: the names are needed
 * wherever a job is shown, and nothing about them wants the whole of a
 * character to come along.
 */
#ifndef MS_SRC_CHARACTER_JOB_NAME_H_
#define MS_SRC_CHARACTER_JOB_NAME_H_

#include <string>

#include "src/protos/character.pb.h"

namespace ms {

// The display name for a job (e.g. "Swordman"), or "Unknown" for a job not yet
// given a name. The full name: use it where the job is being chosen or
// confirmed, and ShortJobName everywhere else.
std::string JobName(Job job);

// The same name shortened where the full one is too wide to live in a column
// -- "I/L Wizard" for the Ice/Lightning Wizard. Every other job is already as
// short as it gets and answers its own name.
//
// This is the default for showing a job. The two places that spell it out in
// full are the advancement picker and the dialog confirming the choice: what
// the player is picking between deserves its whole name, once.
std::string ShortJobName(Job job);

// What the advancement into `job` at `stage` is called. Every stage but the
// 5th is the job's own name; the 5th takes a " V", because it is the one that
// does not change the job, and "Advance to Night Lord?" asked of a Night Lord
// says nothing.
std::string AdvancementName(Job job, int stage);

// The same, shortened the way ShortJobName shortens a job.
std::string ShortAdvancementName(Job job, int stage);

}  // namespace ms

#endif  // MS_SRC_CHARACTER_JOB_NAME_H_
