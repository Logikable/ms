/* liberation_sim: how each branch's max character fares in the Liberation.
 *
 * Seeds each branch's max character at --level and fights every Liberation
 * mission alone, each from a full clock, printing the clear time or, for a
 * mission lost to the clock, the HP left. Read it to see where the run is won
 * and which handicap stops it.
 *
 *   bazelisk run //analysis/sims:liberation_sim -- --level=260
 *   bazelisk run //analysis/sims:liberation_sim -- --level=270 --job=hero
 */
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "analysis/sim_jobs.h"
#include "analysis/sim_world.h"
#include "src/combat/boss/boss_run.h"
#include "src/combat/boss/liberation.h"
#include "src/game_state.h"

ABSL_FLAG(int, level, 260, "Character level of every branch's ceiling.");
ABSL_FLAG(std::string, job, "", "One branch (\"dark_knight\"), or all.");

namespace ms {
namespace {

constexpr unsigned int kSeed = 1;
constexpr double kStepSeconds = 0.02;

void Run() {
  Catalogs catalogs = LoadCatalogs();
  const int level = absl::GetFlag(FLAGS_level);
  std::vector<Job> branches = BranchesAt(level);
  if (!absl::GetFlag(FLAGS_job).empty()) {
    branches = {ParseBranch(absl::GetFlag(FLAGS_job))};
  }
  std::printf("%-22s", "branch");
  for (const LiberationMission& mission : kLiberationMissions) {
    std::printf(" %13s", mission.boss);
  }
  std::printf("\n");
  for (Job branch : branches) {
    GameState state =
        NewMaxState(catalogs, AdvancementForJobStage(branch, StageOf(branch)),
                    level, kSeed);
    state.bosses = catalogs.bosses;
    std::printf("%-22s", BranchName(branch).c_str());
    for (int i = 0; i < kLiberationMissionCount; ++i) {
      std::unique_ptr<BossRun> run =
          LiberationMissionRun(state, i, BossOptions());
      while (run != nullptr && !run->done()) {
        run->Advance(state, kStepSeconds);
      }
      if (run == nullptr) {
        std::printf(" %13s", "missing");
      } else if (run->won()) {
        std::printf(" %12.0fs", run->clear_seconds());
      } else {
        // The phase reached and the share of its HP left.
        std::printf(" %6s P%d %3.0f%%", "lost", run->phase(),
                    100.0 * run->phase_hp_fraction());
      }
      std::fflush(stdout);
    }
    std::printf("\n");
  }
}

}  // namespace
}  // namespace ms

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  ms::Run();
  return 0;
}
