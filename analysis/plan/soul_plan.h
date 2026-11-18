/* When to spend soul shards, the way a player who knows the odds would.
 *
 * A soul replaces the weapon's soul outright, so a roll can lose what the
 * weapon had: the worth of holding n rolls is an optimal stopping problem.
 * With V_0(x) = x and V_n(x) = max(x, mean over lines of V_(n-1)), rolling is
 * worth it while the mean of V_(n-1) beats the soul held. Shards have no
 * other use, so nothing else prices them.
 *
 * Pure math over line values the caller measures; see GearShopper.
 */
#ifndef MS_ANALYSIS_SOUL_PLAN_H_
#define MS_ANALYSIS_SOUL_PLAN_H_

#include <vector>

namespace ms {

// What rolling now is worth with `rolls` rolls in hand, each landing on one of
// `values` at equal odds and the best stop taken after it. Zero for no rolls.
double SoulRollWorth(const std::vector<double>& values, int rolls);

// Whether a player holding a soul worth `held` should roll again.
bool ShouldRollSoul(double held, const std::vector<double>& values, int rolls);

}  // namespace ms

#endif  // MS_ANALYSIS_SOUL_PLAN_H_
