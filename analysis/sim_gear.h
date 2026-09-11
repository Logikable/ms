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

// Buys and wears the best gear the character can hold: the weapon, the
// ammunition it draws from, their branch's off-hand, and the rings, emblem and
// medal on the shop's equipment shelf. Both shelves are shopped, the Frozen
// tier included.
//
// Which weapon comes out of a measurement rather than a list -- the top rung
// of every ladder they can hold is swung at a mob of their own level, and the
// hardest hitter is bought. Asked afresh every time, because the answer moves
// as the book behind the weapon fills.
//
// `budget` weighs the price against the purse: a sim measuring the climb wants
// it, since affording the weapon is part of what it measures; one asking
// whether a build can hold a map does not.
void Outfit(GameState& state, bool budget,
            EquipType settled = EQUIP_TYPE_UNSPECIFIED);

// The weapon type the character would settle on with their whole book behind
// them. Measured on a copy, so it leaves them exactly as it found them.
//
// The weapon and the book are each worth what the other is: a weapon is worth
// what the skills gated to it can do, and those skills are worth nothing
// without one that can swing them. One of the two has to be settled first, and
// a book spent point by point cannot do it -- every point goes to what the
// weapon in hand can already swing, so a Paladin who happens to pick up a
// polearm never buys Blast, and never measures a mace as worth holding. This
// asks what the branch is FOR instead, which is what a player knows before
// they spend anything.
EquipType SettledWeaponType(GameState& state, bool budget);

// Outfit with the choice already made: the top rung of `type` the character
// can hold, what it draws from, their branch's off-hand, and the accessories
// the shop sells beside it. For a sim that settles the weapon elsewhere --
// measuring it needs a book, and a book is not always bought by the time the
// weapon has to be in hand.
void OutfitWeapon(GameState& state, EquipType type);

// Wears the best of every slot the shop does not stock: the armour, the boss
// accessories and the pocket, which in this game drop rather than sell. What a
// player who had cleared everything would be standing in. A family of slots
// takes as many distinct pieces as it holds, so a character wearing rings
// wears four of them.
//
// `skip` names catalog keys to leave off, for a sim asking whether a fight can
// be won without what only that fight pays -- a boss cannot be beaten in its
// own drop. Within a slot the highest rung wins, the way a shop ladder is
// climbed: there is nothing to measure while each slot holds one item.
void OutfitDrops(GameState& state, const std::set<std::string>& skip = {});

// Wears the best of what the bag is already holding, in the slots the shop
// does not stock -- the armour, the accessories and the pocket, which drop
// rather than sell. A piece is put on when its slot is empty or when it
// outranks what is in it, so a second copy of what is worn never displaces the
// scrolls and stars on the first.
//
// The drop half of Outfit: that one shops, this one opens the bag. A sim
// playing a climb forward needs both, since a player wears what falls.
void WearBestFromBag(CharacterInstance& character);

// Puts everything worn at its ceiling: every upgrade slot filled with the
// scroll that measures best on the item, and stars up to the item's own
// maximum. Nothing is rolled and nothing is paid for -- a sim asking what a
// build can reach wants the ceiling, not one draw from it.
//
// Which scroll is best is measured rather than listed, for the reason Outfit
// measures the weapon: a thief's weapon takes three 15% traces that differ
// only in which stat rides the attack, and only a swing says which.
//
// `star_cap` holds every item below its own maximum, for a ceiling a player
// would actually stop at -- past 15 an attempt can destroy the item, and a
// piece only one boss drops has no second copy to reach for.
void FullyUpgrade(GameState& state, int star_cap = kMaxStarForce);

// Which scroll each worn slot wants: the one the character measures best in
// when it fills every slot of the item, chosen from those the item takes that
// succeed `success_rate` of the time. A slot no scroll helps is absent.
//
// Restores the character afterwards, so asking wears nothing and buys
// nothing.
std::map<EquipSlot, const Scroll*> ChooseScrolls(GameState& state,
                                                 int success_rate);

}  // namespace ms

#endif  // MS_ANALYSIS_SIM_GEAR_H_
