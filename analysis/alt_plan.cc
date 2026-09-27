#include "analysis/alt_plan.h"

#include "src/character/job_branch.h"

namespace ms {

std::vector<AltStep> AltSteps(const AltLadders& ladders, const AltLevels& alts,
                              Job main_job) {
  const Job own = LineOf(main_job);
  std::map<JobBranch, AltStep> cheapest;
  for (const std::pair<const Job, AltLadder>& entry : ladders) {
    const Job line = entry.first;
    if (line == own) {
      continue;
    }
    AltLevels::const_iterator it = alts.find(line);
    const int held = LinkRungsFor(it == alts.end() ? 0 : it->second);
    if (held >= kLinkRungsPerLine || entry.second.seconds[held] < 0.0) {
      continue;
    }
    AltStep step;
    step.line = line;
    step.level = kLinkRungLevels[held];
    step.seconds = entry.second.seconds[held];
    step.meso = entry.second.meso[held];
    if (held > 0) {
      step.seconds -= entry.second.seconds[held - 1];
      step.meso -= entry.second.meso[held - 1];
    }
    const JobBranch branch = BranchOf(line);
    std::map<JobBranch, AltStep>::iterator best = cheapest.find(branch);
    if (best == cheapest.end() || step.seconds < best->second.seconds) {
      cheapest[branch] = step;
    }
  }
  std::vector<AltStep> steps;
  for (const std::pair<const JobBranch, AltStep>& entry : cheapest) {
    steps.push_back(entry.second);
  }
  return steps;
}

LinkTally AltTally(const AltLevels& alts) {
  LinkTally tally;
  for (const std::pair<const Job, int>& entry : alts) {
    tally.Record(entry.first, entry.second);
  }
  return tally;
}

}  // namespace ms
