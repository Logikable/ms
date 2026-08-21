#include "analysis/star_force_curve.h"

#include <cmath>
#include <cstdlib>
#include <utility>
#include <vector>

#include "src/item/equip_instance.h"
#include "src/item/star_force_cost.h"

namespace ms {
namespace {

typedef std::vector<std::vector<double>> Matrix;

// Solves `a` x = `b` by Gaussian elimination with partial pivoting, taking
// both by value because the elimination consumes them. The system is one row
// per star and never larger than thirty, so nothing here needs to be clever.
//
// Returns an empty vector for a singular system, which would mean a star with
// no way out of it -- the rate table has none.
std::vector<double> Solve(Matrix a, std::vector<double> b) {
  int n = static_cast<int>(b.size());
  for (int column = 0; column < n; ++column) {
    int pivot = column;
    for (int row = column + 1; row < n; ++row) {
      if (std::abs(a[row][column]) > std::abs(a[pivot][column])) {
        pivot = row;
      }
    }
    if (a[pivot][column] == 0.0) {
      return {};
    }
    std::swap(a[column], a[pivot]);
    std::swap(b[column], b[pivot]);
    for (int row = column + 1; row < n; ++row) {
      double factor = a[row][column] / a[column][column];
      if (factor == 0.0) {
        continue;
      }
      for (int i = column; i < n; ++i) {
        a[row][i] -= factor * a[column][i];
      }
      b[row] -= factor * b[column];
    }
  }
  std::vector<double> x(n, 0.0);
  for (int row = n - 1; row >= 0; --row) {
    double sum = b[row];
    for (int i = row + 1; i < n; ++i) {
      sum -= a[row][i] * x[i];
    }
    x[row] = sum / a[row][row];
  }
  return x;
}

// The cost of reaching the target from each star below it, given what one
// attempt at each star is worth. Reading the system: standing at s, an
// attempt is paid for, and then the player is either at s + 1, back at
// whatever a recovery hands out, or still at s -- so
//
//   E[s] = cost(s) + p_success E[s+1] + p_destroy E[recovery] + p_fail E[s]
//
// which is the row below once the E[s] terms are gathered on the left.
std::vector<double> Expectations(int target, const std::vector<double>& cost) {
  Matrix a(target, std::vector<double>(target, 0.0));
  for (int stars = 0; stars < target; ++stars) {
    StarForceRate rate = EquipInstance::RateAt(stars);
    double success = rate.success / 10000.0;
    double destroy = rate.destroy / 10000.0;
    a[stars][stars] = success + destroy;
    if (stars + 1 < target) {
      a[stars][stars + 1] -= success;
    }
    if (destroy > 0.0) {
      a[stars][EquipInstance::RecoveryStars(stars)] -= destroy;
    }
  }
  return Solve(a, cost);
}

}  // namespace

StarForceRun StarForceRunTo(int required_level, int from, int to) {
  StarForceRun run;
  if (from < 0 || to <= from || to > kMaxStarForce) {
    return run;
  }
  std::vector<double> meso(to, 0.0);
  std::vector<double> attempts(to, 1.0);
  std::vector<double> booms(to, 0.0);
  for (int stars = 0; stars < to; ++stars) {
    meso[stars] = static_cast<double>(StarForceCost(required_level, stars));
    booms[stars] = EquipInstance::RateAt(stars).destroy / 10000.0;
  }
  std::vector<double> meso_from = Expectations(to, meso);
  std::vector<double> attempts_from = Expectations(to, attempts);
  std::vector<double> booms_from = Expectations(to, booms);
  if (meso_from.empty()) {
    return run;
  }
  run.meso = meso_from[from];
  run.attempts = attempts_from[from];
  run.booms = booms_from[from];
  return run;
}

}  // namespace ms
