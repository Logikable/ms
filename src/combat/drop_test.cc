#include "src/combat/drop.h"

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <string>

#include "src/game_state.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/map.pb.h"
#include "src/protos/mob.pb.h"
#include "src/protos/scroll.pb.h"

namespace ms {
namespace {

// A catalog holding one of each thing a boss table can name.
std::unique_ptr<GameState> MakeState() {
  EquipPrototype ring;
  ring.set_name("Silver Blossom Ring");
  ItemPrototype shard;
  shard.set_name("Zakum's Soul Shard");
  shard.set_kind(ITEM_KIND_SOUL_SHARD);
  ItemPrototype token;
  token.set_name("Cygnus Shoulder Token");
  token.set_kind(ITEM_KIND_TOKEN);
  return std::make_unique<GameState>(
      std::map<std::string, EquipPrototype>{{"ring", ring}},
      std::map<std::string, Scroll>{},
      std::map<std::string, ItemPrototype>{{"shard", shard}, {"token", token}},
      std::map<std::string, Mob>{}, std::map<std::string, MapData>{});
}

MobDrop Item(const std::string& key) {
  MobDrop drop;
  drop.set_item(key);
  return drop;
}

MobDrop Equip(const std::string& key) {
  MobDrop drop;
  drop.set_equip(key);
  return drop;
}

TEST(DropNameTest, ReadsWhicheverCatalogHoldsIt) {
  std::unique_ptr<GameState> state = MakeState();
  EXPECT_EQ(DropName(*state, Equip("ring")), "Silver Blossom Ring");
  EXPECT_EQ(DropName(*state, Item("shard")), "Zakum's Soul Shard");
  EXPECT_EQ(DropName(*state, Equip("nothing")), "");
  EXPECT_EQ(DropName(*state, Item("nothing")), "");
}

// The gear and what buys it are the prize; the shard is what the clear pays.
TEST(DropIsPrizeTest, GearAndTokensAreThePrize) {
  std::unique_ptr<GameState> state = MakeState();
  EXPECT_TRUE(DropIsPrize(*state, Equip("ring")));
  EXPECT_TRUE(DropIsPrize(*state, Item("token")));
  EXPECT_FALSE(DropIsPrize(*state, Item("shard")));
  EXPECT_FALSE(DropIsPrize(*state, Item("nothing")));
}

}  // namespace
}  // namespace ms
