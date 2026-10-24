/* HEXA Stats: three nodes of three stats each, bought with Sol Erda
 * Fragments after the 6th job. GMS opens the nodes at 260, 265 and 270; this
 * game has no 6th job, so the level alone opens them.
 *
 * A node levels 20 times, each time raising one of its three stats a level,
 * none past 10. The primary stat pays the wiki's primary column, the other two
 * its additional column, roughly half. No two nodes share a primary, and an
 * additional stat appears at most twice across them.
 */
#ifndef MS_SRC_CHARACTER_HEXA_STAT_H_
#define MS_SRC_CHARACTER_HEXA_STAT_H_

#include "src/protos/character.pb.h"

namespace ms {

inline constexpr int kHexaStatNodes = 3;
inline constexpr int kHexaStatMaxLevel = 10;
inline constexpr int kHexaStatNodeLevels = 20;

// The level each node opens at, in order.
inline constexpr int kHexaStatNodeUnlockLevels[kHexaStatNodes] = {260, 265,
                                                                  270};

// How many of `nodes` a character of `level` may use.
int HexaStatNodesOpen(int level);

// What one stat at `level` is worth, as a primary or an additional stat. A
// fraction for the percentages, a whole number for attack and the main stat.
double HexaStatValue(HexaStat stat, int level, bool primary);

// Whether `node` is one GMS allows: three different stats, none past 10, no
// more than 20 levels in all.
bool HexaStatNodeValid(const HexaStatNode& node);

// Whether `stats` keeps GMS's rules across nodes: no primary used twice, no
// additional stat on more than two nodes, and every node valid.
bool HexaStatsAllowed(const HexaStats& stats);

// What every open, valid node adds, summed. The main stat is the job's
// primary stat, a final stat like a Hyper Stat's.
struct HexaStatTotals {
  double crit_dmg = 0.0;
  double boss_pct = 0.0;
  double ied = 0.0;  // ignored defense, combined like any other source
  double damage_pct = 0.0;
  int attack = 0;
  int main_stat = 0;
};
HexaStatTotals HexaStatTotalsFor(const HexaStats& stats, int level);

}  // namespace ms

#endif  // MS_SRC_CHARACTER_HEXA_STAT_H_
