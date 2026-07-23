/* Folds a character's AP-allocated stats, worn equipment, and learned passive
 * skills into the totals they actually carry into play. The defensive
 * counterpart to combat/damage.h's OffenseStatsFor: that one answers what the
 * character deals, this one what the character has. character_stats.cc
 * implements it.
 */
#ifndef MS_SRC_CHARACTER_STATS_H_
#define MS_SRC_CHARACTER_STATS_H_

#include <map>
#include <string>

#include "src/character.h"
#include "src/protos/skill.pb.h"

namespace ms {

struct DerivedStats {
  int max_hp = 0;
  int def = 0;
  // The share of incoming damage cancelled (0.10 == 10% less taken). Nothing
  // damages the character yet, so this is carried but never read.
  double damage_taken_pct = 0.0;
};

// `skills` is the loaded skill catalog; every passive in it the character has
// learned contributes its level's effect. Attack skills are ignored -- their
// lever is damage, which OffenseStatsFor handles.
DerivedStats DerivedStatsFor(const CharacterInstance& character,
                             const std::map<std::string, Skill>& skills);

}  // namespace ms

#endif  // MS_SRC_CHARACTER_STATS_H_
