/* Measures a character's damage per second against a crowd, and which attacks
 * dealt it, instead of playing a real fight.
 *
 * A real fight is a poor way to compare two builds: it clears the map and then
 * only measures the respawn rate, its random rolls add more noise than the
 * difference being measured, and it ends. So measurement mode changes the fight
 * instead: monsters never die, rolls always land their average, and the run
 * lasts a fixed time. CombatParams::measuring does exactly that, so no combat
 * rule is reimplemented here.
 */
#ifndef MS_SRC_COMBAT_MEASURE_H_
#define MS_SRC_COMBAT_MEASURE_H_

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "src/combat/encounter.h"

namespace ms {

// HP for a measured monster. Measured monsters never die, so HP only matters
// for held skills, which stop when their target would have died. A monster with
// its real HP would cut them short and undercount them.
inline constexpr int64_t kMeasuredMobHp = 1'000'000'000'000'000;

// The result of a measured run.
struct Sequence {
  // Total damage and run length. Damage includes summons and burns, so the rate
  // is simply one divided by the other.
  double damage = 0.0;
  double seconds = 0.0;
  int main_attack = -1;  // the most-used attack, or -1 for none
  // Damage per entry in params.attacks, with burns credited to the attack that
  // applied them. Adds up to `damage` together with own_clock_damage.
  std::vector<AttackTally> by_attack;
  // Damage from everything on its own timer: summons, skills triggered by
  // attacks or kills, and reflected damage.
  double own_clock_damage = 0.0;
  // The same total by source, largest first. Summons make a job's damage hard
  // to read from its attacks alone.
  std::vector<std::pair<std::string, double>> own_clock_by_source;
  // Fraction of the run each buff was active, parallel to CombatParams::buffs.
  std::vector<double> buff_uptime;
};

// Runs `params` for `horizon` seconds against `enemies` monsters of its first
// type, and reports the damage dealt.
//
// `horizon` uses the stretched clock that all CombatParams durations use: game
// seconds times GameSpeedFactor. At level 230 that factor is 10, so a
// two-minute cooldown is 1200, and a shorter horizon measures a burst window
// with every buff up. Callers working in game seconds must multiply by
// GameSpeedFactor.
//
// This has to be simulated because cooldowns interact: a skill's value depends
// on what else is used while it recharges. A 25% buff active half the time
// isn't worth 12.5% on every attack; it's worth 25% on half of them.
Sequence MeasureFight(const CombatParams& params, double horizon,
                      int enemies = 1);

}  // namespace ms

#endif  // MS_SRC_COMBAT_MEASURE_H_
