/* The potions: what each one costs, what it is worth, and when it opens.
 *
 * A pot is either rented or owned. A rented one charges its price every time
 * it procs -- by the second while farming, or on the way into a boss fight --
 * and an owned one never charges again. Either way the player switches it on
 * and off, and an owned pot switched off does nothing.
 *
 * A charge the purse cannot cover takes what is there and no more. The pot
 * still works: a player who is broke gets it at a discount rather than losing
 * it at the moment they can least afford to.
 *
 * One table, the way hyper_stats.h and inner_ability.h are: what a pot is
 * worth to the character is read off it by DerivedStatsFor and by the boss
 * params, and who may buy what is CharacterInstance's business.
 */
#ifndef MS_SRC_CHARACTER_CONSUMABLES_H_
#define MS_SRC_CHARACTER_CONSUMABLES_H_

#include <cstdint>
#include <string>

#include "absl/types/span.h"
#include "src/protos/character.pb.h"

namespace ms {

// The level the first pot opens at, which is the level the tab arrives at.
inline constexpr int kConsumableUnlockLevel = 170;

// What one pot costs and when it opens. The price is charged per proc, and
// what a proc is differs by pot -- see `per_second`.
struct ConsumableInfo {
  ConsumableType type;
  const char* name;
  int unlock_level;
  // What one proc costs a player who has not bought the pot outright.
  int64_t price;
  // Whether that proc is a second of farming. False for a pot charged on the
  // way into a boss fight instead.
  bool per_second;
  // What buying it outright costs, after which the price above is never
  // charged again.
  int64_t permanent_price;
  // What the pot is worth, one line each, as the Pot Info card lists them.
  // Written here rather than derived from the constants below: the card states
  // what the player gets, which is not always one lever.
  absl::Span<const char* const> effects;
};

// Every pot in the game, in the order they open.
absl::Span<const ConsumableInfo> AllConsumables();

// What `type` is, or null for a type no table row describes.
const ConsumableInfo* ConsumableInfoFor(ConsumableType type);

// What the Wealth Acquisition Potion is worth: a share added past the
// equipment cap, the same share added to drop rate, and a multiplier over the
// meso a kill pays once every share is summed.
inline constexpr double kWealthPotionMesoPct = 0.20;
inline constexpr double kWealthPotionDropPct = 0.20;
inline constexpr double kWealthPotionMesoMult = 1.20;

// What the Extreme Green Potion is worth: attack speed stages during a boss
// fight, and they are stages that may pass the soft cap.
inline constexpr int kGreenPotionAttackSpeed = 1;

}  // namespace ms

#endif  // MS_SRC_CHARACTER_CONSUMABLES_H_
