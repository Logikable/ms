/* Looks up a drop table entry in the item catalogs: its display name, and
 * whether it is a prize.
 *
 * A MobDrop holds only a catalog key and a rate, so this file answers what it
 * actually is. Paying it out is GrantDrop in combat.h.
 */
#ifndef MS_SRC_COMBAT_DROP_H_
#define MS_SRC_COMBAT_DROP_H_

#include <string>

#include "src/game_state.h"
#include "src/protos/mob.pb.h"

namespace ms {

// The drop's display name, or empty if neither catalog knows it.
std::string DropName(const GameState& state, const MobDrop& drop);

// Whether a drop is a prize: gear, or a token a shop trades for gear.
// Everything else, such as soul shards, is a regular clear reward.
//
// Reward lists show prizes separately, so they don't get lost among the rest.
bool DropIsPrize(const GameState& state, const MobDrop& drop);

}  // namespace ms

#endif  // MS_SRC_COMBAT_DROP_H_
