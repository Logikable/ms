#include "analysis/run_stats.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"

namespace ms {
namespace {

std::string Clean(std::string_view text) {
  std::string out(text);
  std::replace(out.begin(), out.end(), '\t', ' ');
  std::replace(out.begin(), out.end(), '\n', ' ');
  return out;
}

double Median(std::vector<double> values) {
  if (values.empty()) {
    return 0.0;
  }
  std::sort(values.begin(), values.end());
  std::size_t mid = values.size() / 2;
  return values.size() % 2 == 1 ? values[mid]
                                : (values[mid - 1] + values[mid]) / 2.0;
}

double Mean(const std::vector<double>& values) {
  double sum = 0.0;
  for (double value : values) {
    sum += value;
  }
  return values.empty() ? 0.0 : sum / static_cast<double>(values.size());
}

// Sample standard deviation over the mean's magnitude; 0 for a zero mean or
// fewer than two values.
double Variation(const std::vector<double>& values) {
  double mean = Mean(values);
  if (values.size() < 2 || mean == 0.0) {
    return 0.0;
  }
  double squares = 0.0;
  for (double value : values) {
    squares += (value - mean) * (value - mean);
  }
  return std::sqrt(squares / static_cast<double>(values.size() - 1)) /
         std::fabs(mean);
}

}  // namespace

void AppendStatsRows(const RunStats& run, std::string* out) {
  std::string branch = Clean(run.branch);
  for (const auto& [name, value] : run.values) {
    absl::StrAppend(out, branch, "\t", run.seed, "\t", Clean(name), "\t", value,
                    "\n");
  }
}

std::vector<RunStats> ParseStatsRows(std::string_view text) {
  std::vector<RunStats> runs;
  std::map<std::pair<std::string, unsigned int>, std::size_t> index;
  for (std::string_view line : absl::StrSplit(text, '\n')) {
    std::vector<std::string_view> fields = absl::StrSplit(line, '\t');
    unsigned int seed = 0;
    double value = 0.0;
    if (fields.size() != 4 || !absl::SimpleAtoi(fields[1], &seed) ||
        !absl::SimpleAtod(fields[3], &value)) {
      continue;
    }
    std::pair<std::string, unsigned int> key(std::string(fields[0]), seed);
    auto [it, added] = index.emplace(key, runs.size());
    if (added) {
      runs.push_back({key.first, seed, {}});
    }
    runs[it->second].Add(std::string(fields[2]), value);
  }
  return runs;
}

std::vector<StatSummary> Summarize(const std::vector<RunStats>& runs) {
  std::set<std::string> names;
  for (const RunStats& run : runs) {
    for (const auto& entry : run.values) {
      names.insert(entry.first);
    }
  }
  std::vector<StatSummary> out;
  for (const std::string& name : names) {
    StatSummary summary;
    summary.stat = name;
    for (const RunStats& run : runs) {
      double value = 0.0;
      for (const auto& [stat, v] : run.values) {
        if (stat == name) {
          value = v;
        }
      }
      summary.by_branch[run.branch].push_back(value);
    }
    std::vector<double> means;
    std::vector<double> variations;
    for (const auto& [branch, values] : summary.by_branch) {
      double mean = Mean(values);
      summary.branch_mean[branch] = mean;
      means.push_back(mean);
      if (values.size() >= 2 && mean != 0.0) {
        variations.push_back(Variation(values));
      }
    }
    summary.median = Median(means);
    summary.branch_spread = Variation(means);
    summary.seed_spread = Mean(variations);
    out.push_back(std::move(summary));
  }
  return out;
}

std::vector<Outlier> FindOutliers(const std::vector<StatSummary>& stats,
                                  double min_z, double min_ratio) {
  std::vector<Outlier> out;
  for (const StatSummary& summary : stats) {
    if (summary.branch_mean.size() < 3 || summary.median == 0.0) {
      continue;
    }
    std::vector<double> deviations;
    for (const auto& entry : summary.branch_mean) {
      deviations.push_back(std::fabs(entry.second - summary.median));
    }
    double scale = 1.4826 * Median(deviations);
    // A gap a reseed could open is no finding, however tight the branches.
    double floor = std::max(min_ratio, 2.0 * summary.seed_spread);
    for (const auto& [branch, mean] : summary.branch_mean) {
      double gap = std::fabs(mean - summary.median);
      double z =
          scale > 0.0 ? gap / scale : std::numeric_limits<double>::infinity();
      double ratio = gap / std::fabs(summary.median);
      if (gap > 0.0 && z >= min_z && ratio >= floor) {
        out.push_back({summary.stat, branch, mean, summary.median, z, ratio});
      }
    }
  }
  std::stable_sort(
      out.begin(), out.end(),
      [](const Outlier& a, const Outlier& b) { return a.ratio > b.ratio; });
  return out;
}

}  // namespace ms
