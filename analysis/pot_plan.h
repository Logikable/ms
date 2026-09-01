/* Keeping the potions, the way a player decides to: which to have switched on,
 * and which to stop renting and buy outright.
 *
 * On is arithmetic. A pot pays a rate and costs a rate, and the encounter in
 * front of the character says which is larger -- so the Wealth Acquisition
 * Potion goes on the moment the map pays more than the thousand a second it
 * drinks, and comes off again on a map that does not.
 *
 * Buying is a question about the horizon. The permanent price is worth paying
 * only when what it saves over the rest of the run clears it with room to
 * spare, which is what separates the two pots: a potion drunk by the second
 * pays its 100m back in a day, and one charged per boss entry is up against
 * a lockout that allows a handful of entries a day.
 *
 * Nothing here decides how a fight goes -- the potions' effects are the game's
 * business. This only says what the player does about them and adds up what
 * that cost.
 */
#ifndef MS_ANALYSIS_POT_PLAN_H_
#define MS_ANALYSIS_POT_PLAN_H_

#include <cstdint>

#include "absl/types/span.h"
#include "src/game_state.h"
#include "src/protos/mob.pb.h"

namespace ms {

// How a run treats the pots. Anything but kAuto is there to read the
// counterfactual against: what the climb looks like having never drunk one,
// and what it looks like renting one it should have bought.
enum class PotMode {
  kAuto,  // on when it pays, bought when the horizon says it pays twice over
  kOff,   // never switched on
  kRent,  // switched on when it pays, never bought outright
  kBuy,   // switched on when it pays, bought the moment the purse can
};

// What the player knows when they take the decision.
struct PotPolicy {
  PotMode mode = PotMode::kAuto;
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

// What the pots did to the purse, and what was drunk to do it.
struct PotSpend {
  // Paid by the second of farming and by the boss entry.
  int64_t drained = 0;
  // Paid once, for good.
  int64_t bought = 0;
  // Boss fights walked into, which is what the entry rate above is measured
  // from -- counted whether or not a pot was on for them.
  int64_t entries = 0;
  // Playtime the run has spent farming with a pot switched on.
  double drinking_seconds = 0.0;
};

// Meso a second the encounter pays under one set of levers. `mobs` and
// `kills_per_second` are parallel; a boss body is skipped, since a boss pays
// out of its own table and no %meso reaches it.
double PotMesoPerSecond(absl::Span<const Mob* const> mobs,
                        absl::Span<const double> kills_per_second,
                        double meso_pct, double meso_mult, double drop_pct);

// Takes the pot decisions for the character as they stand, switching each on
// or off and buying what is worth buying. Called at a look, beside the rest of
// the player's shopping.
void PlanPots(GameState& state, const PotPolicy& policy,
              absl::Span<const Mob* const> mobs,
              absl::Span<const double> kills_per_second, PotSpend* spend);

// Charges the pots for `seconds` of farming. The sim jumps whole stretches
// rather than ticking, so it pays for them here rather than through
// AdvanceCombat.
void DrinkPots(GameState& state, double seconds, PotSpend* spend);

// Charges what walking into one boss fight costs, and counts the entry.
void EnterFightWithPots(GameState& state, PotSpend* spend);

}  // namespace ms

#endif  // MS_ANALYSIS_POT_PLAN_H_
