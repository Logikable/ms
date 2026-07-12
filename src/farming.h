/* AdvanceCombat drives the idle loop: it steps the fight over the current map
 * and banks the EXP, drops, and meso earned by the kills that step produced. It
 * sits above GameState (data), the encounter, and the fight itself, so none of
 * those carry reward behavior.
 */
#ifndef MS_SRC_FARMING_H_
#define MS_SRC_FARMING_H_

#include "src/combat/fight.h"
#include "src/game_state.h"

namespace ms {

// Advances `sim` by elapsed_seconds on `state`'s current map and grants the
// rewards for every mob it killed: their EXP, their drops, and their meso.
// No-op without a current map or an equipped weapon.
void AdvanceCombat(GameState& state, CombatSim& sim, double elapsed_seconds);

}  // namespace ms

#endif  // MS_SRC_FARMING_H_
