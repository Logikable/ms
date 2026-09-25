#include "src/spawn.h"

#include <gtest/gtest.h>

#include "src/protos/mob.pb.h"

namespace ms {
namespace {

// A map gives a count; a boss phase lists a spot per monster and uses the
// number of spots as the count.
TEST(SpawnCountTest, SpotsWinOverTheCount) {
  Spawn map_spawn;
  map_spawn.set_count(6);
  EXPECT_EQ(SpawnCount(map_spawn), 6);

  Spawn phase;
  phase.add_spots();
  phase.add_spots();
  EXPECT_EQ(SpawnCount(phase), 2);

  // Nothing should set both, but if something does, the spots describe the
  // arena and the count can't be honoured anyway.
  phase.set_count(9);
  EXPECT_EQ(SpawnCount(phase), 2);
}

TEST(SpawnCountTest, AnEmptySpawnPutsOutNothing) {
  EXPECT_EQ(SpawnCount(Spawn()), 0);
}

}  // namespace
}  // namespace ms
