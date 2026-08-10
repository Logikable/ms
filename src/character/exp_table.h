#ifndef MS_SRC_CHARACTER_EXP_TABLE_H_
#define MS_SRC_CHARACTER_EXP_TABLE_H_

#include <cstdint>

namespace ms {

constexpr int kMaxLevel = 300;

// Where this release stops handing out EXP. The game is shipping as a trial,
// so a character stops earning here -- the levels above are in the table and
// simply out of reach. Raise it or delete it when the trial ends; kMaxLevel is
// the real ceiling waiting behind it.
//
// It sits at the top of the map ladder: 100 is where the maps stop, and a
// character who cannot be paid past the last map has nothing left to do. Move
// it with the content, as it moved 30 -> 60 when the 2nd jobs landed.
//
// The 61-100 maps have no 3rd job book and no weapon tier behind them yet, so
// the climb over them is a 2nd job in level 60 gear. That is a content gap,
// not a cap problem -- see //analysis:level_sim for what it costs.
constexpr int kTrialLevelCap = 100;

// Returns EXP required to advance from `level` to `level + 1`.
// Returns 0 for level < 1 or level >= kMaxLevel.
int64_t ExpToNextLevel(int level);

}  // namespace ms

#endif  // MS_SRC_CHARACTER_EXP_TABLE_H_
