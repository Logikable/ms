/* V Points: the currency for the 5th job's V Matrix.
 *
 * A 5th job earns no SP. Nodes are levelled with V Points instead, and only
 * monsters drop them: each kill has a small chance of one, raised by item drop
 * rate like any drop, because that is what they are. Bosses give none.
 *
 * Only monsters on maps that require a force (Arcane River and Grandis) drop
 * them. Nothing else gates them: a character who farms there before the 5th
 * advancement stores points they can't see yet, the same way honor builds up
 * before it is shown.
 *
 * Pure math, like honor.h; the caller adds the points to the character.
 */
#ifndef MS_SRC_CHARACTER_V_MATRIX_H_
#define MS_SRC_CHARACTER_V_MATRIX_H_

#include <cstdint>
#include <random>

#include "src/protos/skill.pb.h"

namespace ms {

// The chance a kill drops a V Point, before item drop rate. Balanced against
// the cost of a whole matrix and the endgame kill rate: 4,505 points in total,
// and a kill about every 2.7 seconds.
inline constexpr double kVPointDropChance = 0.001;

// The average V Points per kill at `item_drop_pct` (0.20 == +20%), for sims
// that use the rate instead of rolling.
double VPointsPerKill(double item_drop_pct);

// The V Points `kills` actually dropped. One roll for the whole batch, like
// meso and honor, since nothing later needs to know which kills paid.
int64_t RollMobVPoints(int64_t kills, double item_drop_pct, std::mt19937& rng);

// The max level of a node of `kind`: 30 for common and job nodes, 60 for boost
// nodes. Zero for a skill that isn't a node.
int MaxVNodeLevel(VNodeKind kind);

// The cost of reaching `level` from the level below. GMS's costs rise in bands
// of ten: a job node's first level is free and a common's costs 7, then both
// cost 4, 6 and 9 per level. A boost costs one per level up to 40 and two
// after.
int VNodeStepCost(VNodeKind kind, int level);

// The total cost of raising a node of `kind` from `from` to `to`. Zero if `to`
// isn't higher than `from`.
int VNodeCost(VNodeKind kind, int from, int to);

}  // namespace ms

#endif  // MS_SRC_CHARACTER_V_MATRIX_H_
