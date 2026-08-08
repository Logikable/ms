#include "src/item/shop.h"

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "src/character/exp_table.h"
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
  e.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  return e;
}

EquipPrototype MakeItem(const std::string& name, int level, int price,
                        EquipType type) {
  EquipPrototype e = MakeItem(name, level, price);
  e.set_equip_type(type);
  return e;
}

TEST(ShopTest, StocksOnlyPricedItems) {
  std::map<std::string, EquipPrototype> equips{
      {"free", MakeItem("Free", 10, 0)},
      {"sold", MakeItem("Sold", 10, 5000)},
  };
  EXPECT_EQ(ShopStock(equips), std::vector<std::string>{"sold"});
}

// The four keys of the sort, checked one at a time: each case leaves every
// earlier key equal so only the one under test can decide the order, and every
// case runs the catalog keys the opposite way from the answer, or sorting on
// the key would give the same result and none of this would be testing
// anything.
TEST(ShopTest, SortsByLevelBeforeAnythingElse) {
  // The dearer item comes first on its lower level, against its type, its
  // price and its name.
  std::map<std::string, EquipPrototype> equips{
      {"a", MakeItem("Anvil", 30, 5000, EQUIP_TYPE_BOW)},
      {"b", MakeItem("Zebra", 10, 9000, EQUIP_TYPE_SPEAR)},
  };
  std::vector<std::string> expected{"b", "a"};
  EXPECT_EQ(ShopStock(equips), expected);
}

TEST(ShopTest, SortsByWeaponTypeWithinALevel) {
  std::map<std::string, EquipPrototype> equips{
      {"a", MakeItem("Anvil", 10, 5000, EQUIP_TYPE_SPEAR)},
      {"b", MakeItem("Zebra", 10, 5000, EQUIP_TYPE_BOW)},
  };
  std::vector<std::string> expected{"b", "a"};
  EXPECT_EQ(ShopStock(equips), expected);
}

TEST(ShopTest, SortsByPriceWithinAWeaponType) {
  std::map<std::string, EquipPrototype> equips{
      {"a", MakeItem("Anvil", 10, 9000, EQUIP_TYPE_BOW)},
      {"b", MakeItem("Zebra", 10, 5000, EQUIP_TYPE_BOW)},
  };
  std::vector<std::string> expected{"b", "a"};
  EXPECT_EQ(ShopStock(equips), expected);
}

TEST(ShopTest, SortsByNameWithinAPrice) {
  std::map<std::string, EquipPrototype> equips{
      {"a", MakeItem("Zebra", 10, 5000, EQUIP_TYPE_BOW)},
      {"b", MakeItem("Anvil", 10, 5000, EQUIP_TYPE_BOW)},
  };
  std::vector<std::string> expected{"b", "a"};
  EXPECT_EQ(ShopStock(equips), expected);
}

// The shipped catalog, so the stock the player sees is pinned rather than
// whatever the data files happen to say. The stars sit in their own level's
// tier rather than at the end: they undercut the weapons they are listed
// beside because they are ammunition, not the weapon a character is built
// around.
TEST(ShopTest, ShippedStockIsFiftyFourWeapons) {
  std::map<std::string, EquipPrototype> equips = LoadEquips();
  std::vector<std::string> stock = ShopStock(equips);
  std::vector<std::pair<std::string, int>> listing;
  for (const std::string& key : stock) {
    listing.push_back({equips.at(key).name(), equips.at(key).shop_price()});
  }
  std::vector<std::pair<std::string, int>> expected{
      // Level 10.
      {"Long Sword", 5000},
      {"War Bow", 5000},
      {"Wooden Staff", 5000},
      {"Fruit Knife", 5000},
      {"Garnier", 5000},
      {"Subi Throwing-Stars", 1000},
      // Level 20.
      {"Machete", 10000},
      {"Hunter's Bow", 10000},
      {"Old Wooden Staff", 10000},
      {"Coconut Knife", 10000},
      {"Steel Igor", 10000},
      // Level 30 -- where the 2nd-job warrior weapons start, so the warriors
      // outnumber everyone else from here down.
      {"Gladius", 20000},
      {"Ryden", 20000},
      {"Circle-Winded Staff", 20000},
      {"Reef Claw", 20000},
      {"Steel Guards", 20000},
      {"Kumbi Throwing-Stars", 10000},
      {"Blue Axe", 20000},
      {"Forked Spear", 20000},
      {"Mithril Polearm", 20000},
      {"Mithril Maul", 20000},
      {"Scimitar", 20000},
      {"Eagle Crow", 20000},
      // Level 40.
      {"Vaulter 2000", 30000},
      {"Hall Staff", 30000},
      {"Dragon Toenail", 30000},
      {"Steel Avarice", 30000},
      {"Sabretooth", 30000},
      {"Zeco", 30000},
      {"Crescent Polearm", 30000},
      {"Titan", 30000},
      {"Zard", 30000},
      {"Silver Crow", 30000},
      // Level 50.
      {"Olympus", 50000},
      {"Mystic Cane", 50000},
      {"Sai", 50000},
      {"Steel Slain", 50000},
      {"Steely Throwing-Knives", 25000},
      {"The Rising", 50000},
      {"Serpent's Tongue", 50000},
      {"The Nine Dragons", 50000},
      {"Golden Mole", 50000},
      {"Lion's Fang", 50000},
      {"Rower", 50000},
      // Level 60 -- the last tier the trial cap can reach.
      {"Asianic Bow", 75000},
      {"Frantic Crow Staff", 75000},
      {"Deadly Fin", 75000},
      {"Dark Gigantic", 75000},
      {"The Shining", 75000},
      {"Holy Spear", 75000},
      {"Skylar", 75000},
      {"The Blessing", 75000},
      {"Sparta", 75000},
      {"Golden Crow", 75000},
  };
  EXPECT_EQ(listing, expected);
}

// Nothing on sale is out of reach. The trial stops handing out EXP at 60, so
// a weapon above it is one the shop takes meso for and the player can never
// hold -- and an endgame weapon appearing here would be a balance change
// nobody asked for.
TEST(ShopTest, NothingAboveTheTrialCapIsForSale) {
  std::map<std::string, EquipPrototype> equips = LoadEquips();
  for (const std::string& key : ShopStock(equips)) {
    EXPECT_LE(equips.at(key).required_level(), kTrialLevelCap)
        << key << " is for sale";
  }
}

}  // namespace
}  // namespace ms
