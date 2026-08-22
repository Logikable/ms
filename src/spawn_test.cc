#include "src/spawn.h"

#include <gtest/gtest.h>

#include "src/protos/mob.pb.h"

namespace ms {
namespace {

// A map writes a count; a boss phase writes a spot per monster and lets the
// spots be the count.
TEST(SpawnCountTest, SpotsWinOverTheCount) {
  Spawn map_spawn;
  map_spawn.set_count(6);
  EXPECT_EQ(SpawnCount(map_spawn), 6);

  Spawn phase;
  phase.add_spots();
  phase.add_spots();
  EXPECT_EQ(SpawnCount(phase), 2);

  // Nothing should write both, but if something does the spots are the arena's
  // own answer and the count cannot be honoured anyway.
  phase.set_count(9);
  EXPECT_EQ(SpawnCount(phase), 2);
}

TEST(SpawnCountTest, AnEmptySpawnPutsOutNothing) {
  EXPECT_EQ(SpawnCount(Spawn()), 0);
}

}  // namespace
}  // namespace ms
