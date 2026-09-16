/* Performing a job advancement: the choice a player makes at level 10, and
 * everything that follows from it. Kept out of CharacterInstance because
 * handing the gear over needs the equip catalog, which the character cannot
 * see. Which gear that is answers from character.h -- see StarterEquipsFor.
 */
#ifndef MS_SRC_CHARACTER_JOB_ADVANCEMENT_H_
#define MS_SRC_CHARACTER_JOB_ADVANCEMENT_H_

#include "src/game_state.h"
#include "src/protos/character.pb.h"

namespace ms {

// Advances into `job`: the advancement, the starting gear, and on the FIRST
// advancement the AP reset that re-seats the stats. The gear lands in the bag,
// so the player's first act as a Swordman is to equip one.
void PerformJobAdvancement(GameState& state, Job job);

}  // namespace ms

#endif  // MS_SRC_CHARACTER_JOB_ADVANCEMENT_H_
