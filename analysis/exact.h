/* Whether the sims take the speed cuts that trade accuracy for time. Exact
 * runs take none, and are the baseline a cut is measured against: the user's
 * bar is within 5% of it. Every new cut must ask Exact() first.
 */
#ifndef MS_ANALYSIS_EXACT_H_
#define MS_ANALYSIS_EXACT_H_

namespace ms {

bool Exact();
void SetExact(bool exact);

// The longest cooldown cycle a measurement plays out, in game seconds: two
// minutes covers every burst buff, and ToCycleShare cuts a longer cooldown's
// attack to its share. Exact runs play the whole `cycle`.
inline constexpr double kCycleCapSeconds = 120.0;
double CappedCycle(double cycle);

}  // namespace ms

#endif  // MS_ANALYSIS_EXACT_H_
