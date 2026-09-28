/* Expected cost of a star force run.
 *
 * Every attempt is paid for whether it succeeds, fails or destroys the item,
 * and from 15 stars up a trace recovery returns the item several stars lower.
 * That loop makes the cost a linear system rather than a sum. Prices and odds
 * come from StarForceCost and EquipInstance::RateAt.
 */
#ifndef MS_ANALYSIS_STAR_FORCE_CURVE_H_
#define MS_ANALYSIS_STAR_FORCE_CURVE_H_

#include <functional>

namespace ms {

// Average result of one run over many players doing the same thing.
struct StarForceRun {
  double meso = 0.0;      // total cost of every attempt, failures included
  double attempts = 0.0;  // attempts, whatever each one did
  double booms = 0.0;     // times the item is destroyed and recovered
};

// Expected cost of taking one item of `required_level` from `from` stars to
// `to`, recovering it after every boom. `booms` is a count, not a price,
// because a drop-only item has no price. Zero for a run that goes nowhere.
StarForceRun StarForceRunTo(int required_level, int from, int to);

// The run from `from` stars worth the most per meso, and what its first star
// costs. A star is bought as the first step of such a run: 13 to 15 are poor
// on their own, but they are the only way to 16 and past, which add far more.
struct StarRunChoice {
  int to = 0;              // the run's last star; 0 when there is none
  double gain = 0.0;       // what the whole run adds, as `gain_to` measures it
  double cost = 0.0;       // the whole run, each boom priced at `spare`
  double step_cost = 0.0;  // the first star alone, priced the same way
};

// Considers every run up to `ceiling` stars. `gain_to(to)` is what reaching
// `to` adds over `from`. Without `can_boom` a run ends before the first star
// that could destroy the item, since nothing would bring it back.
StarRunChoice BestStarRun(int required_level, int from, int ceiling,
                          double spare, bool can_boom,
                          const std::function<double(int)>& gain_to);

}  // namespace ms

#endif  // MS_ANALYSIS_STAR_FORCE_CURVE_H_
