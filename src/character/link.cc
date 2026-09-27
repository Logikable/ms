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

const LinkPreset& PresetOf(const LinkSkills& link, StatPreset slot) {
  if (IndexOf(slot) >= link.presets_size()) {
    return LinkPreset::default_instance();
  }
  return link.presets(IndexOf(slot));
}

LinkPreset& PresetOf(LinkSkills& link, StatPreset slot) {
  while (link.presets_size() < kNumStatPresets) {
    link.add_presets();
  }
  return *link.mutable_presets(IndexOf(slot));
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
  return LevelFor(line, JOB_UNSPECIFIED, 0);
}

int LinkTally::LevelFor(Job line, Job also, int also_level) const {
  JobBranch branch = BranchOf(line);
  if (branch == JobBranch::kNone || branch == JobBranch::kBeginner) {
    return 0;
  }
  const Job also_line = LineOf(also);
  bool counted = false;
  int level = 0;
  for (const std::pair<const Job, int>& entry : best_) {
    if (BranchOf(entry.first) != branch) {
      continue;
    }
    int best = entry.second;
    if (entry.first == also_line) {
      best = std::max(best, also_level);
      counted = true;
    }
    level += LinkRungsFor(best);
  }
  if (!counted && also_line != JOB_UNSPECIFIED &&
      BranchOf(also_line) == branch) {
    level += LinkRungsFor(also_level);
  }
  return level;
}

}  // namespace ms
