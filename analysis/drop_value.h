/* Values drops in meso, the currency everything else is priced in.
 *
 * Most late-game drops sell for nothing: tokens, soul shards, symbol
 * duplicates, boss gear. Valued at their shop price, they're all worth zero, so
 * +100% item drop rate would measure no better than none, and no plan would
 * ever buy a drop line.
 *
 * Their real worth is what the character would otherwise spend to buy the same
 * combat power. The gear shopper already knows that rate: the best combat power
 * per meso on its shelf. Inverted, it turns power into meso, which puts a drop
 * and a star in the same currency.
 *
 * Every case has one shape: the value of what the drop leads to, minus what
 * finishing it still costs, divided by how many drops that takes. A gear drop
 * takes one and costs nothing more. A token takes as many as the shelf asks. A
 * symbol duplicate takes a whole level's worth, and the level-up charges meso
 * too.
 */
#ifndef MS_ANALYSIS_DROP_VALUE_H_
#define MS_ANALYSIS_DROP_VALUE_H_

#include <map>
#include <string>

#include "analysis/yardstick.h"
#include "src/character/character_stats.h"
#include "src/game_state.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/mob.pb.h"

namespace ms {

// What drops are measured against for the character as they are now. Computed
// once and shared by every drop, since rebuilding it per drop would scan the
// gear each time.
struct DropBasis {
  DerivedStats derived;
  // Stats from everything worn plus everything granted, before percentages are
  // applied. This is what TotalEquipStats sums, not its result.
  EquipStats worn;
  // The fight a drop is judged against, and the character's current damage
  // there. See //analysis:yardstick.
  Yardstick yard;
  double power = 0.0;
  // Damage per meso from GearShopper's best offer. Zero values every drop that
  // doesn't sell as nothing, which is what a caller without a shopper gets.
  double power_per_meso = 0.0;
  // Value of one of each token, keyed by the catalog key the shelf uses.
  // Computed once because it scans every equip the shop stocks, and rates are
  // read far more often than gear changes.
  std::map<std::string, double> tokens;
};

DropBasis DropBasisFor(const GameState& state, double power_per_meso,
                       HeldYardstick& held);

// Value of one copy of `proto`. A piece the character wouldn't wear (blocked by
// job or level, or no better than what's in the slot) is worth nothing, since
// wearing it is the only use of a gear drop.
double EquipDropValue(const GameState& state, const DropBasis& basis,
                      const EquipPrototype& proto);

// Value of one copy of the item under `key`: its shop price if it has one,
// otherwise the value of what it leads to. A token is worth the best piece its
// shelf sells divided by that piece's token price.
double ItemDropValue(const DropBasis& basis, const std::string& key,
                     const ItemPrototype& proto);

// Value of everything one kill of `mob` drops, before drop rate. The mob's meso
// isn't included; that channel has its own cap and is counted separately (see
// //analysis:meso_rate).
double DropsPerKill(const GameState& state, const DropBasis& basis,
                    const Mob& mob);

}  // namespace ms

#endif  // MS_ANALYSIS_DROP_VALUE_H_
