/* Gears a character the way a player does, and measures the attacks that choice
 * depends on. Shared by the sims that play a character forward.
 *
 * A player doesn't look up what weapon to use: they try the weapons their job
 * can hold and keep the one that hits hardest. Outfit does the same, so this
 * file has no list of jobs and a new branch is geared correctly as soon as it
 * exists.
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
// shelves.
//
// The weapon is chosen by measurement, not from a list, and re-measured each
// time because the answer changes as the book fills. `budget` weighs price
// against the character's meso. A sim measuring the climb wants that, since
// affording the weapon is part of what it measures.
void Outfit(GameState& state, bool budget,
            EquipType settled = EQUIP_TYPE_UNSPECIFIED);

// The weapon type the character would settle on with their whole book bought,
// measured on a copy.
//
// The weapon and the book each decide the other's value, so one must be settled
// first. Spending the book point by point can't do it: every point goes to
// skills for the weapon already in hand, so a Paladin who picks up a polearm
// never buys Blast. This asks what the branch is built for, which a player
// knows before spending anything.
EquipType SettledWeaponType(GameState& state, bool budget);

// Outfit with the weapon type already chosen: the best `type` the character can
// hold, its ammunition, the branch's off-hand, and the shop's accessories. For
// a sim that chooses the weapon elsewhere, since measuring it needs a book and
// the book isn't always bought by the time the weapon is needed.
void OutfitWeapon(GameState& state, EquipType type);

// Whether the character has reached the map that gives out `proto`. A symbol
// isn't a regular drop: one waits at each Arcane River area, so a character at
// 200 has the first of the six and none of the rest. True for anything that
// isn't a symbol.
bool ReachedSymbolArea(const CharacterInstance& character,
                       const EquipPrototype& proto);

// Feeds each worn Arcane Symbol the spare copies in the bag, and returns how
// many it absorbed. A spare that nothing absorbs stays in the bag for good, so
// this runs at every look to free bag space as well as to level symbols.
// Leveling up costs meso, which GearShopper ranks against a star.
int CollectSymbols(CharacterInstance& character);

// Wears the best item for every slot the shop doesn't stock, as a player who
// had cleared everything would. An item family provides as many distinct pieces
// as it has. `skip` names catalog keys to leave off, for a sim asking whether a
// fight can be won without the gear only that fight drops.
void OutfitDrops(GameState& state, const std::set<std::string>& skip = {});

// Wears the best items from the bag in the slots the shop doesn't stock. A
// piece goes on when its slot is empty or it outranks what is there, so a
// second copy never replaces a first that has scrolls and stars. This is the
// drop half of Outfit: Outfit shops, this opens the bag.
void WearBestFromBag(CharacterInstance& character);

// Maxes out everything worn: every slot gets the scroll that measures best, and
// stars go to the item's maximum. Nothing is rolled or paid for, since a sim
// asking what a build can reach wants the ceiling, not one random outcome.
//
// `star_cap` stops every item below its own maximum, for a ceiling a player
// would actually stop at. Past 15 stars an attempt can destroy the item, and a
// piece from one boss has no second copy.
void FullyUpgrade(GameState& state, int star_cap = kMaxStarForce);

// The scroll each worn slot should use: the one that measures best when applied
// to every slot, among those with at least `success_rate` success. Restores the
// character, so it wears and buys nothing.
std::map<EquipSlot, const Scroll*> ChooseScrolls(GameState& state,
                                                 int success_rate);

}  // namespace ms

#endif  // MS_ANALYSIS_SIM_GEAR_H_
