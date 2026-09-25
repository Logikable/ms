#include "src/map_force.h"

#include <random>
#include <utility>

#include "gtest/gtest.h"
#include "src/character/character.h"
#include "src/protos/character.pb.h"
#include "src/protos/map.pb.h"

namespace ms {
namespace {

// Each map is read against the force it names, and a map naming neither
// takes nothing.
TEST(MapForceTest, ReadsTheForceTheMapNames) {
  std::mt19937 rng(1);
  Character proto;
  proto.set_level(260);
  CharacterInstance character(rng, std::move(proto));

  MapData river;
  river.set_arcane_force(30);
  MapForce arcane = MapForceFor(river, character);
  EXPECT_EQ(arcane.name, "Arcane Force");
  EXPECT_EQ(arcane.abbreviation, "AF");
  EXPECT_EQ(arcane.required, 30);
  EXPECT_DOUBLE_EQ(arcane.factors.damage_dealt, 0.10);
  EXPECT_TRUE(AsksForForce(river));

  MapData grandis;
  grandis.set_sacred_power(30);
  MapForce sacred = MapForceFor(grandis, character);
  EXPECT_EQ(sacred.name, "Sacred Power");
  EXPECT_EQ(sacred.abbreviation, "SAC");
  EXPECT_EQ(sacred.owned, 0);
  EXPECT_DOUBLE_EQ(sacred.factors.damage_dealt, 0.70);
  EXPECT_TRUE(AsksForForce(grandis));

  MapData field;
  MapForce none = MapForceFor(field, character);
  EXPECT_TRUE(none.name.empty());
  EXPECT_EQ(none.required, 0);
  EXPECT_DOUBLE_EQ(none.factors.damage_dealt, 1.0);
  EXPECT_DOUBLE_EQ(none.factors.damage_taken, 1.0);
  EXPECT_FALSE(AsksForForce(field));
}

}  // namespace
}  // namespace ms
