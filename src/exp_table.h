#ifndef MS_SRC_EXP_TABLE_H_
#define MS_SRC_EXP_TABLE_H_

#include <cstdint>

namespace ms {

constexpr int kMaxLevel = 300;

// Returns EXP required to advance from `level` to `level + 1`.
// Returns 0 for level < 1 or level >= kMaxLevel.
int64_t ExpToNextLevel(int level);

}  // namespace ms

#endif  // MS_SRC_EXP_TABLE_H_
