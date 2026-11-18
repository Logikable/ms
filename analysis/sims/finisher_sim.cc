/* finisher_sim: every boss fight open at --level, fought by each branch's
 * --mode=max ceiling, with the seconds each clear took. For tuning how a fight
 * ends (CombatSim::Finisher): run it before and after a change and compare.
 *
 *   bazelisk run --config=sixth //analysis/sims:finisher_sim --
 *       --level=280 --hexa=30
 */
#include <cstdio>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "analysis/parallel.h"
#include "analysis/sim_boss.h"
#include "analysis/sim_jobs.h"
#include "analysis/sim_world.h"
#include "src/character/character.h"
#include "src/character/job_name.h"
#include "src/game_state.h"
#include "src/protos/boss.pb.h"

ABSL_FLAG(int, level, 260, "Character level of every branch's ceiling.");
ABSL_FLAG(int, hexa, 0,
          "Raise every HEXA node the ceiling reaches to this level first.");
ABSL_FLAG(std::string, job, "", "One branch (\"bow_master\"), or all.");
ABSL_FLAG(std::string, fight, "", "One boss key (\"zakum\"), or all.");

namespace ms {
namespace {

constexpr unsigned int kSeed = 1;

struct Row {
  Job branch = JOB_UNSPECIFIED;
  std::string fight;
  BossOutcome outcome;
};

int Run() {
  const Catalogs catalogs = LoadCatalogs();
  const int level = absl::GetFlag(FLAGS_level);
  std::vector<Job> branches = BranchesAt(level);
  if (!absl::GetFlag(FLAGS_job).empty()) {
    branches = {ParseBranch(absl::GetFlag(FLAGS_job))};
  }
  auto advancement = [](Job branch) {
    return AdvancementForJobStage(branch, StageOf(branch));
  };
  auto prepare = [&](GameState& state) {
    state.bosses = catalogs.bosses;
    if (absl::GetFlag(FLAGS_hexa) > 0) {
      LearnHexaTo(state, absl::GetFlag(FLAGS_hexa));
    }
  };
  std::vector<Row> rows;
  for (Job branch : branches) {
    GameState state = NewMaxState(catalogs, advancement(branch), level, kSeed);
    prepare(state);
    for (const std::pair<std::string, int>& fight :
         UnlockedBosses(state, level)) {
      if (!absl::GetFlag(FLAGS_fight).empty() &&
          fight.first != absl::GetFlag(FLAGS_fight)) {
        continue;
      }
      rows.push_back(
          {branch,
           fight.first + " " +
               state.bosses.at(fight.first).difficulties(fight.second).name(),
           {}});
    }
  }
  ParallelFor(static_cast<int>(rows.size()), [&](int i) {
    GameState state =
        NewMaxState(catalogs, advancement(rows[i].branch), level, kSeed);
    prepare(state);
    const std::string& name = rows[i].fight;
    const std::string key = name.substr(0, name.find(' '));
    const std::string difficulty = name.substr(name.find(' ') + 1);
    rows[i].outcome = FightBoss(
        state, key, BossDifficultyIndex(state.bosses.at(key), difficulty));
  });

  std::map<Job, std::pair<double, int>> totals;
  for (const Row& row : rows) {
    std::printf("%-26s %-32s %8.1fs %s\n", JobName(row.branch).c_str(),
                row.fight.c_str(), row.outcome.seconds,
                row.outcome.won ? "won" : "lost");
    if (row.outcome.won) {
      totals[row.branch].first += row.outcome.seconds;
      ++totals[row.branch].second;
    }
  }
  std::printf("\n");
  double all = 0.0;
  for (const auto& [branch, total] : totals) {
    std::printf("%-26s %3d won %10.1fs\n", JobName(branch).c_str(),
                total.second, total.first);
    all += total.first;
  }
  std::printf("%-26s %18.1fs\n", "All", all);
  return 0;
}

}  // namespace
}  // namespace ms

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  return ms::Run();
}
