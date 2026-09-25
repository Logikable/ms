/* Performing a job advancement: the choice a player makes at level 10, and
 * everything that follows from it. Kept out of CharacterInstance because
 * handing out gear needs the equip catalog, which the character can't see.
 * StarterEquipsFor in character.h decides which gear.
 */
#ifndef MS_SRC_CHARACTER_JOB_ADVANCEMENT_H_
#define MS_SRC_CHARACTER_JOB_ADVANCEMENT_H_

#include "src/game_state.h"
#include "src/protos/character.pb.h"

namespace ms {

// Advances into `job`: the advancement itself, the starting gear, and on the
// first advancement the AP reset. The gear goes in the bag, so the player's
// first act as a Swordman is to equip it.
void PerformJobAdvancement(GameState& state, Job job);

}  // namespace ms

#endif  // MS_SRC_CHARACTER_JOB_ADVANCEMENT_H_
