#ifndef MS_SRC_CHARACTER_EXP_TABLE_H_
#define MS_SRC_CHARACTER_EXP_TABLE_H_

#include <cstdint>

namespace ms {

constexpr int kMaxLevel = 300;

// The level where this release stops giving EXP; higher levels are in the table
// but can't be reached. It matches the top of the map ladder, so move it with
// the content. It went from 30 to 60 with the 2nd jobs, 200 to 230 with Esfera,
// and 230 to 260 with Limina and Grandis.
constexpr int kTrialLevelCap = 260;

// Where Burning stops: a character behind the rest of the account gains several
// levels at a time up to here, and one at a time above it. Move it by hand when
// the cap moves; it trails the cap, but not by a fixed amount.
constexpr int kBurningLevel = 230;

// EXP required to advance from `level` to `level + 1`. Returns 0 for level < 1
// or level >= kMaxLevel.
int64_t ExpToNextLevel(int level);

}  // namespace ms

#endif  // MS_SRC_CHARACTER_EXP_TABLE_H_
