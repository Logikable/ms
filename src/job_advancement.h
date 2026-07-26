/* Performing a job advancement: the choice a player makes at level 10, and
 * everything that follows from it. Kept out of CharacterInstance because the
 * starting gear comes from the equip catalog, which the character cannot see.
 */
#ifndef MS_SRC_JOB_ADVANCEMENT_H_
#define MS_SRC_JOB_ADVANCEMENT_H_

#include <string>
#include <vector>

#include "src/game_state.h"
#include "src/protos/character.pb.h"

namespace ms {

// The equip catalog keys a character is handed on advancing into `job`, or an
// empty list for a job with no starting gear defined.
std::vector<std::string> StarterEquipsFor(Job job);

// Advances the character into `job`: the advancement itself, the AP reset that
// re-seats the stats on the new job's primary, and the starting weapons. The
// weapons land in the bag rather than on the character, so the player's first
// act as a Swordman is to equip one.
void PerformJobAdvancement(GameState& state, Job job);

}  // namespace ms

#endif  // MS_SRC_JOB_ADVANCEMENT_H_
