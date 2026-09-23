/* breakdown_sim: the table a boss fight ends on, for every branch.
 *
 * Seeds each branch's ceiling character at --level, fights --fight as Practice
 * for up to --seconds, and prints the damage breakdown the fight screen will
 * show: a row per skill, its share, casts, lines and damage per line. For
 * reading which skills a branch's damage actually comes from, and for checking
 * that every row names a skill the player would recognise.
 *
 *   bazelisk run //analysis:breakdown_sim -- --level=230 --fight=lotus
 */
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/log.h"
#include "analysis/sim_boss.h"
#include "analysis/sim_jobs.h"
#include "analysis/sim_world.h"
#include "src/character/character.h"
#include "src/combat/boss_run.h"
#include "src/combat/damage_breakdown.h"
#include "src/game_state.h"
#include "src/protos/boss.pb.h"

ABSL_FLAG(int, level, 230, "Character level of every branch's ceiling.");
ABSL_FLAG(std::string, fight, "lotus", "Boss key, as data/bosses names it.");
ABSL_FLAG(std::string, difficulty, "Normal", "Difficulty name.");
ABSL_FLAG(std::string, job, "", "One branch (\"dark_knight\"), or all.");
ABSL_FLAG(double, seconds, 180.0,
          "Fight seconds before the table is read, if the boss is still up.");

namespace ms {
namespace {

constexpr unsigned int kSeed = 1;
constexpr double kStepSeconds = 0.02;

void PrintBreakdown(Job branch, const DamageBreakdown& breakdown) {
  std::printf("\n%s: %.0f damage over %.1fs\n", BranchName(branch).c_str(),
              breakdown.total(), breakdown.seconds());
  std::printf("  %-30s %18s %7s %7s %9s %16s\n", "skill", "damage", "share",
              "casts", "lines", "per line");
  for (const BreakdownRow& row : breakdown.Rows()) {
    std::printf("  %-30s %18.0f %6.2f%% %7lld %9lld %16.0f\n",
                row.skill.c_str(), row.damage,
                100.0 * DamageShare(row, breakdown.total()),
                static_cast<long long>(row.casts),
                static_cast<long long>(row.lines), row.per_line());
  }
}

void Run() {
  Catalogs catalogs = LoadCatalogs();
  std::map<std::string, Boss>::const_iterator boss =
      catalogs.bosses.find(absl::GetFlag(FLAGS_fight));
  if (boss == catalogs.bosses.end()) {
    LOG(FATAL) << "no boss " << absl::GetFlag(FLAGS_fight);
  }
  int difficulty =
      BossDifficultyIndex(boss->second, absl::GetFlag(FLAGS_difficulty));
  if (difficulty < 0) {
    LOG(FATAL) << "no difficulty " << absl::GetFlag(FLAGS_difficulty);
  }
  int level = absl::GetFlag(FLAGS_level);
  std::vector<Job> branches = BranchesAt(level);
  if (!absl::GetFlag(FLAGS_job).empty()) {
    branches = {ParseBranch(absl::GetFlag(FLAGS_job))};
  }
  for (Job branch : branches) {
    GameState state =
        NewMaxState(catalogs, AdvancementForJobStage(branch, StageOf(branch)),
                    level, kSeed);
    state.bosses = catalogs.bosses;
    BossRun run(boss->first, state.bosses.at(boss->first), difficulty,
                /*authority=*/nullptr, /*practice=*/true);
    while (!run.done() &&
           run.breakdown().seconds() < absl::GetFlag(FLAGS_seconds)) {
      run.Advance(state, kStepSeconds);
    }
    PrintBreakdown(branch, run.breakdown());
  }
}

}  // namespace
}  // namespace ms

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  ms::Run();
  return 0;
}
