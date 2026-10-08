// Reads progression_sim --stats_out rows and prints what stands out: branches
// far from the rest, stats that swing from seed to seed, and stats that split
// the branches by more than a seed does.
//
//   bazelisk run //analysis:progression_sim -- --runs=3 --stats_out=/tmp/s.tsv
//   bazelisk run //analysis:stats_report -- --in=/tmp/s.tsv
//   bazelisk run //analysis:stats_report -- --in=/tmp/s.tsv --stat=cubes

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/log.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "analysis/run_stats.h"
#include "analysis/sim_format.h"

ABSL_FLAG(std::string, in, "", "The rows progression_sim --stats_out wrote.");
ABSL_FLAG(double, min_z, 3.0,
          "How many robust deviations from the median a branch must sit.");
ABSL_FLAG(double, min_ratio, 0.25,
          "How far from the median, as a share of it, a branch must sit.");
ABSL_FLAG(int, top, 30, "Rows per section.");
ABSL_FLAG(std::string, stat, "",
          "Print every branch and seed of each stat whose name contains this, "
          "instead of the summary.");

namespace ms {
namespace {

std::string Short(double value) {
  char text[16];
  if (std::fabs(value) < 1000.0) {
    std::snprintf(text, sizeof(text), "%.3g", value);
  } else {
    FormatShort(value, text, sizeof(text));
  }
  return text;
}

void PrintTable(const StatSummary& summary) {
  std::printf("\n%s  (median %s)\n", summary.stat.c_str(),
              Short(summary.median).c_str());
  for (const auto& [branch, values] : summary.by_branch) {
    std::printf("  %-16s %9s  |", branch.c_str(),
                Short(summary.branch_mean.at(branch)).c_str());
    for (double value : values) {
      std::printf(" %9s", Short(value).c_str());
    }
    std::printf("\n");
  }
}

void PrintOutliers(const std::vector<StatSummary>& stats, int top) {
  std::vector<Outlier> found = FindOutliers(stats, absl::GetFlag(FLAGS_min_z),
                                            absl::GetFlag(FLAGS_min_ratio));
  std::printf(
      "\nBranches far from the rest (%zu found): the branch's mean over its "
      "seeds against\nthe median branch, past twice the stat's seed "
      "spread.\n\n",
      found.size());
  std::printf("  %-44s %-16s %9s %9s %7s\n", "stat", "branch", "mean", "median",
              "off by");
  for (int i = 0; i < std::min<int>(top, found.size()); ++i) {
    const Outlier& o = found[i];
    std::printf("  %-44s %-16s %9s %9s %6.0f%%\n", o.stat.c_str(),
                o.branch.c_str(), Short(o.mean).c_str(),
                Short(o.median).c_str(), 100.0 * o.ratio);
  }
}

// Stats most branches never touch, with the branches that do.
void PrintFew(const std::vector<StatSummary>& stats, int top) {
  std::printf("\nStats most branches leave at 0, and who doesn't.\n\n");
  int shown = 0;
  for (const StatSummary& summary : stats) {
    if (summary.median != 0.0 || shown >= top) {
      continue;
    }
    std::vector<std::string> some;
    for (const auto& [branch, mean] : summary.branch_mean) {
      if (mean != 0.0) {
        some.push_back(absl::StrCat(branch, " ", Short(mean)));
      }
    }
    if (some.empty()) {
      continue;
    }
    std::printf("  %-44s %s\n", summary.stat.c_str(),
                absl::StrJoin(some, ", ").c_str());
    ++shown;
  }
}

void PrintRanked(const char* title, std::vector<const StatSummary*> ranked,
                 double (*key)(const StatSummary&), int top) {
  std::stable_sort(ranked.begin(), ranked.end(),
                   [key](const StatSummary* a, const StatSummary* b) {
                     return key(*a) > key(*b);
                   });
  std::printf("\n%s\n\n  %-44s %9s %9s %9s\n", title, "stat", "median",
              "by seed", "by job");
  for (int i = 0; i < std::min<int>(top, ranked.size()); ++i) {
    const StatSummary& s = *ranked[i];
    std::printf("  %-44s %9s %8.0f%% %8.0f%%\n", s.stat.c_str(),
                Short(s.median).c_str(), 100.0 * s.seed_spread,
                100.0 * s.branch_spread);
  }
}

double SeedKey(const StatSummary& s) {
  return s.seed_spread;
}

// How much more the branches differ than one branch's seeds do. A stat that
// never varies by seed sorts by its branch spread alone.
double SplitKey(const StatSummary& s) {
  return s.branch_spread / std::max(s.seed_spread, 0.05);
}

void Run() {
  std::ifstream file(absl::GetFlag(FLAGS_in));
  if (!file) {
    LOG(ERROR) << "can't read --in=" << absl::GetFlag(FLAGS_in);
    return;
  }
  std::stringstream text;
  text << file.rdbuf();
  std::vector<RunStats> runs = ParseStatsRows(text.str());
  std::vector<StatSummary> stats = Summarize(runs);
  const std::string& wanted = absl::GetFlag(FLAGS_stat);
  if (!wanted.empty()) {
    for (const StatSummary& summary : stats) {
      if (absl::StrContains(summary.stat, wanted)) {
        PrintTable(summary);
      }
    }
    return;
  }
  std::printf("%zu runs, %zu stats.\n", runs.size(), stats.size());
  int top = absl::GetFlag(FLAGS_top);
  PrintOutliers(stats, top);
  PrintFew(stats, top);
  std::vector<const StatSummary*> live;
  for (const StatSummary& summary : stats) {
    if (summary.median != 0.0 || summary.branch_spread != 0.0) {
      live.push_back(&summary);
    }
  }
  PrintRanked(
      "Stats that swing most from seed to seed (coefficient of variation "
      "within a branch,\naveraged over branches).",
      live, SeedKey, top);
  PrintRanked(
      "Stats that split the branches by more than a seed does (spread of "
      "the branch\nmeans over the seed spread).",
      live, SplitKey, top);
}

}  // namespace
}  // namespace ms

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  ms::Run();
  return 0;
}
