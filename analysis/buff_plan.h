/* Keeping the potions, the way a player decides to: which to have switched on,
 * and which to stop renting and buy outright.
 *
 * On is arithmetic. A buff pays a rate and costs a rate, and the encounter in
 * front of the character says which is larger -- so the Wealth Acquisition
 * Potion goes on the moment the map pays more than the thousand a second it
 * drinks, and comes off again on a map that does not.
 *
 * Buying is a question about the horizon. The permanent price is worth paying
 * only when what it saves over the rest of the run clears it with room to
 * spare, which is what separates the two buffs: a potion drunk by the second
 * pays its 100m back in a day, and one charged per boss entry is up against
 * a lockout that allows a handful of entries a day.
 *
 * Nothing here decides how a fight goes -- the potions' effects are the game's
 * business. This only says what the player does about them and adds up what
 * that cost.
 */
#ifndef MS_ANALYSIS_BUFF_PLAN_H_
#define MS_ANALYSIS_BUFF_PLAN_H_

#include <cstdint>

#include "absl/types/span.h"
#include "src/game_state.h"
#include "src/protos/mob.pb.h"

namespace ms {

// How a run treats the buffs. Anything but kAuto is there to read the
// counterfactual against: what the climb looks like having never drunk one,
// and what it looks like renting one it should have bought.
enum class BuffMode {
  kAuto,  // on when it pays, bought when the horizon says it pays twice over
  kOff,   // never switched on
  kRent,  // switched on when it pays, never bought outright
  kBuy,   // switched on when it pays, bought the moment the purse can
};

// What the player knows when they take the decision.
struct BuffPolicy {
  BuffMode mode = BuffMode::kAuto;
  // Seconds of the run still ahead of them. Only as good as the run's own
  // horizon: under --total_days it is exact, and otherwise it is the give-up
  // clock, which is far longer than the climb really is. A buy decision taken
  // against a horizon nobody reaches is a buy decision that always says yes.
  double seconds_left = 0.0;
  // How often they have been walking into a boss fight, over the run so far.
  // The Extreme Green Potion is charged per entry, so this is the whole of
  // what says whether buying it outright can pay.
  double boss_entries_per_second = 0.0;
};

// What the buffs did to the purse, and what was drunk to do it.
struct BuffSpend {
  // Paid by the second of farming and by the boss entry.
  int64_t drained = 0;
  // Paid once, for good.
  int64_t bought = 0;
  // Boss fights walked into, which is what the entry rate above is measured
  // from -- counted whether or not a buff was on for them.
  int64_t entries = 0;
  // Playtime the run has spent farming with a buff switched on.
  double drinking_seconds = 0.0;
};

// Meso a second the encounter pays under one set of levers. `mobs` and
// `kills_per_second` are parallel; a boss body is skipped, since a boss pays
// out of its own table and no %meso reaches it.
double BuffMesoPerSecond(absl::Span<const Mob* const> mobs,
                         absl::Span<const double> kills_per_second,
                         double meso_pct, double meso_mult, double drop_pct);

// Takes the buff decisions for the character as they stand, switching each on
// or off and buying what is worth buying. Called at a look, beside the rest of
// the player's shopping.
void PlanBuffs(GameState& state, const BuffPolicy& policy,
               absl::Span<const Mob* const> mobs,
               absl::Span<const double> kills_per_second, BuffSpend* spend);

// Charges the buffs for `seconds` of farming. The sim jumps whole stretches
// rather than ticking, so it pays for them here rather than through
// AdvanceCombat.
void DrinkBuffs(GameState& state, double seconds, BuffSpend* spend);

// Charges what walking into one boss fight costs, and counts the entry.
void EnterFightWithBuffs(GameState& state, BuffSpend* spend);

}  // namespace ms

#endif  // MS_ANALYSIS_BUFF_PLAN_H_
