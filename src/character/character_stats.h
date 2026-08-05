/* Folds a character's AP-allocated stats, worn equipment, and learned passive
 * skills into the totals they actually carry into play. The defensive
 * counterpart to combat/damage.h's OffenseStatsFor: that one answers what the
 * character deals, this one what the character has. character_stats.cc
 * implements it.
 */
#ifndef MS_SRC_CHARACTER_CHARACTER_STATS_H_
#define MS_SRC_CHARACTER_CHARACTER_STATS_H_

#include <map>
#include <string>

#include "src/character/character.h"
#include "src/protos/equip.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {

struct DerivedStats {
  int max_hp = 0;
  int max_mp = 0;
  // Everything worn and granted, plus the base every character carries for
  // their primary stats alone -- 1.5 per STR and 0.4 per point of DEX and LUK.
  // A character in rags has DEF, and a warrior who spends AP on STR gains it
  // without touching their armour.
  //
  // Nothing in combat reads this yet: the character panel shows it, and the
  // damage-taken formula wants it when mobs can hurt the player.
  int def = 0;
  // The share of incoming damage cancelled (0.10 == 10% less taken). Nothing
  // damages the character yet, so this is carried but never read.
  double damage_taken_pct = 0.0;
  // Added chance for a swing to crit (0.40 == 40%). Feeds OffenseStatsFor,
  // since what it modifies is damage rather than the character's own bulk.
  double crit_rate = 0.0;
  // Faster-swing stages added on top of the weapon's own attack speed. Feeds
  // the swing interval, not the per-hit damage -- see ComputeCombatParams.
  int attack_speed_bonus = 0;
  // What the character's passives grant in the shape of a worn item, because
  // that is how they behave: sum this with CharacterInstance::equip_stats()
  // and hand the total wherever equipment stats go, OffenseStatsFor above all.
  // Skills that grant a primary stat reach the damage chain no other way.
  // DEF is in here too, but it is only the part the passives grant -- `def`
  // above is a larger number, carrying the worn DEF and the primary-stat base
  // as well. Neither is a substitute for the other.
  EquipStats skill_stats;
};

// `skills` is the loaded skill catalog; every passive in it the character has
// learned contributes its level's effect. Attack skills are ignored -- their
// lever is damage, which OffenseStatsFor handles.
DerivedStats DerivedStatsFor(const CharacterInstance& character,
                             const std::map<std::string, Skill>& skills);

// Everything the character wears plus everything their passives grant, summed.
// This is what the rest of the game should read wherever it wants "the
// character's equipment stats": a skill that grants LUK is worth exactly as
// much as a ring that grants LUK, and nothing downstream should have to know
// which one it came from. `derived` is the result of DerivedStatsFor above.
EquipStats TotalEquipStats(const CharacterInstance& character,
                           const DerivedStats& derived);

}  // namespace ms

#endif  // MS_SRC_CHARACTER_CHARACTER_STATS_H_
