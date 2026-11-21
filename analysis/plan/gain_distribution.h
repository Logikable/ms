/* A roll's gain as a distribution: each distinct gain and its chance. What a
 * cube's or a flame's stopping rule is solved on, whether the chances are
 * counted exactly or a roll at a time from samples.
 */
#ifndef MS_ANALYSIS_PLAN_GAIN_DISTRIBUTION_H_
#define MS_ANALYSIS_PLAN_GAIN_DISTRIBUTION_H_

#include <vector>

namespace ms {

class GainDistribution {
 public:
  // Adds `weight` of chance at `gain`. Weights need not sum to 1: Finish
  // scales them so they do.
  void Add(double gain, double weight = 1.0);
  // Sorts, merges equal gains and normalises. Call once, after the last Add
  // and before any query.
  void Finish();
  // Multiplies every gain by `share`, which must be positive.
  void Scale(double share);

  bool empty() const {
    return gains_.empty();
  }
  double low() const {
    return gains_.front();
  }
  double high() const {
    return gains_.back();
  }
  // E[max(X, floor)].
  double MeanMax(double floor) const;
  // P(X < reserve), the chance a rule stopping at `reserve` rolls on.
  double Below(double reserve) const;
  // E[X; X >= reserve]: the mean gain of the rolls it stops on, times their
  // chance.
  double Stopped(double reserve) const;

 private:
  // The first gain at or above `reserve`.
  int FirstAtOrAbove(double reserve) const;

  std::vector<double> gains_;
  std::vector<double> weights_;
  // Running totals of the weights and of weight times gain, one past each
  // index, so a query is a search rather than a sum.
  std::vector<double> weight_before_;
  std::vector<double> mass_before_;
};

}  // namespace ms

#endif  // MS_ANALYSIS_PLAN_GAIN_DISTRIBUTION_H_
