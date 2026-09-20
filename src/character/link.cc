#include "src/character/link.h"

#include <algorithm>

#include "src/character/job_branch.h"
#include "src/protos/character.pb.h"

namespace ms {

int LinkRungsFor(int level) {
  int rungs = 0;
  for (int rung : kLinkRungLevels) {
    if (level >= rung) {
      ++rungs;
    }
  }
  return rungs;
}

void LinkTally::Record(Job job, int level) {
  Job line = LineOf(job);
  if (line == JOB_UNSPECIFIED) {
    return;
  }
  int& best = best_[line];
  best = std::max(best, level);
}

int LinkTally::LevelFor(Job line) const {
  JobBranch branch = BranchOf(line);
  if (branch == JobBranch::kNone || branch == JobBranch::kBeginner) {
    return 0;
  }
  int level = 0;
  for (const std::pair<const Job, int>& entry : best_) {
    if (BranchOf(entry.first) == branch) {
      level += LinkRungsFor(entry.second);
    }
  }
  return level;
}

LinkTally LinkTally::With(Job job, int level) const {
  LinkTally tally = *this;
  tally.Record(job, level);
  return tally;
}

}  // namespace ms
