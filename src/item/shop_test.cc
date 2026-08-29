#include "src/item/shop.h"

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include "src/character/exp_table.h"
#include "src/proto_loader.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"
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

// An item the shop does not stock, which is one that names no price at all --
// not one that names zero.
EquipPrototype MakeUnpriced(const std::string& name, int level) {
  EquipPrototype e = MakeItem(name, level, 0);
  e.clear_shop_price();
  return e;
}

EquipPrototype MakeItem(const std::string& name, int level, int price,
                        EquipType type) {
  EquipPrototype e = MakeItem(name, level, price);
  e.set_equip_type(type);
  return e;
}

// Naming a price is what stocks an item, and zero is a price: the shop hands
// one item over for nothing, and an item that says nothing is the one it does
// not sell.
TEST(ShopTest, StocksOnlyPricedItemsAndZeroIsAPrice) {
  std::map<std::string, EquipPrototype> equips{
      {"free", MakeItem("Free", 10, 0)},
      {"sold", MakeItem("Sold", 10, 5000)},
      {"unsold", MakeUnpriced("Unsold", 10)},
  };
  std::vector<std::string> expected{"free", "sold"};
  EXPECT_EQ(ShopWeaponStock(equips, kPaidInMeso), expected);
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
  EXPECT_EQ(ShopWeaponStock(equips, kPaidInMeso), expected);
}

TEST(ShopTest, SortsByWeaponTypeWithinALevel) {
  std::map<std::string, EquipPrototype> equips{
      {"a", MakeItem("Anvil", 10, 5000, EQUIP_TYPE_SPEAR)},
      {"b", MakeItem("Zebra", 10, 5000, EQUIP_TYPE_BOW)},
  };
  std::vector<std::string> expected{"b", "a"};
  EXPECT_EQ(ShopWeaponStock(equips, kPaidInMeso), expected);
}

TEST(ShopTest, SortsByPriceWithinAWeaponType) {
  std::map<std::string, EquipPrototype> equips{
      {"a", MakeItem("Anvil", 10, 9000, EQUIP_TYPE_BOW)},
      {"b", MakeItem("Zebra", 10, 5000, EQUIP_TYPE_BOW)},
  };
  std::vector<std::string> expected{"b", "a"};
  EXPECT_EQ(ShopWeaponStock(equips, kPaidInMeso), expected);
}

TEST(ShopTest, SortsByNameWithinAPrice) {
  std::map<std::string, EquipPrototype> equips{
      {"a", MakeItem("Zebra", 10, 5000, EQUIP_TYPE_BOW)},
      {"b", MakeItem("Anvil", 10, 5000, EQUIP_TYPE_BOW)},
  };
  std::vector<std::string> expected{"b", "a"};
  EXPECT_EQ(ShopWeaponStock(equips, kPaidInMeso), expected);
}

// The shipped shelves, checked pair by pair against the same four keys the
// header promises. Sortedness rather than a copy of the list: a copy has to be
// rewritten for every tier added, and says nothing the rule does not.
TEST(ShopTest, BothShelvesReadInColumnOrder) {
  std::map<std::string, EquipPrototype> equips = LoadEquips();
  for (const std::vector<std::string>& shelf :
       {ShopWeaponStock(equips, kPaidInMeso),
        ShopEquipStock(equips, kPaidInMeso)}) {
    ASSERT_FALSE(shelf.empty());
    for (int i = 1; i < static_cast<int>(shelf.size()); ++i) {
      const EquipPrototype& above = equips.at(shelf[i - 1]);
      const EquipPrototype& below = equips.at(shelf[i]);
      std::tuple<int, int, int, std::string> keys_above{
          above.required_level(), above.equip_type(), above.shop_price(),
          above.name()};
      std::tuple<int, int, int, std::string> keys_below{
          below.required_level(), below.equip_type(), below.shop_price(),
          below.name()};
      EXPECT_LT(keys_above, keys_below)
          << shelf[i - 1] << " is listed above " << shelf[i];
    }
  }
}

// What shares the weapon shelf. Stars belong on it, because they are what a
// claw swings and a tab holding one item is not a tab. Nothing worn does: it
// has a shelf of its own, and a medallion among the swords would read as
// something to swing. Level orders the shelf, so the stars land in their own
// tier rather than at the end.
TEST(ShopTest, TheWeaponShelfCarriesTheStarsAndNothingWorn) {
  std::map<std::string, EquipPrototype> equips = LoadEquips();
  int stars = 0;
  for (const std::string& key : ShopWeaponStock(equips, kPaidInMeso)) {
    EquipSlot slot = equips.at(key).equip_slot();
    EXPECT_TRUE(slot == EQUIP_SLOT_PRIMARY_WEAPON ||
                slot == EQUIP_SLOT_PROJECTILE)
        << key << " is on the weapon shelf";
    stars += slot == EQUIP_SLOT_PROJECTILE ? 1 : 0;
  }
  EXPECT_GT(stars, 0) << "the stars have fallen off the weapon shelf";
}

// The two shelves partition what the shop stocks: everything worn that is not
// swung or thrown is on the other one, so nothing can fall between them and
// nothing can sit on both.
TEST(ShopTest, TheEquipShelfHoldsEverythingTheWeaponShelfDoesNot) {
  std::map<std::string, EquipPrototype> equips = LoadEquips();
  std::set<std::string> shelved;
  for (Payment payment : {kPaidInMeso, kPaidInTokens}) {
    for (const std::string& key : ShopWeaponStock(equips, payment)) {
      EXPECT_TRUE(shelved.insert(key).second) << key << " is shelved twice";
    }
    for (const std::string& key : ShopEquipStock(equips, payment)) {
      EquipSlot slot = equips.at(key).equip_slot();
      EXPECT_NE(slot, EQUIP_SLOT_PRIMARY_WEAPON) << key;
      EXPECT_NE(slot, EQUIP_SLOT_PROJECTILE) << key;
      EXPECT_TRUE(shelved.insert(key).second) << key << " is shelved twice";
    }
  }
  int stocked = 0;
  for (const std::pair<const std::string, EquipPrototype>& entry : equips) {
    if (entry.second.has_shop_price() || entry.second.token_price() > 0) {
      ++stocked;
      EXPECT_EQ(shelved.count(entry.first), 1u)
          << entry.first << " is priced but on no shelf";
    }
  }
  EXPECT_EQ(static_cast<int>(shelved.size()), stocked);
}

// Nothing on sale is out of reach. EXP stops at the cap, so an item above it
// is one the shop takes meso for and the player can never hold -- and an
// endgame item appearing here would be a balance change nobody asked for.
// Both shelves, since a tier is added to each of them at once.
TEST(ShopTest, NothingAboveTheTrialCapIsForSale) {
  std::map<std::string, EquipPrototype> equips = LoadEquips();
  for (const std::vector<std::string>& shelf :
       {ShopWeaponStock(equips, kPaidInMeso),
        ShopEquipStock(equips, kPaidInMeso)}) {
    for (const std::string& key : shelf) {
      EXPECT_LE(equips.at(key).required_level(), kTrialLevelCap)
          << key << " is for sale";
    }
  }
}

// The token shelf is the same shelf read for a different price: an item names
// one or the other, so neither list can hold anything off the other one.
TEST(ShopTest, TheTokenShelvesHoldWhatATokenBuys) {
  std::map<std::string, EquipPrototype> equips = LoadEquips();
  std::vector<std::string> weapons = ShopWeaponStock(equips, kPaidInTokens);
  std::vector<std::string> worn = ShopEquipStock(equips, kPaidInTokens);
  EXPECT_EQ(weapons.size(), 10u) << "one Frozen weapon per type";
  EXPECT_EQ(worn.size(), 10u) << "one Frozen off-hand per branch";
  for (const std::vector<std::string>& shelf : {weapons, worn}) {
    for (const std::string& key : shelf) {
      const EquipPrototype& proto = equips.at(key);
      EXPECT_GT(proto.token_price(), 0) << key << " costs no token";
      EXPECT_FALSE(proto.has_shop_price())
          << key << " is on the meso shelf too";
    }
  }
  for (const std::string& key : ShopWeaponStock(equips, kPaidInMeso)) {
    EXPECT_EQ(equips.at(key).token_price(), 0)
        << key << " is on the meso shelf and the token shelf at once";
  }
}

// The tokens buy the tier above everything the shop sells, so the two shelves
// never offer the same level twice.
TEST(ShopTest, TheTokenTierIsAboveEveryMesoTier) {
  std::map<std::string, EquipPrototype> equips = LoadEquips();
  for (const std::string& key : ShopWeaponStock(equips, kPaidInTokens)) {
    EXPECT_EQ(equips.at(key).required_level(), 120) << key;
  }
  for (const std::string& key : ShopEquipStock(equips, kPaidInTokens)) {
    EXPECT_EQ(equips.at(key).required_level(), 120) << key;
  }
}

ItemPrototype MakeStackable(const std::string& name, ItemCategory category,
                            int price) {
  ItemPrototype p;
  p.set_name(name);
  p.set_category(category);
  p.set_shop_price(price);
  return p;
}

// Same rule the equip shelf follows: naming a price is what stocks an item,
// and an item with no price is simply not sold.
TEST(ShopEtcStockTest, OnlyPricedEtcItemsAreStocked) {
  std::map<std::string, ItemPrototype> items;
  items["trace"] = MakeStackable("Spell Trace", ITEM_CATEGORY_ETC, 5000);
  items["shell"] = MakeStackable("Snail Shell", ITEM_CATEGORY_ETC, 0);
  items["potion"] = MakeStackable("Red Potion", ITEM_CATEGORY_USE, 50);
  EXPECT_EQ(ShopEtcStock(items), std::vector<std::string>{"trace"});
}

TEST(ShopEtcStockTest, CheapestFirstThenByName) {
  std::map<std::string, ItemPrototype> items;
  items["c"] = MakeStackable("Zinc", ITEM_CATEGORY_ETC, 10);
  items["a"] = MakeStackable("Alum", ITEM_CATEGORY_ETC, 10);
  items["b"] = MakeStackable("Brass", ITEM_CATEGORY_ETC, 5);
  EXPECT_EQ(ShopEtcStock(items), (std::vector<std::string>{"b", "a", "c"}));
}

// The shipped catalog, so the trace reaching the shelf is asserted where a
// missing shop_price would actually show up.
TEST(ShopEtcStockTest, TheSpellTraceIsStocked) {
  std::string err;
  std::unique_ptr<Runfiles> runfiles(Runfiles::CreateForTest(&err));
  ASSERT_NE(runfiles, nullptr) << err;
  std::map<std::string, ItemPrototype> items =
      LoadTextProtoDir<ItemPrototype>(runfiles->Rlocation("ms/data/items"));
  std::vector<std::string> stock = ShopEtcStock(items);
  ASSERT_EQ(stock.size(), 1u) << "the Etc shelf holds more than the trace now";
  EXPECT_EQ(stock[0], "spell_trace");
  EXPECT_EQ(items.at("spell_trace").shop_price(), 5000);
  EXPECT_EQ(items.at("spell_trace").max_stack(), 30000);
}

}  // namespace
}  // namespace ms
