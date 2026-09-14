/* What a second of play pays, which is the currency every plan here ranks in.
 *
 * Damage is not a separate question. A faster kill is another kill's worth of
 * meso, so a rate measured in meso already carries it -- and it carries the
 * half a damage rate cannot see, the drop and meso levers, which fill the
 * purse without moving a swing. That is why nothing here measures damage.
 *
 * Two channels, not one. The meso a mob drops is a 60% chance that drop rate
 * raises to certain and no further, so a rate read off it alone saturates at
 * +66.7%. The Etc the player sells off the same kill has no such ceiling. A
 * plan weighing a drop line against a star has to see both.
 */
#ifndef MS_ANALYSIS_MESO_RATE_H_
#define MS_ANALYSIS_MESO_RATE_H_

#include <map>
#include <string>
#include <vector>

#include "absl/types/span.h"
#include "src/combat/encounter.h"
#include "src/game_state.h"
#include "src/protos/item.pb.h"
#include "src/protos/mob.pb.h"

namespace ms {

// What the Etc off one kill of `mob` fetches at the counter, before drop rate
// lifts it. A token or a soul shard is worth nothing here -- the counter pays
// nothing for either, and what they really buy is priced elsewhere.
double EtcPerKill(const std::map<std::string, ItemPrototype>& items,
                  const Mob& mob);

// What one kill pays, with the Etc already resolved: the meso drop, whose
// chance caps at certain, plus the Etc, whose rate does not. The character's
// %meso is left out -- it multiplies every mob alike, so a caller ranking maps
// needs only this.
double MesoPerKill(const Mob& mob, double etc, double item_drop_pct);

// The monsters in front of the character and how fast they are falling, with
// everything the catalogs had to answer already resolved -- so a plan can keep
// a rate between looks without keeping the catalogs with it.
//
// Parallel to CombatParams::types, a boss body included, so a caller handing
// over a kill rate measured against those types needs no reindexing. A boss
// pays nothing here: it pays out of its own table, which no %meso reaches.
struct Crowd {
  std::vector<Mob> mobs;
  // Parallel to `mobs`: what one kill's Etc fetches, and how many fall a
  // second.
  std::vector<double> etc;
  std::vector<double> kills_per_second;

  // The same crowd killed at a different rate. What the Wild Totem question
  // needs, which is one crowd measured twice.
  Crowd At(absl::Span<const double> rate) const;
};

Crowd CrowdFor(const GameState& state, const CombatParams& params,
               absl::Span<const double> kills_per_second);

// Meso a second `crowd` pays under one set of levers.
double MesoPerSecond(const Crowd& crowd, double meso_pct, double meso_mult,
                     double item_drop_pct);

// The same rate for the character as they stand, reading their three levers
// off their own sheet. What every caller with a GameState in hand wants.
double MesoPerSecondFor(const GameState& state, const Crowd& crowd);

}  // namespace ms

#endif  // MS_ANALYSIS_MESO_RATE_H_
