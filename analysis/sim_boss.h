/* Runs a boss fight from a sim: the same fight the screen runs, stepped to the
 * end without display.
 */
#ifndef MS_ANALYSIS_SIM_BOSS_H_
#define MS_ANALYSIS_SIM_BOSS_H_

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "src/game_state.h"
#include "src/protos/boss.pb.h"
#include "src/protos/mob.pb.h"

namespace ms {

// Result of one fight attempt.
struct BossOutcome {
  bool won = false;
  // Seconds the attempt took, won or lost. A loss may end before time runs out,
  // since a player leaves a fight that is clearly going nowhere.
  double seconds = 0.0;
  // Fraction of the fight's starting HP still left when it ended; 0 for a
  // clear. Unreached phases count in full, so a build that died in the first of
  // three phases reads near 1.0.
  double left = 0.0;
};

// Fights `difficulty_index` of `boss_key` and returns the outcome, collecting
// the clear's rewards. Gives up once the fight is clearly lost. The caller must
// have filled state.bosses.
BossOutcome FightBoss(GameState& state, const std::string& boss_key,
                      int difficulty_index);

// Total HP of `difficulty` across all its phases.
int64_t BossTotalHp(const std::map<std::string, Mob>& mobs,
                    const BossDifficulty& difficulty);

// Highest defence of anything in the fight, which is what every Ignore DEF
// lever is measured against.
int BossPdr(const std::map<std::string, Mob>& mobs,
            const BossDifficulty& difficulty);

// Catalog keys of what the difficulty drops. Leave these off a character used
// to measure the fight, since it can't need gear that only the fight gives out.
std::set<std::string> BossOwnDrops(const BossDifficulty& difficulty);

// The phase with the most HP, which skill spending aims to beat. A fight is
// decided by its heaviest phase, not by Zakum's arms.
int BossObjectivePhase(const std::map<std::string, Mob>& mobs,
                       const BossDifficulty& difficulty);

// Index of the difficulty called `name`, or 0 for an empty name. -1 if the boss
// has no such difficulty.
int BossDifficultyIndex(const Boss& boss, const std::string& name);

// Fights open at `level`, as boss key and difficulty, lowest unlock first. Only
// difficulties the game has built: one marked coming soon is an empty shell
// with only HP.
std::vector<std::pair<std::string, int>> UnlockedBosses(const GameState& state,
                                                        int level);

// The fight every plan aims at: the hardest one open to the character, or the
// next to open if none are. Returns false if the catalog has no fight they
// could ever reach.
bool AimedFight(const GameState& state, std::pair<std::string, int>* fight);

// Defence of the aimed fight, as a fraction; 0 with no fight, which values
// ignored-defence levers at nothing.
double AimedDefence(const GameState& state);

}  // namespace ms

#endif  // MS_ANALYSIS_SIM_BOSS_H_
