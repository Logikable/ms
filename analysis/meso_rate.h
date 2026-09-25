/* Meso earned per second of play, the unit every plan here ranks in.
 *
 * A faster kill means another kill's meso, so a meso rate already includes
 * damage, plus the drop and meso levers a damage rate misses.
 *
 * A mob's meso drop is a 60% chance that drop rate raises to certain and no
 * further, so it alone caps at +66.7%. Etc items sold from the same kill have
 * no such cap.
 */
#ifndef MS_ANALYSIS_MESO_RATE_H_
#define MS_ANALYSIS_MESO_RATE_H_

#include <map>
#include <string>
#include <vector>

#include "absl/types/span.h"
#include "analysis/drop_value.h"
#include "src/combat/encounter.h"
#include "src/game_state.h"
#include "src/protos/item.pb.h"
#include "src/protos/mob.pb.h"

namespace ms {

// What one kill pays, with drops already valued (see //analysis:drop_value).
// The character's %meso is left out, since it multiplies every mob equally.
double MesoPerKill(const Mob& mob, double drops, double item_drop_pct);

// The mobs the character is fighting and how fast they die, with catalog
// lookups resolved so a plan can keep the rate between looks. Parallel to
// CombatParams::types, boss body included. A boss pays nothing here, since it
// pays from its own reward table.
struct Crowd {
  std::vector<Mob> mobs;
  // Parallel to `mobs`: what one kill's drops are worth, and kills per second.
  std::vector<double> drops;
  std::vector<double> kills_per_second;

  // Returns the same crowd killed at a different rate. Used to measure one
  // crowd twice, as the Wild Totem comparison does.
  Crowd At(absl::Span<const double> rate) const;
};

// `basis` is what drops are valued against (see DropBasisFor). Resolving it
// once here lets a plan read the rate many times while paying for catalog
// lookups once.
Crowd CrowdFor(const GameState& state, const DropBasis& basis,
               const CombatParams& params,
               absl::Span<const double> kills_per_second);

// Meso per second `crowd` pays under one set of levers.
double MesoPerSecond(const Crowd& crowd, double meso_pct, double meso_mult,
                     double item_drop_pct);

// Same as MesoPerSecond, reading the three levers from the character's own
// sheet.
double MesoPerSecondFor(const GameState& state, const Crowd& crowd);

}  // namespace ms

#endif  // MS_ANALYSIS_MESO_RATE_H_
