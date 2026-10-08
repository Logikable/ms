/* One run's numbers by name, and what a set of them says across jobs and
 * seeds. progression_sim --stats_out writes the rows; //analysis:stats_report
 * reads them back and picks out the jobs far from the rest and the stats that
 * swing from seed to seed.
 *
 * The file is tab-separated: branch, seed, stat, value, one stat a line. A
 * stat a run never wrote reads as 0, since most are counts (cubes of a kind,
 * days on a map) that a run simply never touched.
 */
#ifndef MS_ANALYSIS_RUN_STATS_H_
#define MS_ANALYSIS_RUN_STATS_H_

#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ms {

struct RunStats {
  std::string branch;
  unsigned int seed = 0;
  std::vector<std::pair<std::string, double>> values;

  void Add(std::string name, double value) {
    values.emplace_back(std::move(name), value);
  }
};

// Appends `run`'s rows to `out`. Tabs and newlines in a name become spaces.
void AppendStatsRows(const RunStats& run, std::string* out);

// The runs in `text`, in the order each first appears. Lines that don't parse
// are skipped.
std::vector<RunStats> ParseStatsRows(std::string_view text);

struct StatSummary {
  std::string stat;
  // Each branch's values, one per seed, and their mean.
  std::map<std::string, std::vector<double>> by_branch;
  std::map<std::string, double> branch_mean;
  // The median of the branch means, and their spread as a coefficient of
  // variation (0 when the median is 0).
  double median = 0.0;
  double branch_spread = 0.0;
  // The mean across branches of each branch's own seed-to-seed coefficient of
  // variation. 0 with a single seed.
  double seed_spread = 0.0;
};

// One summary per stat, in name order, over every branch in `runs`.
std::vector<StatSummary> Summarize(const std::vector<RunStats>& runs);

struct Outlier {
  std::string stat;
  std::string branch;
  double mean = 0.0;
  double median = 0.0;
  // Distance from the median in robust standard deviations (1.4826 MADs),
  // infinite when every other branch agrees exactly.
  double z = 0.0;
};

// The branches whose mean sits at least `min_z` robust deviations and
// `min_ratio` of the median away from it, most extreme first. Needs three
// branches; with fewer there is no "rest".
std::vector<Outlier> FindOutliers(const std::vector<StatSummary>& stats,
                                  double min_z, double min_ratio);

}  // namespace ms

#endif  // MS_ANALYSIS_RUN_STATS_H_
