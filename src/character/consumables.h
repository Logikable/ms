/* The buffs: what each costs, what it gives, and when it unlocks.
 *
 * A buff is either rented or owned. A rented buff is charged its price every
 * time it is used (per second while farming, or on entering a boss fight), and
 * an owned one is never charged again. Either way the player switches it on and
 * off, and an owned buff switched off does nothing.
 *
 * A charge the purse can't cover takes what is there. The buff still works: a
 * player who runs out of meso gets it at a discount instead of losing it when
 * they can least afford to.
 */
#ifndef MS_SRC_CHARACTER_CONSUMABLES_H_
#define MS_SRC_CHARACTER_CONSUMABLES_H_

#include <cstdint>
#include <string>

#include "absl/types/span.h"
#include "src/protos/character.pb.h"

namespace ms {

// The level the first buff unlocks, which is when the tab appears.
inline constexpr int kConsumableUnlockLevel = 170;

// What one buff costs and when it unlocks. The price is charged per use, and
// what counts as a use differs by buff; see `per_second`.
struct ConsumableInfo {
  ConsumableType type;
  const char* name;
  int unlock_level;
  // The cost of one use for a player who hasn't bought the buff outright.
  int64_t price;
  // Whether a use is one second of farming. False for a buff charged on
  // entering a boss fight instead.
  bool per_second;
  // The price to buy it outright, after which `price` is never charged again.
  int64_t permanent_price;
  // What the buff gives, one line each, as the Buff Info card lists them.
  // Written out instead of derived from the constants below, because the card
  // says what the player gets, which isn't always a single lever.
  absl::Span<const char* const> effects;
};

// Every buff in the game, in unlock order.
absl::Span<const ConsumableInfo> AllConsumables();

// The info for `type`, or null if no row describes it.
const ConsumableInfo* ConsumableInfoFor(ConsumableType type);

// What the Wealth Acquisition Potion gives: a meso bonus past the equipment
// cap, the same bonus to drop rate, and a multiplier on the meso a kill pays
// once all bonuses are added.
inline constexpr double kWealthPotionMesoPct = 0.20;
inline constexpr double kWealthPotionDropPct = 0.20;
inline constexpr double kWealthPotionMesoMult = 1.20;

// What the Extreme Green Potion gives: extra attack speed stages during a boss
// fight, which can go past the soft cap.
inline constexpr int kGreenPotionAttackSpeed = 1;

// What the Wild Totem gives: the respawn interval it sets on a map, replacing
// kRespawnIntervalSeconds. It is half, so twice as many monsters appear and a
// player already killing everything the map spawns kills twice as many.
inline constexpr double kWildTotemRespawnSeconds = 3.78;

}  // namespace ms

#endif  // MS_SRC_CHARACTER_CONSUMABLES_H_
