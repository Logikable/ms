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
#include <string>
#include <vector>

#include "src/combat/encounter.h"
#include "src/combat/fight.h"
#include "src/game_state.h"
#include "src/protos/mob.pb.h"

namespace ms {

// One item a stretch of fighting yielded. `count` is in UNITS rather than
// stacks: fifty full stacks read as ten thousand of the drop.
struct RewardItem {
  std::string name;
  int64_t count = 0;
  // How many of those the bag had no room for and were thrown away.
  int64_t discarded = 0;
};

// What a stretch of fighting paid. Filled by AwardCombatRewards for whoever
// wants to show it; a caller with nothing to show drops it.
struct RewardTally {
  int64_t exp = 0;
  int64_t meso = 0;
  int64_t honor = 0;
  int64_t v_points = 0;
  std::vector<RewardItem> items;  // in the order the drop tables list them
  // What the potions took back out of the purse. A cost rather than a
  // payment, and here because it is charged by the SECOND of farming.
  int64_t consumable_cost = 0;
};

// Advances `sim` on `state`'s map and grants the rewards for every mob killed.
// Returns what the step paid. No-op without a map or an equipped weapon.
RewardTally AdvanceCombat(GameState& state, CombatSim& sim,
                          double elapsed_seconds);

// The same against params already built, which must be what
// ComputeCombatParams would return right now -- so a caller rebuilds them
// whenever the character or the map changes.
//
// For a caller stepping far faster than the game's tick: building the params
// prices every attack against every mob and nothing in it changes between two
// steps of one fight. A sim stepping at 0.1s spends almost all its time
// here.
RewardTally AdvanceCombat(GameState& state, CombatSim& sim,
                          const CombatParams& params, double elapsed_seconds);

// Pays `kills` of each of `params`' mob types and returns what was handed
// over. Shared by the live tick and offline progress, which pays for hours in
// one call: the rolls are batched, so a million kills cost no more than one
// and give the same distribution.
RewardTally AwardCombatRewards(GameState& state, const CombatParams& params,
                               const std::vector<int64_t>& kills);

// Hands `count` copies of one rolled drop over and returns how many the bag
// had room for. A name neither catalog knows is skipped rather than guessed
// at. Shared with the boss runs, which pay a cleared table through it.
int64_t GrantDrop(GameState& state, const MobDrop& drop, int64_t count);

}  // namespace ms

#endif  // MS_SRC_COMBAT_COMBAT_H_
