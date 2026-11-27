/* breakdown_sim: the end-of-fight damage breakdown, for every branch.
 *
 * Seeds each branch's max character at --level, fights --fight in Practice mode
 * for up to --seconds, and prints the damage breakdown the fight screen shows:
 * a row per skill with its share, casts, lines and damage per line. Use it to
 * see where a branch's damage comes from, and to check that every row names a
 * skill a player would recognise.
 *
 *   bazelisk run //analysis/sims:breakdown_sim -- --level=230 --fight=lotus
 *   bazelisk run --config=sixth //analysis/sims:breakdown_sim -- --level=300 \
 *       --hexa=30 --fight=darknell --difficulty=Hard --seconds=340
 */
#include <algorithm>
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
#include "src/character/matrix/hexa_matrix.h"
#include "src/combat/boss/boss_run.h"
#include "src/combat/damage_breakdown.h"
#include "src/game_state.h"
#include "src/protos/boss.pb.h"

ABSL_FLAG(int, level, 230, "Character level of every branch's ceiling.");
ABSL_FLAG(int, hexa, 0,
          "Learn every HEXA node the branch reaches to this level (6th job "
          "builds only); max mode buys none.");
ABSL_FLAG(std::string, fight, "lotus", "Boss key, as data/bosses names it.");
ABSL_FLAG(std::string, difficulty, "Normal", "Difficulty name.");
ABSL_FLAG(std::string, job, "", "One branch (\"dark_knight\"), or all.");
ABSL_FLAG(bool, dummy, false,
          "Fight one of the first phase's first boss, alone, on no clock and "
          "with HP that outlasts --seconds: KMS's dummy runs, for comparing "
          "shares with their battle analyses.");
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

// The fight cut down to one of its first monster, which never dies or runs
// out of time. See --dummy.
std::string MakeDummy(GameState& state, const std::string& key,
                      int difficulty) {
  BossDifficulty& fight =
      *state.bosses.at(key).mutable_difficulties(difficulty);
  // A fight's clock of 0 has already run out; a day outlasts any --seconds.
  fight.set_time_limit_seconds(86400);
  BossPhase first = fight.phases(0);
  first.set_time_limit_seconds(0);
  Spawn lone = first.spawns(0);
  lone.set_count(1);
  first.clear_spawns();
  *first.add_spawns() = lone;
  fight.clear_phases();
  *fight.add_phases() = first;
  // Far past any run, and far from overflowing what HP is multiplied into.
  state.mobs.at(lone.mob()).set_max_hp(int64_t{1} << 57);
  return lone.mob();
}

DamageBreakdown Play(GameState& state, const std::string& key, int difficulty) {
  BossOptions practice_only;
  practice_only.set_practice(true);
  BossRun run(key, state.bosses.at(key), difficulty, /*authority=*/nullptr,
              practice_only);
  while (!run.done() &&
         run.breakdown().seconds() < absl::GetFlag(FLAGS_seconds)) {
    run.Advance(state, kStepSeconds);
  }
  return run.breakdown();
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
    std::string dummy;
    if (absl::GetFlag(FLAGS_dummy)) {
      dummy = MakeDummy(state, boss->first, difficulty);
    }
    if (absl::GetFlag(FLAGS_hexa) > 0) {
      LearnHexaTo(state, absl::GetFlag(FLAGS_hexa));
    }
    DamageBreakdown played = Play(state, boss->first, difficulty);
    // Ascents go out as the HP falls, so the dummy gets the HP the first run
    // dealt: its marks then pass across the run, as in a real fight.
    if (!dummy.empty()) {
      state.mobs.at(dummy).set_max_hp(
          static_cast<int64_t>(played.total() * 1.01));
      played = Play(state, boss->first, difficulty);
    }
    PrintBreakdown(branch, played);
  }
}

}  // namespace
}  // namespace ms

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  ms::Run();
  return 0;
}
