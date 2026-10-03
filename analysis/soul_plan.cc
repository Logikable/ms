#include "analysis/soul_plan.h"

#include <algorithm>
#include <vector>

namespace ms {
namespace {

// Past this many rolls the worth has converged on the best line to well
// within a measurement's noise, so a purse of 999 souls costs no more.
constexpr int kMaxRollsCounted = 64;

double Mean(const std::vector<double>& values, double floor) {
  double sum = 0.0;
  for (double value : values) {
    sum += std::max(value, floor);
  }
  return sum / values.size();
}

}  // namespace

double SoulRollWorth(const std::vector<double>& values, int rolls) {
  if (values.empty() || rolls <= 0) {
    return 0.0;
  }
  // Worth(1) is the plain mean; each roll after it floors the lines at the
  // worth of the rolls still to come.
  double worth = Mean(values, -1e300);
  for (int n = 2; n <= std::min(rolls, kMaxRollsCounted); ++n) {
    worth = Mean(values, worth);
  }
  return worth;
}

bool ShouldRollSoul(double held, const std::vector<double>& values, int rolls) {
  // The margin keeps a tie, or a gain lost in rounding, from spending shards.
  return rolls > 0 && SoulRollWorth(values, rolls) > held * (1.0 + 1e-9);
}

}  // namespace ms
