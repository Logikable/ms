/* V Points: the currency the 5th job's V Matrix is bought with.
 *
 * A 5th job earns no SP. What levels a node instead is V Points, and monsters
 * are the only thing that pays them -- a small chance at one per kill, lifted
 * by item drop rate the way a drop is, because that is what they are. Bosses
 * pay nothing.
 *
 * Only Arcane River monsters carry any, the map's Arcane Force requirement
 * being what marks the river. Nothing else gates them: a character who farms
 * there before the 5th advancement banks points they cannot yet see, the way
 * honor accrues before the sheet names it.
 *
 * Pure math over the numbers, like honor.h. Adding the points to the character
 * is the caller's.
 */
#ifndef MS_SRC_CHARACTER_V_MATRIX_H_
#define MS_SRC_CHARACTER_V_MATRIX_H_

#include <cstdint>
#include <random>

#include "src/protos/skill.pb.h"

namespace ms {

// The share of kills that pay a V Point, before item drop rate lifts it. Set
// against what a whole matrix costs and how fast the endgame kills: 4,505
// points is the bill, and a kill lands about every 2.7 seconds.
inline constexpr double kVPointDropChance = 0.001;

// What one kill is worth on average at `item_drop_pct` (0.20 == +20%), for a
// sim reading the rate rather than rolling it.
double VPointsPerKill(double item_drop_pct);

// V Points `kills` actually paid. One roll over the batch, as the meso and the
// honor are: which kills paid is not a question anything downstream asks.
int64_t RollMobVPoints(int64_t kills, double item_drop_pct, std::mt19937& rng);

// How far a node of `kind` goes: thirty for a common or a job node, sixty for
// a boost. Zero for a skill that is not a node.
int MaxVNodeLevel(VNodeKind kind);

// What the step up to `level` costs. GMS's ladders climb by bands of ten: a
// job node's first level is free and a common's is 7, then both cost 4, 6 and
// 9. A boost costs one a level to forty and two past it.
int VNodeStepCost(VNodeKind kind, int level);

// What raising a node of `kind` from `from` to `to` costs altogether. Zero for
// a climb that goes nowhere or backwards.
int VNodeCost(VNodeKind kind, int from, int to);

}  // namespace ms

#endif  // MS_SRC_CHARACTER_V_MATRIX_H_
