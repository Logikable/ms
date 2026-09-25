#include "src/item/shop.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include "src/character/exp_table.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"
#include "src/testing/data_files.h"

namespace ms {
namespace {

std::map<std::string, EquipPrototype> LoadEquips() {
  return LoadTestData<EquipPrototype>("equip");
}

EquipPrototype MakeItem(const std::string& name, int level, int price) {
  EquipPrototype e;
  e.set_name(name);
  e.set_required_level(level);
  e.set_shop_price(price);
  e.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  return e;
}

// An item the shop doesn't stock, meaning one with no price at all, not a price
// of zero.
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

// Setting a price is what stocks an item, and zero is a price: the shop gives
// one item away free, and an item with no price isn't sold.
TEST(ShopTest, StocksOnlyPricedItemsAndZeroIsAPrice) {
  std::map<std::string, EquipPrototype> equips{
      {"free", MakeItem("Free", 10, 0)},
      {"sold", MakeItem("Sold", 10, 5000)},
      {"unsold", MakeUnpriced("Unsold", 10)},
  };
  std::vector<std::string> expected{"free", "sold"};
  EXPECT_EQ(ShopWeaponStock(equips, kPaidInMeso), expected);
}

// The four sort keys, checked one at a time. Each case keeps earlier keys equal
// so only the key under test decides the order, and every case lists the
// catalog keys in the opposite order from the answer, so sorting by key alone
// wouldn't pass.
TEST(ShopTest, SortsByLevelBeforeAnythingElse) {
  // The pricier item comes first because of its lower level, despite its type,
  // price and name.
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

// The shipped shelves, checked pair by pair against the header's four keys.
// Checks sortedness instead of a copied list: a copy would need rewriting for
// every new tier and adds nothing the rule doesn't.
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

// What shares the weapon shelf. Stars belong there, since a claw uses them and
// a one-item tab isn't worth having. No worn items do: they have their own
// shelf, and a medallion among swords would look like a weapon. The shelf is
// sorted by level, so the stars appear in their own tier instead of at the end.
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

// The two shelves split everything the shop stocks: every worn item that isn't
// a weapon or thrown is on the other shelf, so nothing falls between them or
// appears on both.
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

// Nothing for sale is out of reach. EXP stops at the cap, so an item above it
// would take meso for something the player can never use, and an endgame item
// here would be an unrequested balance change. Checks both shelves, since each
// new tier goes on both.
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

// The token shelf is the same shelf read for a different price: an item has one
// price or the other, so neither list can contain anything from the other.
TEST(ShopTest, TheTokenShelvesHoldWhatATokenBuys) {
  std::map<std::string, EquipPrototype> equips = LoadEquips();
  std::vector<std::string> weapons = ShopWeaponStock(equips, kPaidInTokens);
  std::vector<std::string> worn = ShopEquipStock(equips, kPaidInTokens);
  EXPECT_EQ(weapons.size(), 30u) << "a weapon per type at each of the three "
                                    "token tiers";
  EXPECT_EQ(worn.size(), 64u)
      << "ten Frozen and ten Princess No off-hands, four Cygnus shoulders, "
         "and three pieces of Root Abyss and seven of AbsoLab armour per "
         "branch";
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

// A token buys the tier above everything meso can buy for the same slot, so the
// two shelves never offer the same slot and a token is never the worse buy.
// Checked per slot instead of across the whole shop, since the meso shelf sells
// a level 140 ring next to secondaries that stop at 100.
TEST(ShopTest, ATokenTierIsAboveEveryMesoTierOfItsSlot) {
  std::map<std::string, EquipPrototype> equips = LoadEquips();
  std::map<EquipSlot, int> highest;
  for (const std::vector<std::string>& shelf :
       {ShopWeaponStock(equips, kPaidInMeso),
        ShopEquipStock(equips, kPaidInMeso)}) {
    for (const std::string& key : shelf) {
      const EquipPrototype& proto = equips.at(key);
      highest[proto.equip_slot()] =
          std::max(highest[proto.equip_slot()], proto.required_level());
    }
  }
  int checked = 0;
  for (const std::vector<std::string>& shelf :
       {ShopWeaponStock(equips, kPaidInTokens),
        ShopEquipStock(equips, kPaidInTokens)}) {
    for (const std::string& key : shelf) {
      ++checked;
      const EquipPrototype& proto = equips.at(key);
      EXPECT_GT(proto.required_level(), highest[proto.equip_slot()]) << key;
    }
  }
  EXPECT_GT(checked, 0) << "nothing is bought with a token";
}

ItemPrototype MakeStackable(const std::string& name, int price) {
  ItemPrototype p;
  p.set_name(name);
  p.set_shop_price(price);
  return p;
}

// The same rule as the equip shelf: a price stocks an item, and an item with no
// price isn't sold.
TEST(ShopEtcStockTest, OnlyPricedItemsAreStocked) {
  std::map<std::string, ItemPrototype> items;
  items["trace"] = MakeStackable("Spell Trace", 5000);
  items["shell"] = MakeStackable("Snail Shell", 0);
  EXPECT_EQ(ShopEtcStock(items), std::vector<std::string>{"trace"});
}

TEST(ShopEtcStockTest, CheapestFirstThenByName) {
  std::map<std::string, ItemPrototype> items;
  items["c"] = MakeStackable("Zinc", 10);
  items["a"] = MakeStackable("Alum", 10);
  items["b"] = MakeStackable("Brass", 5);
  EXPECT_EQ(ShopEtcStock(items), (std::vector<std::string>{"b", "a", "c"}));
}

// Uses the shipped catalog, so a missing shop_price on the trace would be
// caught here.
TEST(ShopEtcStockTest, TheSpellTraceIsStocked) {
  std::map<std::string, ItemPrototype> items =
      LoadTestData<ItemPrototype>("items");
  std::vector<std::string> stock = ShopEtcStock(items);
  ASSERT_EQ(stock.size(), 1u) << "the Etc shelf holds more than the trace now";
  EXPECT_EQ(stock[0], "spell_trace");
  EXPECT_EQ(items.at("spell_trace").shop_price(), 5000);
  EXPECT_EQ(items.at("spell_trace").kind(), ITEM_KIND_SPELL_TRACE)
      << "what buys it lands in the purse, not on a tab";
}

}  // namespace
}  // namespace ms
