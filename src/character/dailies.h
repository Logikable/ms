/* The daily claim: what a character can collect each day, and collecting it.
 *
 * Arcane Symbols from the 5th advancement: owning one unlocks every area at or
 * below it, because characters progress through the areas in order: anyone who
 * reached Lachelein passed through Vanishing Journey and Chu Chu Island,
 * whether or not they kept a symbol from either. Sacred Symbols (Grand ones
 * included) from the 6th, paid by how well the character farms each area
 * instead, since Grandis's monsters barely hurt and a symbol drop from any of
 * them is within reach of everyone.
 *
 * A day's symbols come packed into one item per area instead of separate
 * copies, which would take a bag row each. Feeding the packed item to a worn
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

// Copies of each Arcane Symbol one day's claim gives. GMS gives 40 a day plus
// a weekly quest of 80 done three times a week, about 74 a day.
inline constexpr int kSymbolsPerDay = 75;

// One area's part of a day's claim.
struct DailyReward {
  const EquipPrototype* proto = nullptr;
  int copies = 0;
};

// Copies of `proto` one day's claim gives: kSymbolsPerDay for an Arcane
// Symbol, the data's daily_copies for a Sacred one.
int DailySymbolCopies(const EquipPrototype& proto);

// What `character` can claim, Arcane then Sacred, each in area order, leaving
// out symbols whose copies held already reach the max level. Arcane needs the
// 5th advancement and pays every area at or below the furthest symbol owned,
// worn or in the bag. Sacred needs the 6th and pays each area named in
// `sacred_shares` (symbol name to the share of its copies earned; see
// //src:sacred_dailies), rounded, whether or not a symbol of it is owned.
// Empty if none is left.
std::vector<DailyReward> ClaimableSymbols(
    const CharacterInstance& character,
    const std::map<std::string, EquipPrototype>& equips,
    const std::map<std::string, double>& sacred_shares = {});

// Whether a claim last collected at `claimed` can be collected again at `now`.
bool DailiesAvailable(int64_t claimed, int64_t now);

// Puts one packed symbol per claimable area in the bag and records the claim at
// `now`. All or nothing: claiming half would cost the player the rest until
// tomorrow.
bool ClaimDailies(CharacterInstance& character,
                  const std::map<std::string, EquipPrototype>& equips,
                  int64_t now,
                  const std::map<std::string, double>& sacred_shares = {});

// A `proto` symbol containing `copies` of itself, levelled up as far as they
// go. 75 Arcane copies make a level 5 symbol holding none.
Equip PackedSymbol(const EquipPrototype& proto, int copies);

}  // namespace ms

#endif  // MS_SRC_CHARACTER_DAILIES_H_
