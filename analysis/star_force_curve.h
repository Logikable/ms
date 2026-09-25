/* Expected cost of a star force run.
 *
 * Every attempt is paid for whether it succeeds, fails or destroys the item,
 * and from 15 stars up a trace recovery returns the item several stars lower.
 * That loop makes the cost a linear system rather than a sum. Prices and odds
 * come from StarForceCost and EquipInstance::RateAt.
 */
#ifndef MS_ANALYSIS_STAR_FORCE_CURVE_H_
#define MS_ANALYSIS_STAR_FORCE_CURVE_H_

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

}  // namespace ms

#endif  // MS_ANALYSIS_STAR_FORCE_CURVE_H_
