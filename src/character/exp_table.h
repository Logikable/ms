#ifndef MS_SRC_CHARACTER_EXP_TABLE_H_
#define MS_SRC_CHARACTER_EXP_TABLE_H_

#include <cstdint>

namespace ms {

constexpr int kMaxLevel = 300;

// Where this release stops handing out EXP. The game is shipping as a trial,
// so a character stops earning at level 30 -- the levels above it are in the
// table and simply out of reach. Raise it or delete it when the trial ends;
// kMaxLevel is the real ceiling waiting behind it.
constexpr int kTrialLevelCap = 30;

// Returns EXP required to advance from `level` to `level + 1`.
// Returns 0 for level < 1 or level >= kMaxLevel.
int64_t ExpToNextLevel(int level);

}  // namespace ms

#endif  // MS_SRC_CHARACTER_EXP_TABLE_H_
