#include "src/map_level.h"

#include <gtest/gtest.h>

#include <map>
#include <string>

#include "src/protos/map.pb.h"
#include "src/protos/mob.pb.h"

namespace ms {
namespace {

std::map<std::string, Mob> MakeMobs() {
  std::map<std::string, Mob> mobs;
  mobs["snail"].set_level(2);
  mobs["golem"].set_level(60);
  return mobs;
}

Spawn* AddSpawn(MapData& map, const std::string& mob, int count) {
  Spawn* spawn = map.add_spawns();
  spawn->set_mob(mob);
  spawn->set_count(count);
  return spawn;
}

TEST(MapLevelTest, WeighsEachMobByHowManyOfItSpawn) {
  MapData map;
  AddSpawn(map, "snail", 9);
  AddSpawn(map, "golem", 1);
  // A single straggler must not pull the map up away from the crowd: the mean
  // is (9*2 + 60) / 10, not (2 + 60) / 2.
  EXPECT_DOUBLE_EQ(MapLevel(MakeMobs(), map), 7.8);
}

TEST(MapLevelTest, ATownHasNoLevel) {
  EXPECT_DOUBLE_EQ(MapLevel(MakeMobs(), MapData()), 0.0);
}

// A spawn the catalog has no file for is dropped by the loader, so it must not
// be counted here either -- level 0 in the mean would drag the map down.
TEST(MapLevelTest, SpawnsNoMobFileDefinesAreSkipped) {
  MapData map;
  AddSpawn(map, "golem", 1);
  AddSpawn(map, "nothing_by_that_name", 99);
  EXPECT_DOUBLE_EQ(MapLevel(MakeMobs(), map), 60.0);

  MapData unknown_only;
  AddSpawn(unknown_only, "nothing_by_that_name", 99);
  EXPECT_DOUBLE_EQ(MapLevel(MakeMobs(), unknown_only), 0.0);
}

// A boss phase names a spot per monster instead of a count, and MapLevel reads
// the same SpawnCount everything else does.
TEST(MapLevelTest, SpotsCountAsMonsters) {
  MapData map;
  Spawn* snails = AddSpawn(map, "snail", 0);
  snails->add_spots();
  snails->add_spots();
  snails->add_spots();
  AddSpawn(map, "golem", 1);
  EXPECT_DOUBLE_EQ(MapLevel(MakeMobs(), map), (3 * 2 + 60) / 4.0);
}

}  // namespace
}  // namespace ms
