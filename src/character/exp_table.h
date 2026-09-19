#ifndef MS_SRC_CHARACTER_EXP_TABLE_H_
#define MS_SRC_CHARACTER_EXP_TABLE_H_

#include <cstdint>

namespace ms {

constexpr int kMaxLevel = 300;

// Where this release stops handing out EXP; the levels above are in the table
// and out of reach. It sits at the top of the MAP ladder, so move it with the
// content -- it went 30 -> 60 with the 2nd jobs and 200 -> 230 with Esfera.
constexpr int kTrialLevelCap = 230;

// Where Burning stops: a character behind the rest of the account climbs
// several levels at a time up to here and one at a time above it. Moved by
// hand when the cap moves -- it trails it, but not by a fixed distance.
constexpr int kBurningLevel = 200;

// Returns EXP required to advance from `level` to `level + 1`.
// Returns 0 for level < 1 or level >= kMaxLevel.
int64_t ExpToNextLevel(int level);

}  // namespace ms

#endif  // MS_SRC_CHARACTER_EXP_TABLE_H_
