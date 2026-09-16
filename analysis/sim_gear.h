/* Gearing a character the way a player does, and the swing measurement the
 * choice rests on. Shared by the sims that play a character forward.
 *
 * A player does not read a table to learn what to hold: they try the weapons
 * their job can hold and keep the one that hits hardest. Outfit does the same,
 * which is why no list of jobs appears here -- a branch added tomorrow is
 * geared correctly the day it exists.
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

// The name of the character's weapon, "-" for empty hands.
std::string HeldWeaponName(const CharacterInstance& character);

// Puts the bag's copy of `name` on, if the character can wear it. Found by
// name rather than by index because equipping shuffles the bag: what is
// displaced goes back into it.
bool EquipByName(CharacterInstance& character, const std::string& name);

// Buys and wears the best gear the character can hold: the weapon, its
// ammunition, their branch's off-hand and the shop's accessories, both shelves
// included.
//
// Which weapon comes out of a MEASUREMENT rather than a list, and is asked
// afresh every time because the answer moves as the book fills. `budget`
// weighs the price against the purse: a sim measuring the climb wants it,
// since affording the weapon is part of what it measures.
void Outfit(GameState& state, bool budget,
            EquipType settled = EQUIP_TYPE_UNSPECIFIED);

// The weapon type the character would settle on with their whole book behind
// them, measured on a copy.
//
// The weapon and the book are each worth what the other is, so one has to be
// settled first -- and a book spent point by point cannot do it: every point
// goes to what is already in hand, so a Paladin who picks up a polearm never
// buys Blast. This asks what the branch is FOR, which a player knows before
// spending anything.
EquipType SettledWeaponType(GameState& state, bool budget);

// Outfit with the choice already made: the top rung of `type` the character
// can hold, what it draws from, their branch's off-hand, and the accessories
// the shop sells beside it. For a sim that settles the weapon elsewhere --
// measuring it needs a book, and a book is not always bought by the time the
// weapon has to be in hand.
void OutfitWeapon(GameState& state, EquipType type);

// Whether the character has reached the map that hands `proto` over. A symbol
// is not a drop off a ladder: one waits at each Arcane River checkpoint, so a
// character standing at 200 has the first of the six and none of the rest.
// True for everything that is not a symbol.
bool ReachedSymbolArea(const CharacterInstance& character,
                       const EquipPrototype& proto);

// Feeds every worn Arcane Symbol the spares the bag holds, and says how many
// it absorbed. A spare nothing takes sits there for good, so this runs at
// every look for the ROOM as much as the rung. Raising the level is paid in
// meso, which GearShopper ranks against a star.
int CollectSymbols(CharacterInstance& character);

// Wears the best of every slot the shop does not stock -- what a player who
// had cleared everything would stand in. A family takes as many distinct
// pieces as it holds. `skip` names catalog keys to leave off, for a sim asking
// whether a fight can be won without what only that fight pays.
void OutfitDrops(GameState& state, const std::set<std::string>& skip = {});

// Wears the best of what the BAG holds, in the slots the shop does not stock.
// A piece goes on when its slot is empty or it outranks what is in it, so a
// second copy never displaces the scrolls and stars on the first. The drop
// half of Outfit: that one shops, this one opens the bag.
void WearBestFromBag(CharacterInstance& character);

// Puts everything worn at its ceiling: every slot filled with the scroll that
// MEASURES best, and stars to the item's maximum. Nothing is rolled and
// nothing is paid for -- a sim asking what a build can reach wants the
// ceiling, not one draw from it.
//
// `star_cap` holds every item below its own maximum, for a ceiling a player
// would stop at: past 15 an attempt can destroy the item, and a piece one boss
// drops has no second copy.
void FullyUpgrade(GameState& state, int star_cap = kMaxStarForce);

// Which scroll each worn slot wants: the one the character measures best in
// when it fills every slot, out of those succeeding `success_rate` of the
// time. Restores the character, so asking wears and buys nothing.
std::map<EquipSlot, const Scroll*> ChooseScrolls(GameState& state,
                                                 int success_rate);

}  // namespace ms

#endif  // MS_ANALYSIS_SIM_GEAR_H_
