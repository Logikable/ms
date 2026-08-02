#include "src/item/shop.h"

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "src/proto_loader.h"
#include "src/protos/equip.pb.h"
#include "tools/cpp/runfiles/runfiles.h"

namespace ms {
namespace {

using bazel::tools::cpp::runfiles::Runfiles;

std::map<std::string, EquipPrototype> LoadEquips() {
  std::string err;
  std::unique_ptr<Runfiles> runfiles(Runfiles::CreateForTest(&err));
  EXPECT_NE(runfiles, nullptr) << err;
  return LoadTextProtoDir<EquipPrototype>(runfiles->Rlocation("ms/data/equip"));
}

EquipPrototype MakeItem(const std::string& name, int level, int price) {
  EquipPrototype e;
  e.set_name(name);
  e.set_required_level(level);
  e.set_shop_price(price);
  return e;
}

TEST(ShopTest, StocksOnlyPricedItems) {
  std::map<std::string, EquipPrototype> equips{
      {"free", MakeItem("Free", 10, 0)},
      {"sold", MakeItem("Sold", 10, 5000)},
  };
  EXPECT_EQ(ShopStock(equips), std::vector<std::string>{"sold"});
}

// Level first so each price tier reads as a block, name second so the order is
// the one the player is reading down rather than however the catalog is keyed.
TEST(ShopTest, SortsByLevelThenName) {
  // Keys deliberately run the other way from names, or sorting on the key
  // would give the same answer and this would not be testing anything.
  std::map<std::string, EquipPrototype> equips{
      {"a", MakeItem("Zebra", 10, 5000)},
      {"b", MakeItem("Anvil", 10, 5000)},
      {"c", MakeItem("Apple", 20, 5000)},
  };
  std::vector<std::string> expected{"b", "a", "c"};
  EXPECT_EQ(ShopStock(equips), expected);
}

// The shipped catalog, so the stock the player sees is pinned rather than
// whatever the data files happen to say.
TEST(ShopTest, ShippedStockIsTheSeventeenWeapons) {
  std::map<std::string, EquipPrototype> equips = LoadEquips();
  std::vector<std::string> stock = ShopStock(equips);
  std::vector<std::pair<std::string, int>> listing;
  for (const std::string& key : stock) {
    listing.push_back({equips.at(key).name(), equips.at(key).shop_price()});
  }
  std::vector<std::pair<std::string, int>> expected{
      // Level 10. The stars undercut the weapons of their level: they are
      // ammunition, not the weapon a character is built around.
      {"Fruit Knife", 5000},
      {"Garnier", 5000},
      {"Long Sword", 5000},
      {"Subi Throwing-Stars", 1000},
      {"War Bow", 5000},
      {"Wooden Wand", 5000},
      // Level 20.
      {"Coconut Knife", 10000},
      {"Hunter's Bow", 10000},
      {"Machete", 10000},
      {"Metal Wand", 10000},
      {"Steel Igor", 10000},
      // Level 30.
      {"Gladius", 20000},
      {"Kumbi Throwing-Stars", 10000},
      {"Mithril Wand", 20000},
      {"Reef Claw", 20000},
      {"Ryden", 20000},
      {"Steel Guards", 20000},
  };
  EXPECT_EQ(listing, expected);
}

// Everything on sale is early-game gear. A shop that quietly picked up an
// endgame weapon would be a balance change nobody asked for.
TEST(ShopTest, NothingAboveLevelThirtyIsForSale) {
  std::map<std::string, EquipPrototype> equips = LoadEquips();
  for (const std::string& key : ShopStock(equips)) {
    EXPECT_LE(equips.at(key).required_level(), 30) << key << " is for sale";
  }
}

}  // namespace
}  // namespace ms
