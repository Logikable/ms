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
  e.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  return e;
}

EquipPrototype MakeItem(const std::string& name, int level, int price,
                        EquipSlot slot, EquipJobCategory job) {
  EquipPrototype e = MakeItem(name, level, price);
  e.set_equip_slot(slot);
  e.add_equip_job_categories(job);
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
TEST(ShopTest, SortsBySlotBeforeAnythingElse) {
  // The stars are cheaper, lower level, and earlier in the alphabet, so slot
  // is the only reason for them to come second.
  std::map<std::string, EquipPrototype> equips{
      {"a",
       MakeItem("Ammo", 10, 1000, EQUIP_SLOT_STARS, EQUIP_JOB_CATEGORY_THIEF)},
      {"b", MakeItem("Blade", 30, 5000, EQUIP_SLOT_PRIMARY_WEAPON,
                     EQUIP_JOB_CATEGORY_THIEF)},
  };
  std::vector<std::string> expected{"b", "a"};
  EXPECT_EQ(ShopStock(equips), expected);
}

TEST(ShopTest, SortsByLevelWithinASlot) {
  std::map<std::string, EquipPrototype> equips{
      {"a", MakeItem("Zebra", 10, 5000, EQUIP_SLOT_PRIMARY_WEAPON,
                     EQUIP_JOB_CATEGORY_THIEF)},
      {"b", MakeItem("Anvil", 20, 5000, EQUIP_SLOT_PRIMARY_WEAPON,
                     EQUIP_JOB_CATEGORY_THIEF)},
  };
  std::vector<std::string> expected{"a", "b"};
  EXPECT_EQ(ShopStock(equips), expected);
}

// Class order, not the alphabet: the warrior item comes first despite naming
// the later class and the later key.
TEST(ShopTest, SortsByClassOrderWithinALevel) {
  std::map<std::string, EquipPrototype> equips{
      {"a", MakeItem("Anvil", 10, 5000, EQUIP_SLOT_PRIMARY_WEAPON,
                     EQUIP_JOB_CATEGORY_THIEF)},
      {"b", MakeItem("Zebra", 10, 5000, EQUIP_SLOT_PRIMARY_WEAPON,
                     EQUIP_JOB_CATEGORY_WARRIOR)},
  };
  std::vector<std::string> expected{"b", "a"};
  EXPECT_EQ(ShopStock(equips), expected);
}

TEST(ShopTest, SortsByNameWithinAClass) {
  std::map<std::string, EquipPrototype> equips{
      {"a", MakeItem("Zebra", 10, 5000, EQUIP_SLOT_PRIMARY_WEAPON,
                     EQUIP_JOB_CATEGORY_THIEF)},
      {"b", MakeItem("Anvil", 10, 5000, EQUIP_SLOT_PRIMARY_WEAPON,
                     EQUIP_JOB_CATEGORY_THIEF)},
  };
  std::vector<std::string> expected{"b", "a"};
  EXPECT_EQ(ShopStock(equips), expected);
}

// An item naming no class sorts with the universal items, which is where the
// list displays it, rather than ahead of the warriors on a zero enum value.
TEST(ShopTest, SortsAnItemWithNoClassAsUniversal) {
  std::map<std::string, EquipPrototype> equips{
      {"a", MakeItem("Anything", 10, 5000)},
      {"b", MakeItem("Blade", 10, 5000, EQUIP_SLOT_PRIMARY_WEAPON,
                     EQUIP_JOB_CATEGORY_WARRIOR)},
  };
  std::vector<std::string> expected{"b", "a"};
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
      // Weapons, level 10, in class order.
      {"War Bow", 5000},
      {"Wooden Wand", 5000},
      {"Fruit Knife", 5000},
      {"Garnier", 5000},
      {"Long Sword", 5000},
      // Weapons, level 20.
      {"Machete", 10000},
      {"Hunter's Bow", 10000},
      {"Metal Wand", 10000},
      {"Coconut Knife", 10000},
      {"Steel Igor", 10000},
      // Weapons, level 30.
      {"Gladius", 20000},
      {"Ryden", 20000},
      {"Mithril Wand", 20000},
      {"Reef Claw", 20000},
      {"Steel Guards", 20000},
      // The stars, last because of the slot they go in. They undercut the
      // weapons of their level: they are ammunition, not the weapon a
      // character is built around.
      {"Subi Throwing-Stars", 1000},
      {"Kumbi Throwing-Stars", 10000},
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
