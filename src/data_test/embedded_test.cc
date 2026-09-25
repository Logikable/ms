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

// Checks what the shipped binary contains. An accessor wired to the wrong
// filegroup still compiles and returns a map, so each is asked for something it
// should contain.

TEST(EmbeddedDataTest, EquipsParse) {
  std::map<std::string, EquipPrototype> equips =
      LoadTextProtoMap<EquipPrototype>(EmbeddedEquips());
  ASSERT_TRUE(equips.count("sword") > 0);
  EXPECT_EQ(equips["sword"].equip_slot(), EQUIP_SLOT_PRIMARY_WEAPON);
}

TEST(EmbeddedDataTest, ItemsParse) {
  std::map<std::string, ItemPrototype> items =
      LoadTextProtoMap<ItemPrototype>(EmbeddedItems());
  // One drop from each band's folder. Every item is in a subfolder, so a glob
  // that stopped recursing would return an empty map, not a short one.
  EXPECT_TRUE(items.count("green_snail_shell") > 0);
  EXPECT_TRUE(items.count("wooden_board") > 0);
  EXPECT_TRUE(items.count("spell_trace") > 0);
}

// Spell traces are the currency scrolling is paid in, so a sell price would
// turn the game's biggest meso sink back into meso. Nothing else would notice
// one being added.
TEST(EmbeddedDataTest, SpellTracesAreWorthNothingAtTheCounter) {
  std::map<std::string, ItemPrototype> items =
      LoadTextProtoMap<ItemPrototype>(EmbeddedItems());
  ASSERT_TRUE(items.count("spell_trace") > 0);
  EXPECT_EQ(items["spell_trace"].name(), kSpellTraceName);
  EXPECT_EQ(items["spell_trace"].sell_price(), 0);
  EXPECT_GT(items["spell_trace"].shop_price(), 0);
}

TEST(EmbeddedDataTest, EveryOtherCatalogParses) {
  // The map new characters start on. Without it, play mode has nowhere to go.
  EXPECT_TRUE(LoadTextProtoMap<MapData>(EmbeddedMaps()).count("maple_island") >
              0);
  EXPECT_TRUE(LoadTextProtoMap<Boss>(EmbeddedBosses()).count("zakum") > 0);
  EXPECT_FALSE(LoadTextProtoMap<Mob>(EmbeddedMobs()).empty());
  EXPECT_FALSE(LoadTextProtoMap<Scroll>(EmbeddedScrolls()).empty());
  EXPECT_FALSE(LoadTextProtoMap<Skill>(EmbeddedSkills()).empty());
}

}  // namespace
}  // namespace ms
