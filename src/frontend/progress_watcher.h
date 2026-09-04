/* Spotting a level or a job that has changed since the last look.
 *
 * Polled rather than pushed: the character has no way to call back, and
 * polling catches every route to a new level -- combat EXP, the debug
 * Level-Up item, an advancement -- with one piece of code.
 */
#ifndef MS_SRC_FRONTEND_PROGRESS_WATCHER_H_
#define MS_SRC_FRONTEND_PROGRESS_WATCHER_H_

#include "src/protos/character.pb.h"

namespace ms {

// What the last look turned up.
enum ProgressKind {
  kNothingNoticed,
  kLevelGained,
  kJobAdvanced,
};

// One change, with everything a celebration card needs to name it. Only the
// fields belonging to `kind` are filled.
struct Progress {
  ProgressKind kind = kNothingNoticed;
  int from_level = 0;
  int to_level = 0;
  // AP and SP the climb paid. SP is 0 for a Beginner, whose points are real
  // but unreachable -- the skills tab belongs to a job.
  int ap = 0;
  int sp = 0;
  int hyper_sp = 0;
  Job from_job = JOB_UNSPECIFIED;
  Job to_job = JOB_UNSPECIFIED;
  // The stage advanced into. Needed to name the advancement: the 5th leaves
  // the job where it was, so the two job fields alone read as no change.
  int to_stage = 0;
};

class ProgressWatcher {
 public:
  // Seeded from the character as loaded, so launching into a level 13
  // character is not itself a level-up.
  explicit ProgressWatcher(const Character& character);

  // What has changed since the previous call, and remembers where the
  // character now stands.
  Progress Notice(const Character& character);

 private:
  int last_level_;
  Job last_job_;
  int last_stage_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_PROGRESS_WATCHER_H_
