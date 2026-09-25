/* Expected cost of a star force run.
 *
 * Every attempt is paid for whether it succeeds, fails or destroys the item. So
 * a star costs its price divided by the chance of success, plus every attempt
 * spent climbing back after a boom. From 15 stars up, a trace recovery returns
 * the item several stars lower, so the same star can be paid for many times.
 *
 * That loop makes the cost a linear system rather than a sum, which this
 * solves. Prices and odds come from the game itself (StarForceCost and
 * EquipInstance::RateAt), so they can't drift from what a player is charged.
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
// `to`, recovering it after every boom and continuing from the star the trace
// returns. `booms` is a count, not a price, because a replacement's cost
// depends on where the item came from and a drop-only item has no price. Zero
// for a run that goes nowhere.
StarForceRun StarForceRunTo(int required_level, int from, int to);

}  // namespace ms

#endif  // MS_ANALYSIS_STAR_FORCE_CURVE_H_
