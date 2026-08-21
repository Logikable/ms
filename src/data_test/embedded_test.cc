#include <gtest/gtest.h>

#include <map>
#include <string>

#include "src/embedded_data.h"
#include "src/item/item.h"
#include "src/proto_loader.h"
#include "src/protos/boss.pb.h"
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
  // One drop out of each band's folder. level_up alone sits at the top of
  // data/items, so it would still be here if the glob stopped recursing and
  // every Etc drop in the game quietly went missing.
  EXPECT_TRUE(items.count("green_snail_shell") > 0);
  EXPECT_TRUE(items.count("wooden_board") > 0);
}

// Spell traces are bought, never sold. They are the currency scrolling is paid
// in, so a sell price on them would be a way to turn the game's largest meso
// sink back into meso. Pinned here because the rule is enforced by a line the
// data file does not have, and nothing else would notice it appearing.
TEST(EmbeddedDataTest, SpellTracesCannotBeSold) {
  std::map<std::string, ItemPrototype> items =
      LoadTextProtoMap<ItemPrototype>(EmbeddedItems());
  ASSERT_TRUE(items.count("spell_trace") > 0);
  EXPECT_EQ(items["spell_trace"].name(), kSpellTraceName);
  EXPECT_EQ(items["spell_trace"].sell_price(), 0);
  EXPECT_GT(items["spell_trace"].shop_price(), 0);
}

TEST(EmbeddedDataTest, MapsParse) {
  std::map<std::string, MapData> maps =
      LoadTextProtoMap<MapData>(EmbeddedMaps());
  // The map a new character starts on: without it, play mode has nowhere to be.
  EXPECT_TRUE(maps.count("maple_island") > 0);
}

TEST(EmbeddedDataTest, BossesParse) {
  std::map<std::string, Boss> bosses = LoadTextProtoMap<Boss>(EmbeddedBosses());
  EXPECT_TRUE(bosses.count("zakum") > 0);
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
