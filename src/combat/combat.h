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

// One item a stretch of fighting yielded, for a caller reporting it. `count`
// is in units rather than stacks -- fifty full stacks of a drop read as ten
// thousand of it, which is what the player wants to know they picked up.
struct RewardItem {
  std::string name;
  int64_t count = 0;
  // How many of those the bag had no room for and were thrown away.
  int64_t discarded = 0;
};

// What a stretch of fighting paid: the EXP, the meso, and the items. Filled
// by AwardCombatRewards for whoever wants to show it; a caller with nothing to
// show drops it on the floor.
struct RewardTally {
  int64_t exp = 0;
  int64_t meso = 0;
  std::vector<RewardItem> items;  // in the order the drop tables list them
};

// Advances `sim` by elapsed_seconds on `state`'s current map and grants the
// rewards for every mob it killed: their EXP, their drops, and their meso.
// Returns what that step paid, for a caller measuring the map. No-op without a
// current map or an equipped weapon.
RewardTally AdvanceCombat(GameState& state, CombatSim& sim,
                          double elapsed_seconds);

// The same, against params already built. `params` must be what
// ComputeCombatParams would return for `state` right now, so a caller has to
// rebuild them whenever the character or the map changes.
//
// For a caller stepping far faster than the game's tick: building the params
// walks every skill and prices every attack against every mob on the map, and
// none of that changes between two steps of the same fight. The game itself
// has no use for this -- it ticks 3 times a second -- but a sim stepping at
// 0.1s spends almost all of its time here. See //analysis:progression_sim.
RewardTally AdvanceCombat(GameState& state, CombatSim& sim,
                          const CombatParams& params, double elapsed_seconds);

// Pays `kills` of each of `params`' mob types -- their EXP, their meso, their
// drops -- and returns what was handed over. `kills` is indexed to match
// params.types.
//
// Shared by the live tick, which pays for one step, and offline progress,
// which pays for hours in one call. The rolls are batched either way (see
// loot.h), so paying for a million kills at once costs no more than paying
// for one and gives the same distribution.
RewardTally AwardCombatRewards(GameState& state, const CombatParams& params,
                               const std::vector<int64_t>& kills);

// Hands `count` copies of one rolled drop to the character and returns how
// many of them the bag had room for. A drop names either a stackable or an
// equip, so this asks which and takes the matching path; a name neither
// catalog knows is skipped rather than guessed at.
//
// Shared with the boss runs, which pay a cleared fight's table through it.
int64_t GrantDrop(GameState& state, const MobDrop& drop, int64_t count);

}  // namespace ms

#endif  // MS_SRC_COMBAT_COMBAT_H_
