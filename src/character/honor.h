/* Where honor comes from: the currency Inner Ability resets cost.
 *
 * All three sources are here instead of next to the code that pays them. How
 * long a player waits for a reroll depends on their sum, so the sum should be
 * readable in one place.
 *
 * Level-ups pay by far the most, a boss pays a flat amount once a day, and
 * monsters have a small chance of paying a little. Nothing increases any of
 * them: item drop rate doesn't affect the monster roll, since the currency that
 * rerolls a drop rate line shouldn't pay faster for having one.
 *
 * Pure math, like exp_table.h; the caller adds the honor to the character.
 */
#ifndef MS_SRC_CHARACTER_HONOR_H_
#define MS_SRC_CHARACTER_HONOR_H_

#include <cstdint>
#include <random>

#include "src/character/inner_ability.h"

namespace ms {

// Honor for one level-up: 700 up to level 60, plus 100 for every ten-level band
// above that, so 800 in the sixties and 900 in the seventies. It pays for the
// level reached, so reaching 60 pays 800.
int64_t HonorForLevelUp(int level);

// The same for going from `from_level` to `to_level`, which one offline period
// can span several bands. Zero if no level was gained.
int64_t HonorForLevels(int from_level, int to_level);

// Honor for clearing a boss, whatever the boss or difficulty: the reward is for
// the day's clear, and the lockout covers every difficulty together.
inline constexpr int64_t kBossClearHonor = 1500;

// The chance a kill pays honor, and how much one payment is.
inline constexpr double kMobHonorChance = 0.05;
inline constexpr int64_t kMobHonorPerDrop = 10;

// Average honor per kill, for sims that use the rate instead of rolling.
inline constexpr double kMobHonorPerKill = kMobHonorChance * kMobHonorPerDrop;

// Whether to show honor at all. It is earned from level 1, but Inner Ability,
// the only thing that spends it, opens at 160. Checked against the account too:
// a player whose main has reached 160 already knows what it's for.
bool HonorVisible(int character_level, int account_level);

// The honor `kills` actually paid. One roll for the whole batch, like meso
// drops, since nothing later needs to know which kills paid.
int64_t RollMobHonor(int64_t kills, std::mt19937& rng);

}  // namespace ms

#endif  // MS_SRC_CHARACTER_HONOR_H_
