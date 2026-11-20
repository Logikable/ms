#ifndef MS_SRC_CHARACTER_EXP_TABLE_H_
#define MS_SRC_CHARACTER_EXP_TABLE_H_

#include <cstdint>

#include "src/build_config.h"
#include "src/protos/character.pb.h"

namespace ms {

constexpr int kMaxLevel = 300;

// The level where this release stops giving EXP; higher levels are in the table
// but can't be reached. It matches the top of the map ladder, so move it with
// the content. It went from 30 to 60 with the 2nd jobs, 200 to 230 with Esfera,
// and 230 to 260 with Limina and Grandis. A job whose 6th job is written goes
// on to kMaxLevel; see LevelCap.
constexpr int kTrialLevelCap = 260;

// Where Burning stops: a character behind the rest of the account gains several
// levels at a time up to here, and one at a time above it. Fixed at the 5th
// job's cap, whatever the build: past it, a job's own 6th job sets the pace.
constexpr int kBurningLevel = 260;

// Whether every HEXA node of `job`'s 6th job has its skill written. A data test
// holds this to the skill files.
bool SixthJobWritten(Job job);

// The level `job` stops gaining EXP at: kMaxLevel once the 6th job is in the
// game and the job's is written, kTrialLevelCap otherwise.
int LevelCap(Job job);

// EXP required to advance from `level` to `level + 1`. Returns 0 for level < 1
// or level >= kMaxLevel.
int64_t ExpToNextLevel(int level);

}  // namespace ms

#endif  // MS_SRC_CHARACTER_EXP_TABLE_H_
