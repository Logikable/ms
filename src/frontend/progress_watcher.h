/* Detects a level or job that has changed since the last check.
 *
 * It polls instead of being notified: the character has no way to call back,
 * and polling catches every way of gaining a level (combat EXP, an advancement)
 * in one place.
 */
#ifndef MS_SRC_FRONTEND_PROGRESS_WATCHER_H_
#define MS_SRC_FRONTEND_PROGRESS_WATCHER_H_

#include "src/protos/character.pb.h"

namespace ms {

// What the last check found.
enum ProgressKind {
  kNothingNoticed,
  kLevelGained,
  kJobAdvanced,
};

// One change, with everything a celebration card needs to describe it. Only the
// fields for `kind` are filled in.
struct Progress {
  ProgressKind kind = kNothingNoticed;
  int from_level = 0;
  int to_level = 0;
  // AP and SP the climb paid. SP is 0 for a Beginner, whose points exist but
  // can't be spent yet, since the skills tab needs a job.
  int ap = 0;
  int sp = 0;
  int hyper_sp = 0;
  Job from_job = JOB_UNSPECIFIED;
  Job to_job = JOB_UNSPECIFIED;
  // The stage advanced into. It is needed to name the advancement, because the
  // 5th doesn't change the job, so the two job fields alone look unchanged.
  int to_stage = 0;
};

class ProgressWatcher {
 public:
  // Starts from the character as loaded, so loading a level 13 character
  // doesn't count as a level-up.
  explicit ProgressWatcher(const Character& character);

  // Returns what changed since the previous call, and records the character's
  // current state.
  Progress Notice(const Character& character);

 private:
  int last_level_;
  Job last_job_;
  int last_stage_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_PROGRESS_WATCHER_H_
