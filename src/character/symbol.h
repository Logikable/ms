/* Arcane and Sacred Symbols: what a symbol at a level is worth, and what the
 * next level costs.
 *
 * Both kinds level by absorbing duplicates and then paying meso, and both grant
 * their area's force and a final primary stat. They differ in every number:
 * Arcane Symbols climb 20 levels on a gentle curve, Sacred Symbols 11 on a
 * steep one, and only a maxed Sacred Symbol adds anything beyond its stats.
 *
 * Pure math over the protos. What the character actually wears is up to
 * CharacterInstance; see CharacterInstance::arcane_force and sacred_power.
 */
#ifndef MS_SRC_CHARACTER_SYMBOL_H_
#define MS_SRC_CHARACTER_SYMBOL_H_

#include <cstdint>

#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"

namespace ms {

// Each kind's max level. Past it a symbol takes no more duplicates and its
// force stops rising.
inline constexpr int kMaxArcaneSymbolLevel = 20;
inline constexpr int kMaxSacredSymbolLevel = 11;

// What a maxed Sacred Symbol adds: EXP from every kill, and damage against the
// boss its area ends with (SacredSymbolInfo::boss), at every difficulty.
inline constexpr double kSacredMaxExpPct = 0.10;
inline constexpr double kSacredMaxBossDamagePct = 0.20;

// Which kind `proto` is, from its symbol block, which nothing else has.
bool IsSymbol(const EquipPrototype& proto);
bool IsArcaneSymbol(const EquipPrototype& proto);
bool IsSacredSymbol(const EquipPrototype& proto);

// The max level for `proto`'s kind; 0 for an item that isn't a symbol.
int SymbolMaxLevel(const EquipPrototype& proto);

// The level `item` is at, which is 1 for a new drop. Read through this instead
// of the field, so the zero every drop has means the level every drop starts
// at.
int SymbolLevel(const Equip& item);

// Whether `item` has reached its kind's max level.
bool SymbolMaxed(const EquipPrototype& proto, const Equip& item);

// Duplicates needed to go from `level` to the next, 0 at the max level. Arcane:
// level^2 + 11, 2,679 in total. Sacred: 9 x level^2 + 20 x level, 4,565 in
// total.
int SymbolExpToNextLevel(const EquipPrototype& proto, int level);

// Meso needed to level up from `level`, 0 at the max level. Arcane: 10,000 x
// floor[(base + 0.1 x level) x duplicates]. Sacred: 100,000 x floor[(base - 0.6
// x level) x duplicates]. The base rises with the area.
int64_t SymbolLevelUpCost(const EquipPrototype& proto, int level);

// The force a symbol at `level` gives: Arcane Force 10 per level plus 20 for
// wearing one at all, or Sacred Power 10 per level.
int SymbolForce(const EquipPrototype& proto, int level);

// What `item` is worth when fed to another symbol: itself, plus every duplicate
// used for the levels it has, plus its EXP beyond them. So a new symbol is
// worth 1, and an Arcane one packed to level 2 with 7 EXP is worth 20.
int SymbolWorth(const EquipPrototype& proto, const Equip& item);

// Whether `item` has the duplicates its next level needs. The remaining cost is
// meso, which the player pays; see SymbolLevelUpCost.
bool SymbolCanLevelUp(const EquipPrototype& proto, const Equip& item);

// Raises `item` one level and carries over the extra EXP. Does nothing to a
// symbol without enough EXP or at the max level; charging for it is the
// caller's job.
void LevelUpSymbol(const EquipPrototype& proto, Equip& item);

// The stats a worn symbol gives, all in the wearer's primary stat: 10 per point
// of Arcane Force (GMS's 100 per 10), or 500 for a Sacred Symbol plus 200 a
// level past the first. A symbol gives nothing else, so its prototype has no
// base stats.
EquipStats SymbolStatsFor(const EquipPrototype& proto, StatField primary,
                          int level);

}  // namespace ms

#endif  // MS_SRC_CHARACTER_SYMBOL_H_
