/* Driving a boss fight from a sim: the fight the screen runs, stepped to the
 * end with nobody watching.
 *
 * Shared by the sims that put a character in front of a boss -- one asking
 * whether a build can win at all, another asking what a daily clear pays --
 * so both meet the fight the same way.
 */
#ifndef MS_ANALYSIS_SIM_BOSS_H_
#define MS_ANALYSIS_SIM_BOSS_H_

#include <cstdint>
#include <map>
#include <set>
#include <string>

#include "src/game_state.h"
#include "src/protos/boss.pb.h"
#include "src/protos/mob.pb.h"

namespace ms {

// What one run of a fight came to.
struct BossOutcome {
  bool won = false;
  // Seconds the clear took, 0 for a run that never finished.
  double seconds = 0.0;
};

// Fights `difficulty_index` of `boss_key` to the end and reports it, paying
// the character whatever the clear was worth. The same BossRun the screen
// steps, at the same sixty frames a second.
//
// `limit_seconds` stands in for the fight's own clock, so a build that misses
// it still says by how much rather than only that it did. 0 keeps the clock
// the data gives it, which is the fight as a player meets it.
//
// Reads the boss out of `state`, so the caller must have filled state.bosses.
BossOutcome FightBoss(GameState& state, const std::string& boss_key,
                      int difficulty_index, double limit_seconds = 0.0);

// Everything `difficulty` is holding, over all its phases.
int64_t BossTotalHp(const std::map<std::string, Mob>& mobs,
                    const BossDifficulty& difficulty);

// The stiffest defence anything in the fight stands behind, which is what
// every Ignore DEF lever in the books is worth against it.
int BossPdr(const std::map<std::string, Mob>& mobs,
            const BossDifficulty& difficulty);

// What the difficulty pays, as catalog keys. Left off a character measuring
// the fight: it cannot be beaten in gear only it hands out.
std::set<std::string> BossOwnDrops(const BossDifficulty& difficulty);

// The phase a book is spent to beat: the one holding the most HP. A fight is
// decided against its heaviest phase, and Zakum's arms are not it.
int BossObjectivePhase(const std::map<std::string, Mob>& mobs,
                       const BossDifficulty& difficulty);

// The index of the difficulty called `name`, or 0 for an empty name. -1 when
// the boss has no such difficulty.
int BossDifficultyIndex(const Boss& boss, const std::string& name);

}  // namespace ms

#endif  // MS_ANALYSIS_SIM_BOSS_H_
