/* AdvanceFarming drives the idle farming loop: it composes the combat model
 * over the current map and banks the resulting EXP, drops, and meso onto the
 * character. It sits above GameState (data) and combat (pure math), so neither
 * of those carries farming behavior.
 */
#ifndef MS_SRC_FARMING_H_
#define MS_SRC_FARMING_H_

#include "src/game_state.h"

namespace ms {

// Applies elapsed_seconds of farming on `state`'s current map: banks whole
// kills per mob type, grants their EXP, and accrues their drops and meso.
// No-op without a current map or an equipped weapon.
void AdvanceFarming(GameState& state, double elapsed_seconds);

}  // namespace ms

#endif  // MS_SRC_FARMING_H_
