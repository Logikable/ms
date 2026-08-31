/* Where honor comes from: the currency an Inner Ability reset is paid in.
 *
 * Three sources, and all of them here rather than beside whatever hands them
 * over. How long a player waits for a reroll is one number, and it is the sum
 * of these -- so the sum should be readable in one place.
 *
 * A level pays by far the most, a boss pays a flat prize once a day, and a
 * monster pays a small chance at a little. Nothing lifts any of them: item
 * drop rate leaves the mob's roll alone, since the currency that rerolls a
 * drop-rate line ought not to be paid out faster for holding one.
 *
 * Pure math over the numbers, like exp_table.h. Adding the honor to the
 * character is the caller's.
 */
#ifndef MS_SRC_CHARACTER_HONOR_H_
#define MS_SRC_CHARACTER_HONOR_H_

#include <cstdint>
#include <random>

#include "src/character/inner_ability.h"

namespace ms {

// What one level-up pays: 700 up to level 60, and 100 more for every band of
// ten levels above it -- 800 through the sixties, 900 through the seventies.
// The level reached is what is paid for, so the climb to 60 pays 800.
int64_t HonorForLevelUp(int level);

// The same for a climb from `from_level` to `to_level`, which one idle stretch
// can carry across several bands. Zero for a climb that went nowhere.
int64_t HonorForLevels(int from_level, int to_level);

// What clearing a boss pays, whatever the boss and whichever difficulty was
// taken: the prize is for the day's clear, and the lockout holds every
// difficulty back together.
inline constexpr int64_t kBossClearHonor = 1500;

// The share of kills that pay honor at all, and what one payment is worth.
inline constexpr double kMobHonorChance = 0.05;
inline constexpr int64_t kMobHonorPerDrop = 10;

// What a kill is worth on average, for a sim reading the rate rather than
// rolling it.
inline constexpr double kMobHonorPerKill = kMobHonorChance * kMobHonorPerDrop;

// Whether honor should be shown to the player at all. It is earned from the
// first level, and Inner Ability -- the only thing that spends it -- opens at
// 160, so until then a number counting up explains nothing. Asked of the
// account as well as the character: a player whose main has been there knows
// what it is for, and their next character's honor is not a mystery to them.
bool HonorVisible(int character_level, int account_level);

// Honor `kills` actually paid. One roll over the batch, as the meso drop is:
// which kills paid is not a question anything downstream asks.
int64_t RollMobHonor(int64_t kills, std::mt19937& rng);

}  // namespace ms

#endif  // MS_SRC_CHARACTER_HONOR_H_
