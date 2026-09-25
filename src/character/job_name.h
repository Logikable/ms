/* The names jobs are shown with.
 *
 * A separate library from character: names are needed wherever a job is shown,
 * without depending on a whole character.
 */
#ifndef MS_SRC_CHARACTER_JOB_NAME_H_
#define MS_SRC_CHARACTER_JOB_NAME_H_

#include <string>

#include "src/protos/character.pb.h"

namespace ms {

// The display name for a job (e.g. "Swordman"), or "Unknown" for a job with no
// name yet. This is the full name: use it where the job is being chosen or
// confirmed, and ShortJobName everywhere else.
std::string JobName(Job job);

// The name shortened where the full one is too wide for a column, e.g. "I/L
// Wizard" for Ice/Lightning Wizard. This is the default way to show a job; only
// the advancement picker and its dialog spell the name out.
std::string ShortJobName(Job job);

// The name of the advancement into `job` at `stage`: the job's name, except
// that the 5th adds " V". It doesn't change the job, and "Advance to Night
// Lord?" asked of a Night Lord would say nothing.
std::string AdvancementName(Job job, int stage);

// The same, shortened the way ShortJobName shortens a job.
std::string ShortAdvancementName(Job job, int stage);

}  // namespace ms

#endif  // MS_SRC_CHARACTER_JOB_NAME_H_
