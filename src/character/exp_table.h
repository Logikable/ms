#ifndef MS_SRC_CHARACTER_EXP_TABLE_H_
#define MS_SRC_CHARACTER_EXP_TABLE_H_

#include <cstdint>

namespace ms {

constexpr int kMaxLevel = 300;

// Where this release stops handing out EXP: the levels above are in the table
// and out of reach. It sits at the top of the map ladder, because a character
// who cannot be paid past the last map has nothing left to do -- so move it
// with the content, as it moved 30 -> 60 when the 2nd jobs landed.
constexpr int kTrialLevelCap = 100;

// Returns EXP required to advance from `level` to `level + 1`.
// Returns 0 for level < 1 or level >= kMaxLevel.
int64_t ExpToNextLevel(int level);

}  // namespace ms

#endif  // MS_SRC_CHARACTER_EXP_TABLE_H_
