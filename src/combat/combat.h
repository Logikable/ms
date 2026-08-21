/* The combat module's front door: one call, made once per tick, that runs the
 * fight and pays the player for it.
 *
 * Everything else in src/combat/ is reachable from here. The encounter says
 * what is being fought, the fight steps it and reports kills, and loot prices
 * those kills; AdvanceCombat is the only place the three meet, and the only
 * place in the module that writes to the character.
 */
#ifndef MS_SRC_COMBAT_COMBAT_H_
#define MS_SRC_COMBAT_COMBAT_H_

#include <cstdint>

#include "src/combat/encounter.h"
#include "src/combat/fight.h"
#include "src/game_state.h"
#include "src/protos/mob.pb.h"

namespace ms {

// Advances `sim` by elapsed_seconds on `state`'s current map and grants the
// rewards for every mob it killed: their EXP, their drops, and their meso.
// No-op without a current map or an equipped weapon.
void AdvanceCombat(GameState& state, CombatSim& sim, double elapsed_seconds);

// The same, against params already built. `params` must be what
// ComputeCombatParams would return for `state` right now, so a caller has to
// rebuild them whenever the character or the map changes.
//
// For a caller stepping far faster than the game's tick: building the params
// walks every skill and prices every attack against every mob on the map, and
// none of that changes between two steps of the same fight. The game itself
// has no use for this -- it ticks 3 times a second -- but a sim stepping at
// 0.1s spends almost all of its time here. See //analysis:level_sim.
void AdvanceCombat(GameState& state, CombatSim& sim, const CombatParams& params,
                   double elapsed_seconds);

// Hands `count` copies of one rolled drop to the character and returns how
// many of them the bag had room for. A drop names either a stackable or an
// equip, so this asks which and takes the matching path; a name neither
// catalog knows is skipped rather than guessed at.
//
// Shared with the boss runs, which pay a cleared fight's table through it.
int64_t GrantDrop(GameState& state, const MobDrop& drop, int64_t count);

}  // namespace ms

#endif  // MS_SRC_COMBAT_COMBAT_H_
