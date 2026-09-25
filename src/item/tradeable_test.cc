#include "src/item/tradeable.h"

#include <gtest/gtest.h>

#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"

namespace ms {
namespace {

// A currency is a balance the trade and bank screens move on its own line, so
// it never transfers as a row; everything else on the Etc tab does.
TEST(TradeableTest, ACurrencyStaysWhereItIs) {
  ItemPrototype drop;
  drop.set_name("Green Snail Shell");
  EXPECT_TRUE(CanTrade(drop));

  for (ItemKind kind :
       {ITEM_KIND_SPELL_TRACE, ITEM_KIND_TOKEN, ITEM_KIND_SOUL_SHARD}) {
    ItemPrototype currency;
    currency.set_kind(kind);
    EXPECT_FALSE(CanTrade(currency)) << ItemKind_Name(kind);
  }
}

// A symbol is bound to the character who levelled it; every other equip can be
// transferred.
TEST(TradeableTest, ASymbolIsBoundToItsOwner) {
  EquipPrototype hat;
  hat.set_name("Zakum Helmet");
  EXPECT_TRUE(CanTrade(hat));

  EquipPrototype symbol;
  symbol.set_name("Arcane Symbol: Vanishing Journey");
  symbol.mutable_arcane_symbol()->set_meso_cost_base(8);
  EXPECT_FALSE(CanTrade(symbol));
}

}  // namespace
}  // namespace ms
