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
#include "src/combat/damage.h"
#include "src/protos/equip.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {

struct DerivedStats {
  int max_hp = 0;
  int max_mp = 0;
  // Everything worn and granted, plus the base every character carries for
  // their stats alone: 1.5 per STR and 0.4 per DEX and LUK. So a character in
  // rags has DEF, and AP spent on STR buys some.
  int def = 0;
  // The share of incoming damage cancelled (0.10 == 10% less taken).
  double damage_taken_pct = 0.0;
  // Share of a hit taken that goes straight back into whatever landed it
  // (1.20 == 120% of what the character actually lost). Summed, since two
  // reflections both fire.
  double damage_reflect_pct = 0.0;
  // Added chance for a swing to crit (0.40 == 40%). Feeds OffenseStatsFor,
  // since what it modifies is damage rather than the character's own bulk.
  double crit_rate = 0.0;
  // Plain % damage, summed over every passive granting it, and final damage,
  // combined by multiplying: two 10% sources come to 21%. Both are one number
  // by the time they leave here, because that is all the damage chain takes.
  double damage_pct = 0.0;
  double final_dmg_pct = 0.0;
  // The best weapon mastery the passives grant, 0..1. 0 leaves the beginner's
  // baseline in place rather than making the swing wilder.
  double mastery = 0.0;
  // What Final Attack is worth on an average swing: chance times damage, so a
  // 40% chance of an extra 160% hit reads 0.64. One number because that is all
  // an expected-value chain can use, and summed because independent procs add
  // in expectation.
  double final_attack_pct = 0.0;
  // Faster-swing stages added on top of the weapon's own attack speed. Feeds
  // the swing interval, not the per-hit damage -- see ComputeCombatParams.
  int attack_speed_bonus = 0;
  // What the passives grant, in the shape of a worn item because that is how
  // they behave: sum it with equip_stats() and hand the total wherever
  // equipment stats go. It is the only way a skill's primary stat reaches the
  // damage chain. Its DEF is only the passives' share, unlike `def` above.
  EquipStats skill_stats;
};

// Whether the weapon in hand is one `skill` will work with. True for a skill
// that names no weapon type, which is most of them.
bool SkillAllowsWeapon(const Skill& skill, EquipType weapon);

// `skills` is the loaded skill catalog; every passive in it the character has
// learned contributes its level's effect. Attack skills are ignored -- their
// lever is damage, which OffenseStatsFor handles.
DerivedStats DerivedStatsFor(const CharacterInstance& character,
                             const std::map<std::string, Skill>& skills);

// The offensive half of the derived stats, in the shape combat/damage.h asks
// for them. One place to keep in step with DerivedStats, rather than every
// caller that builds an OffenseStats knowing which fields cross over.
PassiveOffense PassiveOffenseFor(const DerivedStats& derived);

// Everything the character wears plus everything their passives grant, summed.
// This is what the rest of the game should read wherever it wants "the
// character's equipment stats": a skill that grants LUK is worth exactly as
// much as a ring that grants LUK, and nothing downstream should have to know
// which one it came from. `derived` is the result of DerivedStatsFor above.
EquipStats TotalEquipStats(const CharacterInstance& character,
                           const DerivedStats& derived);

}  // namespace ms

#endif  // MS_SRC_CHARACTER_CHARACTER_STATS_H_
