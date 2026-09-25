/* Measuring a character instead of playing them: what they take off a crowd
 * per second, and which of their swings did it.
 *
 * A sim comparing two builds cannot use a fight. A fight empties the map and
 * then measures the respawn beat, its rolls put noise between two answers that
 * differ by less than the noise, and it ends. So the fight is asked the
 * question a different way -- monsters that never fall, rolls that land their
 * mean, and a horizon instead of a clear. That is all CombatParams::measuring
 * does, and it is why nothing here re-implements a single combat rule.
 */
#ifndef MS_SRC_COMBAT_MEASURE_H_
#define MS_SRC_COMBAT_MEASURE_H_

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "src/combat/encounter.h"

namespace ms {

// The HP to stand a measurement's monster up with. Its monsters never fall, so
// all its HP decides is how long a hold is worth holding -- and one stood up
// with its real HP is let go the moment it would have died, reading a fraction
// of what the skill is worth.
inline constexpr int64_t kMeasuredMobHp = 1'000'000'000'000'000;

// What a measured run came to.
struct Sequence {
  // Everything that landed and how long it ran for. The rate is one over the
  // other, summons and burns included, so nothing is added afterwards.
  double damage = 0.0;
  double seconds = 0.0;
  int main_attack = -1;  // index of the one swung most often, -1 for none
  // What each of params.attacks came to, a burn credited to the swing that lit
  // it. Sums with own_clock_damage to `damage`.
  std::vector<AttackTally> by_attack;
  // What everything on a clock of its own came to: the summons, the releases
  // clocked by swings or by defeats, and what a reflection put back.
  double own_clock_damage = 0.0;
  // The same total by source, named and heaviest first. A summon's clock is
  // what makes a branch's damage hard to read off its swings.
  std::vector<std::pair<std::string, double>> own_clock_by_source;
  // Share of the run each of the character's buffs spent standing, parallel to
  // CombatParams::buffs.
  std::vector<double> buff_uptime;
};

// Plays `params` out for `horizon` seconds against `enemies` monsters of its
// first type, and reports what landed.
//
// `horizon` is in the STRETCHED clock every duration in CombatParams is
// written in -- GameSpeedFactor times the game's own. At level 230 that factor
// is 10, so a two-minute cooldown reads 1200 and a shorter horizon is a burst
// window with every buff up throughout. A caller working in game seconds must
// multiply by GameSpeedFactor first.
//
// No closed form can answer this once a cooldown exists: what a skill is worth
// depends on what is swung while it recharges. A buff worth 25% standing for
// half the run is not worth 12.5% of every swing, it is worth all of it to
// half of them.
Sequence MeasureFight(const CombatParams& params, double horizon,
                      int enemies = 1);

}  // namespace ms

#endif  // MS_SRC_COMBAT_MEASURE_H_
