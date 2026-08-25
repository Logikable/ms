/* Arcane Force: the Arcane River stat, what a symbol is worth, and what
 * meeting a map's requirement does to a fight.
 *
 * Every map past level 200 asks for a number of Arcane Force, and how much of
 * that the character carries scales both what they deal and what they take --
 * a tenth of their damage against 2.8x of the monster's at the bottom, half
 * again against nothing at the top. Arcane Symbols are what carry it.
 *
 * Pure math over the protos. What the character actually wears is their own
 * business -- see CharacterInstance::arcane_force.
 */
#ifndef MS_SRC_CHARACTER_ARCANE_FORCE_H_
#define MS_SRC_CHARACTER_ARCANE_FORCE_H_

#include <cstdint>

#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"

namespace ms {

// As far as a symbol goes. Past this it takes no more duplicates and its
// Arcane Force stops climbing.
inline constexpr int kMaxSymbolLevel = 20;

// Whether `proto` is one of the six Arcane Symbols. The symbol block is what
// says so: nothing else carries one.
bool IsArcaneSymbol(const EquipPrototype& proto);

// The level `item` is at, which is 1 for a fresh drop. Read through here
// rather than off the field, so that the zero every drop writes means the
// level every drop starts at.
int SymbolLevel(const Equip& item);

// Duplicates that carry a symbol from `level` to the next: level^2 + 11. So
// 12 at level 1 and 372 at 19, and 2,679 to go the whole way. 0 at the cap,
// where there is no next level to reach.
int SymbolExpToNextLevel(int level);

// Meso the level-up out of `level` costs. The price is per area and climbs
// with the level: 10,000 x floor[(base + 0.1 x level) x duplicates], where
// the base runs 8 in Vanishing Journey to 18 in Esfera. 0 at the cap.
int64_t SymbolLevelUpCost(const EquipPrototype& proto, int level);

// Arcane Force a symbol at `level` is worth: 10 a level, plus 20 for wearing
// one at all. So a fresh symbol is 30 and a maxed one 220.
int SymbolArcaneForce(int level);

// Whether `item` has taken the duplicates its next level asks for. What is
// left is the meso, which is the player's to pay -- see SymbolLevelUpCost.
bool SymbolCanLevelUp(const Equip& item);

// Raises `item` one level and carries the excess EXP into the next rung.
// Does nothing to a symbol that has not earned the level or is at the cap;
// charging for it is the caller's business.
void LevelUpSymbol(Equip& item);

// The stats a worn symbol grants: 10 of the wearer's primary stat for every
// point of Arcane Force, which is GMS's 100 per 10. A symbol grants this and
// nothing else, so its prototype carries no base stats at all.
EquipStats SymbolStatsFor(StatField primary, int level);

// What meeting a map's Arcane Force requirement does to the fight, as two
// multipliers. Both are 1 where nothing is asked for, which is every map
// outside Arcane River.
struct ArcaneFactors {
  double damage_dealt = 1.0;  // 0.10 at nothing met, 1.50 at half again
  double damage_taken = 1.0;  // 2.8 at nothing met, 0 at half again
};

// The factors for a character carrying `owned` against a map asking
// `required`. GMS's table, stepped by the whole percentage met and rounded
// down, so the last point of Arcane Force before a step buys nothing and the
// one after it buys the whole step.
ArcaneFactors ArcaneFactorsFor(int owned, int required);

}  // namespace ms

#endif  // MS_SRC_CHARACTER_ARCANE_FORCE_H_
