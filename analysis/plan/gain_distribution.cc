#include "analysis/plan/gain_distribution.h"

#include <algorithm>
#include <cstddef>
#include <numeric>
#include <utility>
#include <vector>

namespace ms {

void GainDistribution::Add(double gain, double weight) {
  if (weight > 0.0) {
    gains_.push_back(gain);
    weights_.push_back(weight);
  }
}

void GainDistribution::Finish() {
  std::vector<std::size_t> order(gains_.size());
  std::iota(order.begin(), order.end(), 0);
  std::sort(order.begin(), order.end(), [this](std::size_t a, std::size_t b) {
    return gains_[a] < gains_[b];
  });
  std::vector<double> gains;
  std::vector<double> weights;
  double total = 0.0;
  for (std::size_t i : order) {
    total += weights_[i];
    if (!gains.empty() && gains.back() == gains_[i]) {
      weights.back() += weights_[i];
    } else {
      gains.push_back(gains_[i]);
      weights.push_back(weights_[i]);
    }
  }
  gains_ = std::move(gains);
  weights_ = std::move(weights);
  weight_before_.assign(gains_.size() + 1, 0.0);
  mass_before_.assign(gains_.size() + 1, 0.0);
  for (std::size_t i = 0; i < gains_.size(); ++i) {
    weights_[i] /= total;
    weight_before_[i + 1] = weight_before_[i] + weights_[i];
    mass_before_[i + 1] = mass_before_[i] + weights_[i] * gains_[i];
  }
}

void GainDistribution::Scale(double share) {
  for (double& gain : gains_) {
    gain *= share;
  }
  for (double& mass : mass_before_) {
    mass *= share;
  }
}

int GainDistribution::FirstAtOrAbove(double reserve) const {
  return static_cast<int>(
      std::lower_bound(gains_.begin(), gains_.end(), reserve) - gains_.begin());
}

double GainDistribution::MeanMax(double floor) const {
  const int at = FirstAtOrAbove(floor);
  return floor * weight_before_[at] + (mass_before_.back() - mass_before_[at]);
}

double GainDistribution::Below(double reserve) const {
  return weight_before_[FirstAtOrAbove(reserve)];
}

double GainDistribution::Stopped(double reserve) const {
  return mass_before_.back() - mass_before_[FirstAtOrAbove(reserve)];
}

}  // namespace ms
