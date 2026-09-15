/* What a drop is worth to the purse, in the meso everything else is priced in.
 *
 * Most of what falls in the late game sells for nothing -- a token, a soul
 * shard, a symbol duplicate, a piece of boss gear. A rate that reads the
 * counter's price prices all of them at zero, so a character carrying +100%
 * item drop rate measures no better than one carrying none, and nothing the
 * rate feeds will ever buy a drop line.
 *
 * What they are really worth is what the purse would otherwise spend to buy
 * the same combat power, and the shopper already knows that rate: the best
 * combat power per meso on its own shelf. Run backwards it turns power into
 * meso, which is what puts a drop and a star in the same currency.
 *
 * Every case here has one shape -- what the drop ADVANCES toward, less what
 * finishing it still costs, spread over how many of it that takes. A gear drop
 * takes one of itself and costs nothing more. A token takes as many as the
 * shelf asks. A symbol duplicate takes the whole rung's worth, and the rung
 * charges meso on top.
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

// What the character as they stand is measured against, worked out once and
// handed to every drop: rebuilding it per drop would cost a pass over the gear
// for each one.
struct DropBasis {
  DerivedStats derived;
  // Everything worn plus everything granted, before any percentage is folded
  // in -- what TotalEquipStats sums, not what it answers.
  EquipStats worn;
  // The fight a drop is judged against, and what the character takes off it as
  // they stand. See //analysis:yardstick.
  Yardstick yard;
  double power = 0.0;
  // Damage a meso buys elsewhere, off GearShopper's best offer. Zero
  // prices every drop that does not sell at nothing, which is what a caller
  // with no shopper in hand gets.
  double power_per_meso = 0.0;
  // What one of each token is worth, by the catalog key the shelf names it by.
  // Worked out once because it takes a pass over every equip the shop stocks,
  // and a rate is asked for far more often than the gear moves.
  std::map<std::string, double> tokens;
};

DropBasis DropBasisFor(const GameState& state, double power_per_meso);

// What one copy of `proto` is worth. A piece the character would not wear --
// one their job or level shuts them out of, or one no better than what is
// already in the slot -- is worth nothing, since wearing it is the only thing
// a drop is for.
double EquipDropValue(const GameState& state, const DropBasis& basis,
                      const EquipPrototype& proto);

// What one copy of the item filed under `key` is worth: the counter's price
// where it has one, and otherwise what it advances toward. A token is worth
// the best piece its shelf sells divided by the tokens that piece asks for.
double ItemDropValue(const DropBasis& basis, const std::string& key,
                     const ItemPrototype& proto);

// What everything one kill of `mob` drops is worth, before drop rate lifts it.
// The meso the mob drops is NOT in it -- that channel has a cap of its own and
// is counted separately. See //analysis:meso_rate.
double DropsPerKill(const GameState& state, const DropBasis& basis,
                    const Mob& mob);

}  // namespace ms

#endif  // MS_ANALYSIS_DROP_VALUE_H_
