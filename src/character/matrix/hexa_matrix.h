/* The 6th job's HEXA Matrix: what each node level costs.
 *
 * Every node goes to 30 and is bought with Sol Erda and Fragments (see
 * sol_erda.h). GMS prices each kind on its own ladder, published per level;
 * the Mastery and Enhancement ladders are the Origin's halved and three
 * quartered, rounded up per level. A whole matrix (Origin, Ascent, four
 * Masteries, four Enhancements, Sol Janus, Sol Hecate and the line's common
 * node) costs 1,672 Sol Erda and 48,011 Fragments.
 *
 * There is no reset: every node is bought the same way whatever the order, so
 * one would only change the order.
 */
#ifndef MS_SRC_CHARACTER_HEXA_MATRIX_H_
#define MS_SRC_CHARACTER_HEXA_MATRIX_H_

#include <cstdint>

#include "src/protos/skill.pb.h"

namespace ms {

inline constexpr int kHexaNodeMaxLevel = 30;

// A price in both currencies, energy in thousandths of a Sol Erda as the
// character holds it.
struct HexaPrice {
  int64_t energy = 0;
  int64_t fragments = 0;
};

// The max level of a node of `kind`: 30, or 0 for a skill that isn't a node.
int MaxHexaNodeLevel(HexaNodeKind kind);

// The price of reaching `level` from the level below. An Origin's first level
// is free.
HexaPrice HexaNodeStepPrice(HexaNodeKind kind, int level);

// The price of raising a node of `kind` from `from` to `to`. Zero if `to`
// isn't higher than `from`.
HexaPrice HexaNodePrice(HexaNodeKind kind, int from, int to);

}  // namespace ms

#endif  // MS_SRC_CHARACTER_HEXA_MATRIX_H_
