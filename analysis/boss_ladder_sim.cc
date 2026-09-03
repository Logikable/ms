/* Which branch is strongest at a level, answered against the boss ladder.
 *
 * At every level a fight opens at, the mode's ceiling character -- what
 * --mode=max seeds, a player who spent well standing at that level -- takes
 * every fight the screen has open, several times each. A branch is ranked by
 * how many of them it can finish, and two branches that finish the same
 * number by how fast the hardest of them went down.
 *
 * The count comes first because a fight nobody else can win is worth more
 * than a quicker clear of one everybody wins, and the clock breaks the tie
 * because at every level below the cap the whole roster clears the whole
 * ladder.
 *
 * A fight against a fixed character is deterministic: the ceiling is written
 * rather than rolled, and nothing in the fight itself draws. So one attempt
 * settles it, and --trials is there for the day that stops being true.
 *
 * Not a test. Tests pin behaviour that must not change; this prints numbers
 * to look at while deciding what the behaviour should be.
 *
 *   bazelisk run //analysis:boss_ladder_sim
 *   bazelisk run //analysis:boss_ladder_sim -- --levels=200 --trials=5 --detail
 */
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/strings/str_cat.h"
#include "analysis/parallel.h"
#include "analysis/sim_boss.h"
#include "analysis/sim_format.h"
#include "analysis/sim_jobs.h"
#include "analysis/sim_world.h"
#include "src/character/character.h"
#include "src/game_state.h"
#include "src/protos/boss.pb.h"
#include "src/protos/character.pb.h"

ABSL_FLAG(std::string, levels, "110,120,130,140,150,160,170,180,190,200",
          "Levels to rank the roster at, comma separated.");
ABSL_FLAG(int, trials, 1,
          "Attempts at each fight, a branch being credited with one it wins "
          "more often than not. One is enough while a fight against a fixed "
          "character plays out the same way every time.");
ABSL_FLAG(int, seed, 20260903,
          "The random stream. Each attempt draws its own from this.");
ABSL_FLAG(std::string, job, "",
          "One branch only, as --job spells it (\"dark_knight\"). Empty "
          "sweeps every branch the level has reached.");
ABSL_FLAG(bool, detail, false,
          "Print every branch's result fight by fight, not just its total.");

namespace ms {
namespace {

// One fight the boss screen opens: a boss at one of its difficulties.
struct Fight {
  std::string boss;
  int index = 0;
  std::string label;  // "Normal Zakum", the way a player names it
  int unlock = 0;
  int64_t hp = 0;
  int clock = 0;
};

// Every fight open at `level`, easiest first. Only the difficulties the game
// has built: one marked coming soon is a shell holding nothing but HP.
std::vector<Fight> FightsAt(const Catalogs& catalogs, int level) {
  std::vector<Fight> fights;
  for (const std::pair<const std::string, Boss>& entry : catalogs.bosses) {
    for (int i = 0; i < entry.second.difficulties_size(); ++i) {
      const BossDifficulty& difficulty = entry.second.difficulties(i);
      if (difficulty.coming_soon() || difficulty.unlock_level() > level) {
        continue;
      }
      fights.push_back(
          {entry.first, i,
           absl::StrCat(difficulty.name(), " ", entry.second.name()),
           difficulty.unlock_level(), BossTotalHp(catalogs.mobs, difficulty),
           difficulty.time_limit_seconds()});
    }
  }
  std::sort(fights.begin(), fights.end(), [](const Fight& a, const Fight& b) {
    if (a.unlock != b.unlock) {
      return a.unlock < b.unlock;
    }
    if (a.hp != b.hp) {
      return a.hp < b.hp;
    }
    return a.label < b.label;
  });
  return fights;
}

// What one branch did with one fight over every attempt at it.
struct FightResult {
  int wins = 0;
  int tries = 0;
  // The middle clear, over the attempts that won. Zero for a fight never won.
  double seconds = 0.0;
  // The least ever left standing, so a fight nobody wins still says how close
  // the roster came to it.
  double left = 1.0;
};

// One branch's whole ladder at one level.
struct Row {
  Job job = JOB_UNSPECIFIED;
  std::vector<FightResult> fights;  // one per Fight, in the level's order
  int cleared = 0;
  // The stiffest fight cleared, and what it took. -1 for a branch that
  // cleared nothing.
  int hardest = -1;
  double hardest_seconds = 0.0;
};

// The middle of `values`, which is the clear to quote: a mean over three
// attempts is dragged by the one that went badly.
double Median(std::vector<double> values) {
  if (values.empty()) {
    return 0.0;
  }
  std::sort(values.begin(), values.end());
  return values[values.size() / 2];
}

std::string Clock(double seconds) {
  if (seconds < 60.0) {
    return absl::StrCat(static_cast<int>(seconds * 10.0) / 10, ".",
                        static_cast<int>(seconds * 10.0) % 10, "s");
  }
  int whole = static_cast<int>(seconds);
  return absl::StrCat(whole / 60, "m", whole % 60 < 10 ? "0" : "", whole % 60,
                      "s");
}

// Takes `fight` `trials` times with a freshly seeded ceiling character, so no
// attempt is standing in gear an earlier clear paid out.
FightResult TakeFight(const Catalogs& catalogs, JobAdvancement advancement,
                      int level, const Fight& fight, int trials,
                      unsigned int seed) {
  FightResult result;
  std::vector<double> clears;
  for (int trial = 0; trial < trials; ++trial) {
    GameState state =
        NewMaxState(catalogs, advancement, level, seed + trial * 977u);
    state.bosses = catalogs.bosses;
    const BossOutcome outcome = FightBoss(state, fight.boss, fight.index);
    ++result.tries;
    if (outcome.won) {
      ++result.wins;
      clears.push_back(outcome.seconds);
      result.left = 0.0;
    } else {
      result.left = std::min(result.left, outcome.left);
    }
  }
  result.seconds = Median(clears);
  return result;
}

// Fills in what the ladder came to: a fight won more often than not is a
// fight the branch can do, and the hardest of those is the one to rank on.
void Total(const std::vector<Fight>& fights, Row& row) {
  for (int i = 0; i < static_cast<int>(fights.size()); ++i) {
    const FightResult& result = row.fights[i];
    if (result.wins * 2 < result.tries) {
      continue;
    }
    ++row.cleared;
    row.hardest = i;  // fights are in order, so the last one through is it
    row.hardest_seconds = result.seconds;
  }
}

// Strongest first: more of the ladder beats a quicker clear of less of it.
bool Stronger(const Row& a, const Row& b) {
  if (a.cleared != b.cleared) {
    return a.cleared > b.cleared;
  }
  if (a.cleared == 0) {
    return BranchName(a.job) < BranchName(b.job);
  }
  return a.hardest_seconds < b.hardest_seconds;
}

std::vector<Job> Roster(int level) {
  const std::string named = absl::GetFlag(FLAGS_job);
  if (named.empty()) {
    return BranchesAt(level);
  }
  return {ParseBranch(named)};
}

// Every branch's ladder at `level`, strongest first.
std::vector<Row> RankAt(const Catalogs& catalogs, int level,
                        const std::vector<Fight>& fights) {
  const std::vector<Job> roster = Roster(level);
  std::vector<Row> rows(roster.size());
  const int fight_count = static_cast<int>(fights.size());
  const int trials = absl::GetFlag(FLAGS_trials);
  const unsigned int seed = absl::GetFlag(FLAGS_seed) + level;
  for (int i = 0; i < static_cast<int>(roster.size()); ++i) {
    rows[i].job = roster[i];
    rows[i].fights.resize(fights.size());
  }
  ParallelFor(static_cast<int>(roster.size()) * fight_count, [&](int i) {
    Row& row = rows[i / fight_count];
    const Fight& fight = fights[i % fight_count];
    row.fights[i % fight_count] =
        TakeFight(catalogs, AdvancementForJobStage(row.job, StageOf(row.job)),
                  level, fight, trials, seed + i * 7919u);
  });
  for (Row& row : rows) {
    Total(fights, row);
  }
  std::sort(rows.begin(), rows.end(), Stronger);
  return rows;
}

void PrintDetail(const std::vector<Fight>& fights, const Row& row) {
  for (int i = 0; i < static_cast<int>(fights.size()); ++i) {
    const FightResult& result = row.fights[i];
    char hp[16];
    FormatShort(static_cast<double>(fights[i].hp), hp, sizeof(hp));
    std::printf("        %-22s %5s %2d/%-2d %9s  %5.0f%% left\n",
                fights[i].label.c_str(), hp, result.wins, result.tries,
                result.wins > 0 ? Clock(result.seconds).c_str() : "-",
                100.0 * result.left);
  }
}

void PrintLevel(int level, const std::vector<Fight>& fights,
                const std::vector<Row>& rows) {
  std::printf("\nLevel %d -- %d fight%s open, up to %s\n", level,
              static_cast<int>(fights.size()), fights.size() == 1 ? "" : "s",
              fights.back().label.c_str());
  std::printf("  %-4s %-16s %8s  %-22s %8s %8s\n", "rank", "branch", "cleared",
              "hardest cleared", "time", "clock");
  for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
    const Row& row = rows[i];
    const Fight* hardest = row.hardest >= 0 ? &fights[row.hardest] : nullptr;
    std::printf("  %-4d %-16s %4d/%-3d  %-22s %8s %8s\n", i + 1,
                BranchName(row.job).c_str(), row.cleared,
                static_cast<int>(fights.size()),
                hardest != nullptr ? hardest->label.c_str() : "-",
                hardest != nullptr ? Clock(row.hardest_seconds).c_str() : "-",
                hardest != nullptr ? Clock(hardest->clock).c_str() : "-");
    if (absl::GetFlag(FLAGS_detail)) {
      PrintDetail(fights, row);
    }
  }
}

// The whole sweep in one table: what each branch cleared at each level.
void PrintMatrix(const std::vector<int>& levels,
                 const std::vector<std::vector<Row>>& sweep,
                 const std::vector<std::vector<Fight>>& ladders) {
  std::printf("\n\nFights cleared, branch by level\n\n  %-16s", "branch");
  for (int level : levels) {
    std::printf(" %5d", level);
  }
  std::printf("\n  %-16s", "open");
  for (const std::vector<Fight>& fights : ladders) {
    std::printf(" %5d", static_cast<int>(fights.size()));
  }
  std::printf("\n");
  std::map<std::string, std::vector<int>> counts;
  for (int i = 0; i < static_cast<int>(sweep.size()); ++i) {
    for (const Row& row : sweep[i]) {
      std::vector<int>& line = counts[BranchName(row.job)];
      line.resize(levels.size(), -1);
      line[i] = row.cleared;
    }
  }
  for (const std::pair<const std::string, std::vector<int>>& entry : counts) {
    std::printf("  %-16s", entry.first.c_str());
    for (int count : entry.second) {
      if (count < 0) {
        std::printf(" %5s", "-");
      } else {
        std::printf(" %5d", count);
      }
    }
    std::printf("\n");
  }
}

void PrintWinners(const std::vector<int>& levels,
                  const std::vector<std::vector<Row>>& sweep,
                  const std::vector<std::vector<Fight>>& ladders) {
  std::printf(
      "\n\nStrongest branch, level by level\n\n  %-6s %-16s %8s  %-22s %8s\n",
      "level", "branch", "cleared", "hardest cleared", "time");
  for (int i = 0; i < static_cast<int>(levels.size()); ++i) {
    const Row& best = sweep[i].front();
    const Fight* hardest =
        best.hardest >= 0 ? &ladders[i][best.hardest] : nullptr;
    std::printf("  %-6d %-16s %4d/%-3d  %-22s %8s\n", levels[i],
                BranchName(best.job).c_str(), best.cleared,
                static_cast<int>(ladders[i].size()),
                hardest != nullptr ? hardest->label.c_str() : "-",
                hardest != nullptr ? Clock(best.hardest_seconds).c_str() : "-");
  }
}

}  // namespace
}  // namespace ms

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  const std::vector<int> levels =
      ms::ParseLevels(absl::GetFlag(FLAGS_levels), "levels");
  const ms::Catalogs catalogs = ms::LoadCatalogs();
  std::printf(
      "The ceiling character at each level takes every fight the boss screen "
      "has open,\n%d attempt(s) each. Ranked by how much of the ladder falls, "
      "then by the clock on\nthe hardest fight that did.\n",
      absl::GetFlag(FLAGS_trials));

  std::vector<std::vector<ms::Fight>> ladders;
  std::vector<std::vector<ms::Row>> sweep;
  for (int level : levels) {
    std::vector<ms::Fight> fights = ms::FightsAt(catalogs, level);
    if (fights.empty()) {
      std::printf("\nLevel %d -- no fight open yet\n", level);
      continue;
    }
    std::vector<ms::Row> rows = ms::RankAt(catalogs, level, fights);
    ms::PrintLevel(level, fights, rows);
    std::fflush(stdout);
    ladders.push_back(std::move(fights));
    sweep.push_back(std::move(rows));
  }
  if (!sweep.empty()) {
    std::vector<int> ranked;
    for (int level : levels) {
      if (!ms::FightsAt(catalogs, level).empty()) {
        ranked.push_back(level);
      }
    }
    ms::PrintMatrix(ranked, sweep, ladders);
    ms::PrintWinners(ranked, sweep, ladders);
  }
  return 0;
}
