/* The daily claim: what a character may take each day, and taking it.
 *
 * So far that is Arcane Symbols. Owning one opens every area at or below it,
 * because the areas are a ladder a character climbs in order: whoever reached
 * Lachelein passed through Vanishing Journey and Chu Chu Island to get there,
 * whether or not they kept a symbol from either.
 *
 * A day's symbols arrive packed into one item per area rather than as twenty
 * loose copies, which would be twenty rows of the bag. What that item is worth
 * fed to a worn symbol is the same either way -- see SymbolWorth.
 *
 * On the boss reset clock, so the whole game turns over at one hour.
 */
#ifndef MS_SRC_CHARACTER_DAILIES_H_
#define MS_SRC_CHARACTER_DAILIES_H_

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "src/character/character.h"
#include "src/protos/equip.pb.h"

namespace ms {

// Copies of each symbol a day's claim pays.
inline constexpr int kSymbolsPerDay = 20;

// The symbols `character` may claim, in area order. Every one at or below the
// highest they own, worn or in the bag; empty for anyone owning none.
std::vector<const EquipPrototype*> ClaimableSymbols(
    const CharacterInstance& character,
    const std::map<std::string, EquipPrototype>& equips);

// Whether a claim last taken at `claimed` may be taken again at `now`.
bool DailiesAvailable(int64_t claimed, int64_t now);

// One packed symbol per claimable area into the bag, and banks the claim at
// `now`. Takes nothing and returns false unless there is something to claim,
// the day is open, and the whole lot fits: half a claim would cost the player
// the rest of it until tomorrow.
bool ClaimDailies(CharacterInstance& character,
                  const std::map<std::string, EquipPrototype>& equips,
                  int64_t now);

// A symbol carrying `copies` of itself, packed up the level ladder as far as
// it goes. Twenty copies is a level 2 holding 7.
Equip PackedSymbol(int copies);

}  // namespace ms

#endif  // MS_SRC_CHARACTER_DAILIES_H_
