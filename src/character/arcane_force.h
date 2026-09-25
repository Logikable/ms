/* Arcane Force: the Arcane River stat, what a symbol is worth, and how meeting
 * a map's requirement affects a fight.
 *
 * Every river map requires some Arcane Force, and how much of it the character
 * has scales both the damage they deal and the damage they take: a tenth of
 * their damage and 2.8x the monster's at the bottom, 1.5x theirs and none of
 * the monster's at the top. Arcane Symbols provide it.
 *
 * Pure math over the protos. What the character actually wears is up to
 * CharacterInstance; see CharacterInstance::arcane_force.
 */
#ifndef MS_SRC_CHARACTER_ARCANE_FORCE_H_
#define MS_SRC_CHARACTER_ARCANE_FORCE_H_

#include <cstdint>

#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"

namespace ms {

// A symbol's max level. Past this it takes no more duplicates and its Arcane
// Force stops rising.
inline constexpr int kMaxSymbolLevel = 20;

// Whether `proto` is one of the six Arcane Symbols, determined by the symbol
// block, which nothing else has.
bool IsArcaneSymbol(const EquipPrototype& proto);

// The level `item` is at, which is 1 for a new drop. Read through this instead
// of the field, so the zero every drop has means the level every drop starts
// at.
int SymbolLevel(const Equip& item);

// Duplicates needed to go from `level` to the next: level^2 + 11. That is 12 at
// level 1, 372 at 19, and 2,679 in total. 0 at the max level, where there's no
// next level.
int SymbolExpToNextLevel(int level);

// Meso needed to level up from `level`. The price depends on the area and rises
// with the level: 10,000 x floor[(base + 0.1 x level) x duplicates], where the
// base goes from 8 in Vanishing Journey to 18 in Esfera. 0 at the max level.
int64_t SymbolLevelUpCost(const EquipPrototype& proto, int level);

// Arcane Force from a symbol at `level`: 10 per level, plus 20 for wearing one
// at all. So a new symbol gives 30 and a maxed one 220.
int SymbolArcaneForce(int level);

// What `item` is worth when fed to another symbol: itself, plus every duplicate
// used for the levels it has, plus its EXP beyond them. So a new symbol is
// worth 1, and one packed to level 2 with 7 EXP is worth 20.
int SymbolWorth(const Equip& item);

// Whether `item` has the duplicates its next level needs. The remaining cost is
// meso, which the player pays; see SymbolLevelUpCost.
bool SymbolCanLevelUp(const Equip& item);

// Raises `item` one level and carries over the extra EXP. Does nothing to a
// symbol without enough EXP or at the max level; charging for it is the
// caller's job.
void LevelUpSymbol(Equip& item);

// The stats a worn symbol gives: 10 of the wearer's primary stat per point of
// Arcane Force, which is GMS's 100 per 10. A symbol gives nothing else, so its
// prototype has no base stats.
EquipStats SymbolStatsFor(StatField primary, int level);

// How meeting a map's force requirement (Arcane Force here, Sacred Power in
// sacred_power.h) affects the fight, as two multipliers. Both are 1 on maps
// with no requirement.
struct ForceFactors {
  double damage_dealt = 1.0;
  double damage_taken = 1.0;
};

// The factors for `owned` Arcane Force on a map requiring `required`. Follows
// GMS's table, stepped by the whole percent met, rounded down: 0.10 dealt and
// 2.8x taken with none met, 1.50 and 0x at 150%.
ForceFactors ArcaneFactorsFor(int owned, int required);

}  // namespace ms

#endif  // MS_SRC_CHARACTER_ARCANE_FORCE_H_
