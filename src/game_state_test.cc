#include "src/game_state.h"

#include <gtest/gtest.h>

#include <map>
#include <string>

#include "src/protos/equip.pb.h"
#include "src/protos/map.pb.h"
#include "src/protos/mob.pb.h"
#include "src/protos/scroll.pb.h"

namespace ms {
namespace {

GameState MakeState() {
  return GameState({}, {}, {}, {});
}

TEST(GameStateTest, ConstructorStoresEquipsMap) {
  EquipPrototype proto;
  proto.set_name("Sword");
  GameState state({{"sword", proto}}, {}, {}, {});
  ASSERT_TRUE(state.equips.count("sword"));
  EXPECT_EQ(state.equips.at("sword").name(), "Sword");
}

TEST(GameStateTest, ConstructorStoresScrollsMap) {
  Scroll scroll;
  scroll.set_name("60% ATT");
  GameState state({}, {{"att_60", scroll}}, {}, {});
  ASSERT_TRUE(state.scrolls.count("att_60"));
  EXPECT_EQ(state.scrolls.at("att_60").name(), "60% ATT");
}

TEST(GameStateTest, ConstructorStoresMobsMap) {
  Mob mob;
  mob.set_name("Snail");
  GameState state({}, {}, {{"snail", mob}}, {});
  ASSERT_TRUE(state.mobs.count("snail"));
  EXPECT_EQ(state.mobs.at("snail").name(), "Snail");
}

TEST(GameStateTest, ConstructorStoresMapsMap) {
  MapData map;
  map.set_name("Right Around Lith Harbor");
  GameState state({}, {}, {}, {{"lith", map}});
  ASSERT_TRUE(state.maps.count("lith"));
  EXPECT_EQ(state.maps.at("lith").name(), "Right Around Lith Harbor");
}

TEST(GameStateTest, StartingCharacterIsLevel2) {
  GameState state = MakeState();
  EXPECT_EQ(state.character.proto().level(), 2);
}

TEST(GameStateTest, StartingCharacterJobIsBeginner) {
  GameState state = MakeState();
  EXPECT_EQ(state.character.proto().job(), JOB_BEGINNER);
}

TEST(GameStateTest, StartingCharacterHasFiveAp) {
  GameState state = MakeState();
  EXPECT_EQ(state.character.proto().ap(), 5);
}

TEST(GameStateTest, StartingCharacterStats) {
  GameState state = MakeState();
  const AllocatedStats& s = state.character.proto().allocated_stats();
  EXPECT_EQ(s.str(), 13);
  EXPECT_EQ(s.dex(), 4);
  EXPECT_EQ(s.int_(), 4);
  EXPECT_EQ(s.luk(), 4);
  EXPECT_EQ(s.hp(), 50);
  EXPECT_EQ(s.mp(), 15);
}

}  // namespace
}  // namespace ms
