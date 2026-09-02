/* One line of a drop table, read against the catalogs: what it is called, and
 * whether it is the prize the fight was walked into for.
 *
 * A MobDrop names a catalog key and a rate and nothing else, so every question
 * about what it actually is comes back here. Paying one out is combat.h's
 * GrantDrop; this side only reads.
 */
#ifndef MS_SRC_COMBAT_DROP_H_
#define MS_SRC_COMBAT_DROP_H_

#include <string>

#include "src/game_state.h"
#include "src/protos/mob.pb.h"

namespace ms {

// The name the player knows a drop by, or empty for one neither catalog has
// heard of. The drop names a catalog key; what is shown is the prototype's own
// name.
std::string DropName(const GameState& state, const MobDrop& drop);

// Whether a drop is what the player came for: a piece of gear, or the token a
// shop trades for one. Everything else -- the soul shard and its like -- is
// what a clear pays whoever cleared it.
//
// The reward lists rule the two apart, so a prize is never read out of the
// middle of the numbers above it.
bool DropIsPrize(const GameState& state, const MobDrop& drop);

}  // namespace ms

#endif  // MS_SRC_COMBAT_DROP_H_
