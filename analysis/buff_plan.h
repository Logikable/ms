/* Decides potion buffs the way a player would: which to switch on, and which to
 * stop renting and buy outright.
 *
 * Switching on is arithmetic. A buff pays a rate and costs a rate, and the
 * current encounter says which is larger. So the Wealth Acquisition Potion goes
 * on as soon as the map pays more than the thousand meso a second it costs, and
 * off again on a map that doesn't.
 *
 * Buying depends on the time left. The permanent price is only worth paying
 * when the savings over the rest of the run clear it with room to spare. That
 * is what separates the two buffs: a potion paid by the second earns back its
 * 100m in a day, while one charged per boss entry is limited by lockouts to a
 * handful of entries a day.
 *
 * This doesn't decide how a fight goes; the potions' effects belong to the
 * game. It only decides what the player does about them and totals the cost.
 */
#ifndef MS_ANALYSIS_BUFF_PLAN_H_
#define MS_ANALYSIS_BUFF_PLAN_H_

#include <cstdint>

#include "absl/types/span.h"
#include "analysis/meso_rate.h"
#include "src/game_state.h"

namespace ms {

// How a run treats buffs. Modes other than kAuto exist for comparison: the
// climb with no buffs at all, or renting one it should have bought.
enum class BuffMode {
  kAuto,  // on when it pays, bought when the horizon pays twice over
  kOff,   // never on
  kRent,  // on when it pays, never bought
  kBuy,   // on when it pays, bought as soon as affordable
};

// What the player knows when deciding.
struct BuffPolicy {
  BuffMode mode = BuffMode::kAuto;
  // Seconds of the run still ahead. Only as accurate as the run's own horizon:
  // exact under --total_days, otherwise the give-up clock, which is far longer
  // than the real climb. A buy decision against a horizon nobody reaches always
  // says yes.
  double seconds_left = 0.0;
  // Boss entries per second over the run so far. The Extreme Green Potion is
  // charged per entry, so this alone decides whether buying it can pay.
  double boss_entries_per_second = 0.0;
};

// What buffs cost the character, and how long they were on.
struct BuffSpend {
  // Paid per second of farming and per boss entry.
  int64_t drained = 0;
  // Paid once, permanently.
  int64_t bought = 0;
  // Boss fights entered, counted whether or not a buff was on. The entry rate
  // above is measured from this.
  int64_t entries = 0;
  // Farming time spent with a buff on.
  double drinking_seconds = 0.0;
};

// What the current encounter kills, which buffs are weighed against. See
// //analysis:meso_rate for the currency.
struct BuffYield {
  Crowd crowd;
  // The same crowd's kill rate measured twice more: at the normal spawn
  // interval and at the Wild Totem's halved one. No arithmetic on `crowd` can
  // replace this pair. The totem doubles how often the map spawns mobs, and a
  // character who wasn't waiting on spawns kills no more than before.
  absl::Span<const double> kills_without_totem;
  absl::Span<const double> kills_with_totem;
};

// Makes the buff decisions for the character as they are now, switching each on
// or off and buying what is worth buying. Called at a look, alongside the rest
// of the player's shopping.
void PlanBuffs(GameState& state, const BuffPolicy& policy,
               const BuffYield& yield, BuffSpend* spend);

// Charges the buffs for `seconds` of farming. The sim skips ahead in whole
// stretches rather than ticking, so it pays here rather than through
// AdvanceCombat.
void DrinkBuffs(GameState& state, double seconds, BuffSpend* spend);

// Charges the cost of entering one boss fight, and counts the entry.
void EnterFightWithBuffs(GameState& state, BuffSpend* spend);

}  // namespace ms

#endif  // MS_ANALYSIS_BUFF_PLAN_H_
