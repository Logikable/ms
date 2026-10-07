/* How the sims spend the account's familiar EXP and Familiar Cubes, the way a
 * player who measures would.
 *
 * Every familiar is alike, so the plan summons the roster's first three in
 * every preset and calls them the mains. EXP goes, a step at a time, to
 * whichever buys more damage per EXP: the next level of a main, valued as the
 * mean of the lines its new rank rolls, or the next level of Familiar Bond,
 * reached by levelling the others cheapest step first. When the better of the
 * two can't be afforded the pool waits for it.
 *
 * Cubes go only on Legendary mains, since a level-up rerolls anything below.
 * They follow the cube rule (see //analysis:cube_plan): roll while the held
 * lines are worth less than r, with E[max(X, r)] - price = r. Each line is
 * valued alone against the character as they stand and a familiar's two lines
 * are summed, which misses how ignored defence and the boss cap compound.
 */
#ifndef MS_ANALYSIS_FAMILIAR_PLAN_H_
#define MS_ANALYSIS_FAMILIAR_PLAN_H_

#include <cstdint>
#include <functional>
#include <vector>

#include "src/game_state.h"

namespace ms {

// What a value costs and what the drop line earns, from the shelf.
struct FamiliarPrices {
  // Damage a meso buys elsewhere: what a cube costs in damage.
  double power_per_meso = 0.0;
  // Damage the boss drop line is worth: the loot +100% drop rate earns over
  // the rest of the run, at power_per_meso. Zero values it at nothing.
  double drop_line_power = 0.0;
};

struct FamiliarSpend {
  int levels = 0;
  int cubes = 0;
  int64_t meso = 0;
};

// The reservation value r of a roll worth each of `values` with the matching
// `odds`, at `price` a roll: E[max(X, r)] - price = r. The best value when the
// price is zero.
double FamiliarReserve(const std::vector<double>& values,
                       const std::vector<double>& odds, double price);

// Levels, summons and cubes the account's familiars under the rule above.
// `power` is the character's damage as the shelf measures it.
FamiliarSpend SpendFamiliars(GameState& state,
                             const std::function<double(GameState&)>& power,
                             const FamiliarPrices& prices);

}  // namespace ms

#endif  // MS_ANALYSIS_FAMILIAR_PLAN_H_
