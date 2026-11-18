/* HEXA Stats: three nodes of three stats each, opened with Sol Erda and
 * Fragments after the 6th job at 260, 265 and 270, each once the one before
 * is full.
 *
 * A node levels 20 times on Fragments, each time raising one of its three
 * stats a level, none past 10, at random: GMS's odds favour the additional
 * stats more as the primary climbs. The primary stat pays the wiki's primary
 * column, the other two its additional column, roughly half. No two nodes
 * share a primary, and an additional stat appears at most twice across them.
 *
 * GMS charges meso to reset a node (from level 10) and to reselect its stats.
 * Here both are free; a reset still throws away the Fragments the levels cost.
 */
#ifndef MS_SRC_CHARACTER_HEXA_STAT_H_
#define MS_SRC_CHARACTER_HEXA_STAT_H_

#include <cstdint>
#include <random>

#include "src/protos/character.pb.h"

namespace ms {

inline constexpr int kHexaStatNodes = 3;
inline constexpr int kHexaStatMaxLevel = 10;
inline constexpr int kHexaStatNodeLevels = 20;

// The level each node opens at, in order.
inline constexpr int kHexaStatNodeUnlockLevels[kHexaStatNodes] = {260, 265,
                                                                  270};

// A node's total levels before it can be reset.
inline constexpr int kHexaStatResetLevel = 10;

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

// What opening node `index` (0 for the first) costs, from the wiki: 5 Sol Erda
// and 10 Fragments, then 10 and 200, then 15 and 350. Energy is in
// thousandths of a Sol Erda, as the character holds it.
struct HexaStatPrice {
  int64_t energy = 0;
  int64_t fragments = 0;
};
HexaStatPrice HexaStatOpenPrice(int index);

// Whether a character of `level` and `job_stage` holding `stats` may open the
// next node: the 6th job, the node's level, and the node before it full.
bool HexaStatNextNodeOpens(const HexaStats& stats, int level, int job_stage);

// The levels a node has, across its three stats.
int HexaStatNodeLevels(const HexaStatNode& node);

// The Fragments the next level of `node` costs, set by its primary's level:
// 10 to primary 2, 20 to 6, then 30, 40 and 50. The wiki gives no price once
// the primary is at 10, so 50 stands. 0 for a full node.
int64_t HexaStatEnhancePrice(const HexaStatNode& node);

// The chance the next level goes to the primary at `primary_level`: 35% to
// level 2, 20% to 6, then 15%, 10%, 5%, and none at 10. The rest is split
// evenly between the additional stats that have room.
double HexaStatPrimaryChance(int primary_level);

// Raises one stat of `node` by GMS's odds. False, changing nothing, for a full
// node.
bool RollHexaStatLevel(HexaStatNode& node, std::mt19937& rng);

}  // namespace ms

#endif  // MS_SRC_CHARACTER_HEXA_STAT_H_
