/* Familiars: a fixed roster the account levels with the kills its characters
 * make, each with two rolled lines, three summoned at a time.
 *
 * GMS's card drops, badges and duplicate fusion are gone. Every familiar is
 * the account's from the start; farm kills fill one EXP pool, which the
 * player spends on whichever familiar they like. A familiar's level is its
 * rank, and every level-up rolls its lines afresh at the new rank, so a reroll
 * below Legendary is thrown away by the next level. The Familiar Cube rerolls
 * both lines for meso without changing the rank.
 *
 * What GMS's badges granted is a beginner skill whose level follows the total
 * of the account's familiar levels; see FamiliarSkillLevel.
 *
 * Pure rules, like inner_ability.h; the account and character hold the state.
 */
#ifndef MS_SRC_CHARACTER_FAMILIAR_H_
#define MS_SRC_CHARACTER_FAMILIAR_H_

#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "absl/types/span.h"
#include "google/protobuf/repeated_ptr_field.h"
#include "src/character/stat_preset.h"
#include "src/item/potential.h"
#include "src/protos/equip.pb.h"
#include "src/protos/familiar.pb.h"

namespace ms {

// The account level Familiars open at. Kills before it add nothing.
inline constexpr int kFamiliarsLevel = 190;

inline constexpr int kMaxSummonedFamiliars = 3;
// Level 1 is Rare and each level a rank higher, so 4 is Legendary.
inline constexpr int kFamiliarMaxLevel = 4;
inline constexpr int kFamiliarLines = 2;
// The second line is at the familiar's rank this often and a rank below
// otherwise. A Rare familiar has no rank below, so both its lines are Rare.
inline constexpr double kFamiliarPrimeChance = 0.10;

// The meso one Familiar Cube costs, at any rank. A placeholder the user means
// to tune.
inline constexpr int64_t kFamiliarCubeMeso = 3'000'000;

// The boss drop rate line's +100%, read only for boss drops.
inline constexpr double kFamiliarBossDropPct = 1.00;

// Boss damage from summoned familiars together stops here.
inline constexpr double kFamiliarBossDamageCap = 1.20;

// One familiar on the roster, and the level of the monster it is, which only
// orders the list.
struct FamiliarSpecies {
  const char* name;
  int mob_level;
};

// Every familiar, from the lowest-level monster up. Eight of them (Bubbling,
// Jr. Wraith, Jr. Cellion, Rash, Yeti, Werewolf, Memory Guardian and Mutant
// Orange Mushroom) are not on any map here; they exist only as familiars.
absl::Span<const FamiliarSpecies> FamiliarRoster();
bool IsFamiliar(const std::string& name);

// The EXP that raises a familiar from `level - 1` to `level`. Each step is
// paid on its own, so Legendary costs the four together. 0 outside 1 to
// kFamiliarMaxLevel.
int64_t FamiliarLevelCost(int level);

// The rank a familiar of `level` rolls at, or unspecified at level 0.
PotentialRank FamiliarRank(int level);

// What `type` grants at `rank`: flat points for the flat lines, whole percents
// for the rest. 0 where the rank doesn't offer it.
int FamiliarLineValue(FamiliarLineType type, PotentialRank rank);

// The lines a roll at `rank` picks from, all equally likely.
std::vector<FamiliarLineType> FamiliarPool(PotentialRank rank);

// Two fresh lines for a familiar of `rank`.
std::vector<FamiliarLine> RollFamiliarLines(PotentialRank rank,
                                            std::mt19937& rng);

// The account's entry for `name`, or nullptr if it was never levelled.
const Familiar* FindFamiliar(const FamiliarBook& book, const std::string& name);
int FamiliarLevel(const FamiliarBook& book, const std::string& name);

// Whether the pool holds enough to raise `name` one level, and it isn't at
// the top.
bool CanLevelFamiliar(const FamiliarBook& book, const std::string& name);
// Spends the pool to raise `name` one level and rolls its lines at the new
// rank. Refuses, changing nothing, when CanLevelFamiliar is false.
bool LevelUpFamiliar(FamiliarBook& book, const std::string& name,
                     std::mt19937& rng);
// Rerolls both lines at the familiar's rank. Refuses a familiar never
// levelled. The meso is the caller's to take.
bool CubeFamiliar(FamiliarBook& book, const std::string& name,
                  std::mt19937& rng);

// Every familiar's level, added up: what the beginner skill reads.
int TotalFamiliarLevels(const FamiliarBook& book);
// The beginner skill's level for `total_levels`. Six levels, at a total of 1,
// 3, 8, 20, 40 and 80: the last needs every familiar Legendary.
int FamiliarSkillLevel(int total_levels);

// What the summoned familiars give together. Shaped like worn potential, since
// their lines behave the same way: a %stat line scales the same pile a
// potential's does.
struct FamiliarTotals {
  PotentialTotals lines;
  // Flat attack and magic attack, which PotentialTotals has no room for.
  int attack = 0;
  // Whether any summoned familiar has the boss drop rate line.
  bool boss_drop = false;
};
FamiliarTotals SummonedFamiliarTotals(
    const FamiliarBook& book,
    const google::protobuf::RepeatedPtrField<std::string>& summoned);

// The familiars `slot` summons. An unopened preset is empty.
const FamiliarPreset& PresetOf(const SummonedFamiliars& summoned,
                               StatPreset slot);
// The same, first padding the list to kNumStatPresets.
FamiliarPreset& PresetOf(SummonedFamiliars& summoned, StatPreset slot);

}  // namespace ms

#endif  // MS_SRC_CHARACTER_FAMILIAR_H_
