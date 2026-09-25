/* The daily claim: what a character can collect each day, and collecting it.
 *
 * Currently that is Arcane Symbols. Owning one unlocks every area at or below
 * it, because characters progress through the areas in order: anyone who
 * reached Lachelein passed through Vanishing Journey and Chu Chu Island,
 * whether or not they kept a symbol from either.
 *
 * A day's symbols come packed into one item per area instead of twenty separate
 * copies, which would take twenty bag rows. Feeding the packed item to a worn
 * symbol is worth the same either way; see SymbolWorth.
 *
 * Uses the boss reset time, so everything in the game resets at the same hour.
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

// Copies of each symbol one day's claim gives.
inline constexpr int kSymbolsPerDay = 20;

// The symbols `character` can claim, in area order: every one at or below the
// furthest they own, worn or in the bag. Empty if they own none.
std::vector<const EquipPrototype*> ClaimableSymbols(
    const CharacterInstance& character,
    const std::map<std::string, EquipPrototype>& equips);

// Whether a claim last collected at `claimed` can be collected again at `now`.
bool DailiesAvailable(int64_t claimed, int64_t now);

// Puts one packed symbol per claimable area in the bag and records the claim at
// `now`. All or nothing: claiming half would cost the player the rest until
// tomorrow.
bool ClaimDailies(CharacterInstance& character,
                  const std::map<std::string, EquipPrototype>& equips,
                  int64_t now);

// A symbol containing `copies` of itself, levelled up as far as they go. Twenty
// copies make a level 2 symbol holding 7.
Equip PackedSymbol(int copies);

}  // namespace ms

#endif  // MS_SRC_CHARACTER_DAILIES_H_
