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
#include <vector>

#include "src/character/character.h"
#include "src/combat/damage.h"
#include "src/protos/equip.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {

// One Final Attack the character carries: what it is worth against each enemy
// the swing reached, and which swings set it off.
struct FinalAttackSource {
  // Chance times damage, so a 40% chance of an extra 160% hit reads 0.64. One
  // number because that is all an expected-value damage chain can use.
  double pct = 0.0;
  // The swings this follows. SKILL_TAG_UNSPECIFIED means all of them, which is
  // what a Final Attack gated on the weapon in hand wants.
  SkillTag required_tag = SKILL_TAG_UNSPECIFIED;
};

struct DerivedStats {
  int max_hp = 0;
  int max_mp = 0;
  // What the character's stats alone are worth in DEF: 1.5 per STR and 0.4 per
  // DEX and LUK. So a character in rags has DEF, and AP spent on STR buys
  // some. Split out from the total because the stats page shows the pair.
  int base_def = 0;
  // base_def plus everything worn and granted, with any percentage over the
  // whole of it. Worth less than it looks: DEF stops paying once it reaches a
  // share of the attacking monster's attack, and a levelled character sits
  // past that cap already -- see DefenseReduction.
  int def = 0;
  // The share of incoming damage cancelled (0.10 == 10% less taken), combined
  // across every source by multiplying. Magic Guard counts here: the damage it
  // sends to MP is damage the character never takes, since nothing tracks MP.
  double damage_taken_pct = 0.0;
  // Chance an incoming hit misses outright (0.30 == 30%), combined across
  // sources by multiplying what gets through -- two 50% dodges leave a quarter
  // of the hits landing, not none of them.
  double dodge_chance = 0.0;
  // Share of a hit taken that goes straight back into whatever landed it
  // (1.20 == 120% of what the character actually lost). Summed, since two
  // reflections both fire.
  double damage_reflect_pct = 0.0;
  // Added chance for a swing to crit (0.40 == 40%). Feeds OffenseStatsFor,
  // since what it modifies is damage rather than the character's own bulk.
  double crit_rate = 0.0;
  // Added critical damage (0.05 == +5%), summed, and worth only the share of
  // swings that crit at all -- so it is the crit_rate skills that make it
  // worth anything.
  double crit_dmg = 0.0;
  // Expected share of the HP pool a landed swing puts back. Costs no swing,
  // unlike a healing cast -- the fight adds it after the hit lands.
  double hp_recover_pct = 0.0;
  // Extra EXP every kill yields, summed. Unlike everything else here it is
  // read outside a fight -- see AwardCombatRewards.
  double exp_pct = 0.0;
  // Resistance to abnormal statuses and to elemental damage. Nothing inflicts
  // either, so these reach the stats page and go no further.
  double status_resistance = 0.0;
  double elemental_resistance = 0.0;
  // Plain % damage, summed over every passive granting it, and final damage,
  // combined by multiplying: two 10% sources come to 21%. Both are one number
  // by the time they leave here, because that is all the damage chain takes.
  double damage_pct = 0.0;
  double final_dmg_pct = 0.0;
  // Share of the monster's DEF the character's attacks ignore, combined in
  // reverse across the passives granting it. Gear grants it too, and the two
  // meet the same way -- see OffenseStatsFor.
  double ied = 0.0;
  // The best weapon mastery the passives grant, 0..1. 0 leaves the beginner's
  // baseline in place rather than making the swing wilder.
  double mastery = 0.0;
  // The Final Attacks the character's passives grant, merged by what sets them
  // off. Two sources that follow the same swings are one entry, since
  // independent procs add in expectation -- which is what keeps the Hunter's
  // two arrow skills reading as one extra arrow.
  std::vector<FinalAttackSource> final_attacks;
  // Faster-swing stages added on top of the weapon's own attack speed. Feeds
  // the swing interval, not the per-hit damage -- see ComputeCombatParams.
  int attack_speed_bonus = 0;
  // Damage the passives add to one named skill apiece, keyed by that skill's
  // display name and charged per line. Empty for every character but a Ranger.
  // Kept as a map rather than folded into damage_pct because what it lifts is
  // one swing rather than all of them -- see SkillEffect::boosted_skill_pct.
  std::map<std::string, double> skill_pct_bonus;
  // Percentage over the character's whole attack, worn and granted alike.
  // Applied where the totals are summed rather than folded into skill_stats,
  // because what it scales includes the weapon in their hand -- see
  // TotalEquipStats.
  double attack_pct = 0.0;
  // What the passives grant, in the shape of a worn item because that is how
  // they behave: sum it with equip_stats() and hand the total wherever
  // equipment stats go. It is the only way a skill's primary stat reaches the
  // damage chain. Its DEF is only the passives' share, unlike `def` above.
  EquipStats skill_stats;
};

// Whether the weapon in hand is one `skill` will work with. True for a skill
// that names no weapon type, which is most of them.
bool SkillAllowsWeapon(const Skill& skill, EquipType weapon);

// Whether the character is carrying everything `skill` demands: the weapon
// type it names, and something in the secondary slot if it asks for one. A
// skill whose demand is unmet stays learned -- it is the effect that lapses,
// and it comes back with the right gear on.
bool SkillGearMet(const CharacterInstance& character, const Skill& skill);

// Levels every skill the character has learned gains from a skill that grants
// them -- Combat Orders, and nothing else so far. 0 for a character without
// one, which is every character but a White Knight.
int BonusSkillLevels(const CharacterInstance& character,
                     const std::map<std::string, Skill>& skills);

// What `skill` is actually worth to this character: the level they learned
// plus `bonus`, held to the master level. An unlearned skill stays unlearned,
// and the skill granting the bonus does not receive it.
//
// This is the level everything that READS a skill wants -- its stats, its
// damage, the level shown beside it. Spending SP wants skill_level() instead:
// a granted level is not one the player bought, and must not stop them buying
// the real one.
int EffectiveSkillLevel(const CharacterInstance& character, const Skill& skill,
                        int bonus);

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
