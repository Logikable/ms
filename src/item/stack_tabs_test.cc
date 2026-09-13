#include "src/item/stack_tabs.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "src/item/item.h"
#include "src/protos/item.pb.h"

namespace ms {
namespace {

StackableItem Stack(const std::string& name, ItemKind kind, int count) {
  ItemPrototype proto;
  proto.set_name(name);
  proto.set_kind(kind);
  return StackableItem(proto, count);
}

std::vector<StackableItem> Bag() {
  return {
      Stack("Green Snail Shell", ITEM_KIND_UNSPECIFIED, 40),
      Stack("Frozen Weapon Token", ITEM_KIND_TOKEN, 3),
      Stack("Spell Trace", ITEM_KIND_SPELL_TRACE, 9000),
      Stack("Zakum's Soul Shard", ITEM_KIND_SOUL_SHARD, 12),
      Stack("AbsoLab Coin", ITEM_KIND_TOKEN, 1),
  };
}

// Each view lists its own kind and nothing else, and the three together leave
// out the spell trace, which is drawn as a balance rather than as a row.
TEST(StackTabsTest, EachViewListsItsOwnKind) {
  std::vector<StackableItem> bag = Bag();
  EXPECT_EQ(StacksIn(bag, StackView::kTokens), (std::vector<int>{1, 4}));
  EXPECT_EQ(StacksIn(bag, StackView::kSoulShards), (std::vector<int>{3}));
  EXPECT_EQ(StacksIn(bag, StackView::kEtc), (std::vector<int>{0}));
}

// The indices come back in bag order, so a sorted bag makes a sorted column.
TEST(StackTabsTest, AViewKeepsTheBagsOrder) {
  std::vector<StackableItem> bag = {
      Stack("AbsoLab Coin", ITEM_KIND_TOKEN, 9),
      Stack("Green Snail Shell", ITEM_KIND_UNSPECIFIED, 40),
      Stack("Frozen Weapon Token", ITEM_KIND_TOKEN, 3),
  };
  EXPECT_EQ(StacksIn(bag, StackView::kTokens), (std::vector<int>{0, 2}));
}

// The Token tab opens on the first token or shard, and a bag of drops and
// traces does not open it.
TEST(StackTabsTest, CurrencyIsATokenOrAShard) {
  EXPECT_TRUE(HoldsCurrency(Bag()));
  EXPECT_FALSE(HoldsCurrency({}));
  EXPECT_FALSE(HoldsCurrency({
      Stack("Green Snail Shell", ITEM_KIND_UNSPECIFIED, 40),
      Stack("Spell Trace", ITEM_KIND_SPELL_TRACE, 9000),
  }));
  EXPECT_TRUE(HoldsCurrency({Stack("Zakum's", ITEM_KIND_SOUL_SHARD, 1)}));
}

}  // namespace
}  // namespace ms
