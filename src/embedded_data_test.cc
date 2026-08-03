#include "src/embedded_data.h"

#include <gtest/gtest.h>

#include <map>
#include <string>

#include "src/proto_loader.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/map.pb.h"
#include "src/protos/mob.pb.h"
#include "src/protos/scroll.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {
namespace {

// What the shipped binary carries, checked here rather than left to the first
// player to find. An accessor wired to the wrong filegroup, or to none, still
// compiles and still hands back a map -- so each one is asked for something it
// is supposed to hold.

TEST(EmbeddedDataTest, EquipsParse) {
  std::map<std::string, EquipPrototype> equips =
      LoadTextProtoMap<EquipPrototype>(EmbeddedEquips());
  ASSERT_TRUE(equips.count("sword") > 0);
  EXPECT_EQ(equips["sword"].equip_slot(), EQUIP_SLOT_PRIMARY_WEAPON);
}

TEST(EmbeddedDataTest, ItemsParse) {
  std::map<std::string, ItemPrototype> items =
      LoadTextProtoMap<ItemPrototype>(EmbeddedItems());
  ASSERT_TRUE(items.count("level_up") > 0);
  EXPECT_EQ(items["level_up"].effect(), ITEM_EFFECT_LEVEL_UP);
}

TEST(EmbeddedDataTest, MapsParse) {
  std::map<std::string, MapData> maps =
      LoadTextProtoMap<MapData>(EmbeddedMaps());
  // The map a new character starts on: without it, play mode has nowhere to be.
  EXPECT_TRUE(maps.count("maple_island") > 0);
}

TEST(EmbeddedDataTest, MobsParse) {
  std::map<std::string, Mob> mobs = LoadTextProtoMap<Mob>(EmbeddedMobs());
  EXPECT_FALSE(mobs.empty());
}

TEST(EmbeddedDataTest, ScrollsParse) {
  std::map<std::string, Scroll> scrolls =
      LoadTextProtoMap<Scroll>(EmbeddedScrolls());
  EXPECT_FALSE(scrolls.empty());
}

TEST(EmbeddedDataTest, SkillsParse) {
  std::map<std::string, Skill> skills =
      LoadTextProtoMap<Skill>(EmbeddedSkills());
  EXPECT_FALSE(skills.empty());
}

}  // namespace
}  // namespace ms
