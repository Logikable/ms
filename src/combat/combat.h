/* Entry point for combat: called once per tick to run the fight and pay the
 * player.
 *
 * The encounter says what is being fought, the fight steps it and reports
 * kills, and loot prices those kills. AdvanceCombat ties the three together and
 * is the only code in this module that writes to the character.
 */
#ifndef MS_SRC_COMBAT_COMBAT_H_
#define MS_SRC_COMBAT_COMBAT_H_

#include <cstdint>
#include <string>
#include <vector>

#include "src/combat/encounter.h"
#include "src/combat/fight.h"
#include "src/game_state.h"
#include "src/protos/mob.pb.h"

namespace ms {

// One item earned from fighting. `count` is the number of items, not stacks.
struct RewardItem {
  std::string name;
  int64_t count = 0;
  // How many of those were thrown away because the bag was full.
  int64_t discarded = 0;
};

// What a stretch of fighting paid. Callers that don't display it can ignore it.
struct RewardTally {
  int64_t exp = 0;
  int64_t meso = 0;
  int64_t honor = 0;
  int64_t v_points = 0;
  std::vector<RewardItem> items;  // in drop-table order
  // Potion cost taken out of meso. Listed here because it is charged per second
  // of farming.
  int64_t consumable_cost = 0;
};

// Advances `sim` on `state`'s map and pays for every mob killed. Returns what
// was paid. Does nothing without a map or an equipped weapon.
RewardTally AdvanceCombat(GameState& state, CombatSim& sim,
                          double elapsed_seconds);

// Same, but reuses `params` instead of rebuilding them. They must match what
// ComputeCombatParams would return now, so rebuild them whenever the character
// or map changes. Sims use this because building params is slow.
RewardTally AdvanceCombat(GameState& state, CombatSim& sim,
                          const CombatParams& params, double elapsed_seconds);

// Pays for `kills` of each of `params`' mob types and returns what was paid.
// Used by both the live tick and offline progress. Rolls are batched, so a
// million kills cost the same as one and give the same distribution.
RewardTally AwardCombatRewards(GameState& state, const CombatParams& params,
                               const std::vector<int64_t>& kills);

// Gives `count` copies of one drop and returns how many fit in the bag. Skips
// names that no catalog knows. Boss clears also pay through this.
int64_t GrantDrop(GameState& state, const MobDrop& drop, int64_t count);

}  // namespace ms

#endif  // MS_SRC_COMBAT_COMBAT_H_
