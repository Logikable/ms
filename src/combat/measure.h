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
#include <vector>

#include "src/combat/encounter.h"

namespace ms {

// The HP to stand a monster up with for a measurement. Its monsters never
// fall, so the one thing its HP still decides is how long a held swing is
// worth holding -- and a monster that cannot be killed has to look it. Stood
// up with its real HP instead, a hold is let go the moment the dummy would
// have died and the sim reads a fraction of what the skill is worth.
inline constexpr int64_t kMeasuredMobHp = 1'000'000'000'000'000;

// What a measured run came to.
struct Sequence {
  // Everything that landed over the run, and how long it ran for. The rate is
  // one over the other -- summons, burns and triggered releases included, so
  // nothing has to be added to it afterwards.
  double damage = 0.0;
  double seconds = 0.0;
  int main_attack = -1;  // index of the one swung most often, -1 for none
  // What each swing came to, parallel to CombatParams::attacks, with a burn
  // credited to the swing that lit it. Sums with own_clock_damage to `damage`,
  // so a share is one entry over that.
  std::vector<double> damage_by_attack;
  // What everything on a clock of its own came to: the summons, the releases
  // clocked by swings or by defeats, and what a reflection put back.
  double own_clock_damage = 0.0;
  // Share of the run each of the character's buffs spent standing, parallel to
  // CombatParams::buffs.
  std::vector<double> buff_uptime;
};

// Plays `params` out for `horizon` seconds against `enemies` monsters of its
// first type, and reports what landed.
//
// `horizon` is in the STRETCHED clock -- the one every duration inside
// CombatParams is written in, GameSpeedFactor times the game's own. At level
// 200 that factor is 10, so a two-minute cooldown reads 1200 here and a
// horizon under it is a burst window with every timed buff up for the whole of
// it. A caller working in game seconds must multiply by GameSpeedFactor first,
// or its window means a different length at every level.
//
// A closed form cannot answer this once a cooldown exists -- what a skill is
// worth depends on what gets swung while it recharges, and on how much of a
// charge is already wound up when it returns. The buffs are the same problem
// again: a buff worth 25% that stands for half the run is not worth 12.5% of
// every swing, it is worth all of it to half of them.
Sequence MeasureFight(const CombatParams& params, double horizon,
                      int enemies = 1);

}  // namespace ms

#endif  // MS_SRC_COMBAT_MEASURE_H_
