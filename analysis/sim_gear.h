/* Gears a character the way a player does, and measures the attacks that choice
 * depends on. A player tries the weapons their job can hold and keeps the one
 * that hits hardest, so this file has no list of jobs.
 */
#ifndef MS_ANALYSIS_SIM_GEAR_H_
#define MS_ANALYSIS_SIM_GEAR_H_

#include <map>
#include <set>
#include <string>
#include <vector>

#include "src/character/character.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/protos/equip.pb.h"
#include "src/protos/scroll.pb.h"

namespace ms {

// Name of the character's weapon, or "-" if empty-handed.
std::string HeldWeaponName(const CharacterInstance& character);

// Equips the bag's copy of `name` if the character can wear it. Found by name
// rather than index because equipping reorders the bag: the displaced item goes
// back into it.
bool EquipByName(CharacterInstance& character, const std::string& name);

// Buys and wears the best gear the character can use: the weapon, its
// ammunition, the branch's off-hand, and the shop's accessories from both
// shelves. The weapon is re-measured each time, since the answer changes as the
// book fills. `budget` weighs price against the character's meso.
void Outfit(GameState& state, bool budget,
            EquipType settled = EQUIP_TYPE_UNSPECIFIED);

// The weapon type the character would settle on with their whole book bought,
// measured on a copy. Spending the book point by point can't settle it: every
// point goes to skills for the weapon in hand, so a Paladin who picks up a
// polearm never buys Blast.
EquipType SettledWeaponType(GameState& state, bool budget);

// Outfit with the weapon type already chosen, for a sim whose book isn't bought
// by the time the weapon is needed.
void OutfitWeapon(GameState& state, EquipType type);

// Whether the character has reached the map that gives out `proto`. One symbol
// waits at each Arcane River area. True for anything that isn't a symbol.
bool ReachedSymbolArea(const CharacterInstance& character,
                       const EquipPrototype& proto);

// Feeds each worn Arcane Symbol the spare copies in the bag, and returns how
// many it absorbed. Runs at every look, since an unabsorbed spare holds a bag
// row for good.
int CollectSymbols(CharacterInstance& character);

// Wears the best item for every slot the shop doesn't stock, as a player who
// had cleared everything would. `skip` names catalog keys to leave off, for a
// sim asking whether a fight can be won without the gear only that fight drops.
void OutfitDrops(GameState& state, const std::set<std::string>& skip = {});

// Wears the best items from the bag in the slots the shop doesn't stock. A
// piece goes on when its slot is empty or it outranks what is there, so a
// second copy never replaces a first that has scrolls and stars.
void WearBestFromBag(CharacterInstance& character);

// Maxes out everything worn: the best-measuring scroll in every slot, and stars
// to the item's maximum. Nothing is rolled or paid for.
//
// `star_cap` stops every item below its own maximum, where a player would stop:
// past 15 stars an attempt can destroy the item.
void FullyUpgrade(GameState& state, int star_cap = kMaxStarForce);

// The scroll each worn slot should use: the one that measures best when applied
// to every slot, among those with at least `success_rate` success. Restores the
// character, so it wears and buys nothing.
std::map<EquipSlot, const Scroll*> ChooseScrolls(GameState& state,
                                                 int success_rate);

}  // namespace ms

#endif  // MS_ANALYSIS_SIM_GEAR_H_
