/* Chooses what the account spends its Legion points on.
 *
 * Like Hyper Stats (see hyper_plan.h), a point's value is measured on the
 * character rather than listed, so the job's own stats decide. Unlike them, a
 * Legion point costs the same at every level, so points go one at a time to
 * whichever stat the rating gains most from next, re-measured each time:
 * crit rate stops paying at the cap and Ignore Defense pays less as it piles
 * up.
 */
#ifndef MS_SRC_CHARACTER_LEGION_PLAN_H_
#define MS_SRC_CHARACTER_LEGION_PLAN_H_

#include <functional>

#include "src/character/character.h"
#include "src/character/stat_preset.h"

namespace ms {

// What the character is worth. Called on the character this file has just
// changed, so it must read the character it is given.
using LegionRate = std::function<double(CharacterInstance&)>;

// Spends every point the Legion has on `slot` of the character's copy of the
// Legion, discarding what was there; the caller hands it back to the account.
// A point nothing improves goes to the first stat with room, so none is left
// over. Returns the points spent: 0 while the Legion is locked.
int SpendLegion(CharacterInstance& character, StatPreset slot,
                const LegionRate& rate);

}  // namespace ms

#endif  // MS_SRC_CHARACTER_LEGION_PLAN_H_
