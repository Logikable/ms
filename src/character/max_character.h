/* The best a character can be at each level: what a player who spent well is
 * wearing and carrying when they get there.
 *
 * One fixed answer for every job. The %stat lines follow the job, as does
 * whether weapon lines use ATT or M.ATT, but nothing else here depends on the
 * job: the point of the mode is to measure fights against a known character,
 * not to find each job's optimum.
 *
 * Every number is priced against what //analysis:progression_sim says leveling
 * pays by that level; the .cc shows the math band by band. The rule is that a
 * band's gear costs no more than the income at the level where it opens, so a
 * max character is a rich player, not an impossible one.
 */
#ifndef MS_SRC_CHARACTER_MAX_CHARACTER_H_
#define MS_SRC_CHARACTER_MAX_CHARACTER_H_

#include <map>
#include <string>

#include "src/character/character.h"
#include "src/protos/boss.pb.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/mob.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {

// What has been done to every item the character wears. Stars are capped at
// each item's own limit for its level, so a low-level item in a high-level
// outfit gets what it can, not what was asked for.
struct MaxGear {
  // Both Golden Hammers used, which adds two upgrade slots. Every upgrade slot
  // is then scrolled.
  bool hammered = false;
  int stars = 0;
  // The weapon's stars, set separately because it's where a player spends meso
  // first and the one item worth taking further than the rest.
  int weapon_stars = 0;
  // The potential rank of every cubeable item, and of the three weapon slots.
  // UNSPECIFIED for a level with no cubing.
  PotentialRank armour_potential = POTENTIAL_RANK_UNSPECIFIED;
  PotentialRank weaponry_potential = POTENTIAL_RANK_UNSPECIFIED;
};

// The gear a character at `level` has paid for.
MaxGear MaxGearForLevel(int level);

// The lines `slot` has at `level`, for a character whose damage is based on
// `primary`. Empty for a slot that takes no potential and for a level with no
// cubing.
//
// Every item of one kind has the same lines. The variety a real player ends up
// with is luck, not a decision, and a character whose stats change with the
// random seed makes fight measurements meaningless.
Potential MaxPotentialFor(EquipSlot slot, const MaxGear& gear,
                          StatField primary);

// Spends the whole Hyper Stat pool on both presets, best value per point first,
// discarding any previous allocation. A stat's value is measured on this
// character instead of listed here (see hyper_plan.h), so the job's own stats
// decide. `skills` is the catalog combat power is read through; `bosses` and
// `mobs` are what Ignore Defense is valued against: the toughest fight the
// character's level has unlocked.
void SpendMaxHyperStats(CharacterInstance& character,
                        const std::map<std::string, Skill>& skills,
                        const std::map<std::string, Boss>& bosses,
                        const std::map<std::string, Mob>& mobs);

// The three Inner Ability lines each preset has: one Legendary line on top and
// two Epic ones below, which is what the honor from leveling can reach.
AbilityPreset MaxAbilityPreset(Activity preset, StatField primary);

}  // namespace ms

#endif  // MS_SRC_CHARACTER_MAX_CHARACTER_H_
