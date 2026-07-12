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

#include "src/combat/fight.h"
#include "src/game_state.h"

namespace ms {

// Advances `sim` by elapsed_seconds on `state`'s current map and grants the
// rewards for every mob it killed: their EXP, their drops, and their meso.
// No-op without a current map or an equipped weapon.
void AdvanceCombat(GameState& state, CombatSim& sim, double elapsed_seconds);

}  // namespace ms

#endif  // MS_SRC_COMBAT_COMBAT_H_
