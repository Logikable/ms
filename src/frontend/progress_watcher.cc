#include "src/frontend/progress_watcher.h"

#include "src/character/character.h"
#include "src/protos/character.pb.h"

namespace ms {

ProgressWatcher::ProgressWatcher(const Character& character)
    : last_level_(character.level()),
      last_job_(character.job()),
      last_stage_(character.job_stage()) {
}

Progress ProgressWatcher::Notice(const Character& character) {
  Progress progress;
  // The advancement is checked first and wins: it is the bigger news, and
  // reaching the level that offers one doesn't take it, so the two can't happen
  // at the same moment. The stage is checked, not the job, because the 5th
  // advancement leaves a Night Lord a Night Lord.
  if (character.job_stage() != last_stage_) {
    progress.kind = kJobAdvanced;
    progress.from_job = last_job_;
    progress.to_job = character.job();
    progress.to_stage = character.job_stage();
    last_job_ = character.job();
    last_stage_ = character.job_stage();
    last_level_ = character.level();
    return progress;
  }
  if (character.level() > last_level_) {
    LevelGains gains = GainsForLevels(last_level_, character.level());
    progress.kind = kLevelGained;
    progress.from_level = last_level_;
    progress.to_level = character.level();
    progress.ap = gains.ap;
    progress.sp = character.job() == JOB_BEGINNER ? 0 : gains.sp;
    progress.hyper_sp = gains.hyper_sp;
  }
  // Assigned, not just raised, so if the level ever went down the next real
  // level-up doesn't report a climb that didn't happen.
  last_level_ = character.level();
  return progress;
}

}  // namespace ms
