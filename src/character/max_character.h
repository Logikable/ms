/* The ceiling a character stands at, level by level: what a player who
 * spent well is wearing and carrying when they get there.
 *
 * One static answer for every job. Which stat a %stat line raises follows the
 * job, and so does whether the weapon lines read ATT or M.ATT, but nothing
 * else here asks who is holding it: the point of the mode is a fight measured
 * against a known character, not a per-job optimum.
 *
 * Every number below is priced against what //analysis:progression_sim says
 * the climb pays by that level -- see the .cc, which carries the arithmetic
 * band by band. The rule is that a band's gear costs no more than the income
 * of the level it opens at, so a max character is a rich player rather than
 * an impossible one.
 */
#ifndef MS_SRC_CHARACTER_MAX_CHARACTER_H_
#define MS_SRC_CHARACTER_MAX_CHARACTER_H_

#include "src/character/character.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"

namespace ms {

// What every piece the character wears has had done to it. Stars are held to
// each item's own cap for its level, so a low-level piece in a high-level
// outfit carries what it can rather than what was asked for.
struct MaxGear {
  // Both Golden Hammers driven in, which widens the upgrade shelf by two.
  // Every slot the shelf ends up with is then scrolled.
  bool hammered = false;
  int stars = 0;
  // The weapon alone, which is where a player's meso goes first and the one
  // piece worth taking past the rest.
  int weapon_stars = 0;
  // The rank every cubeable piece carries, and the rank the three weaponry
  // slots carry. UNSPECIFIED for a level with no cubing behind it.
  PotentialRank armour_potential = POTENTIAL_RANK_UNSPECIFIED;
  PotentialRank weaponry_potential = POTENTIAL_RANK_UNSPECIFIED;
};

// The gear a character at `level` has paid for.
MaxGear MaxGearForLevel(int level);

// The lines `slot` carries at `level`, for a character whose damage is built
// on `primary`. Empty for a slot that takes no potential and for a level with
// no cubing behind it.
//
// Every piece of one kind carries the same lines: the spread a real player
// ends up with is luck rather than a decision, and a fight measured against a
// character whose sheet moves with the seed says nothing.
Potential MaxPotentialFor(EquipSlot slot, const MaxGear& gear,
                          StatField primary);

// Spends the whole Hyper Stat pool on both presets, cheapest level first over
// the stats a fight cares about. Throws away whatever was allocated before.
void SpendMaxHyperStats(CharacterInstance& character);

// The three Inner Ability lines each preset holds: a Legendary line on top
// and two Epic ones under it, which is what the honor a climb pays reaches.
AbilityPreset MaxAbilityPreset(StatPreset preset, StatField primary);

}  // namespace ms

#endif  // MS_SRC_CHARACTER_MAX_CHARACTER_H_
