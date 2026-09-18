#include "src/item/currency.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "google/protobuf/map.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"

namespace ms {
namespace {

ItemPrototype Proto(const std::string& name, ItemKind kind) {
  ItemPrototype proto;
  proto.set_name(name);
  proto.set_kind(kind);
  return proto;
}

ItemPrototype Token(const std::string& name, int level, EquipSlot slot) {
  ItemPrototype proto = Proto(name, ITEM_KIND_TOKEN);
  proto.set_currency_level(level);
  proto.set_currency_slot(slot);
  return proto;
}

std::vector<std::string> Names(const CurrencyPurse& purse) {
  std::vector<std::string> names;
  for (const CurrencyAmount& entry : purse.entries()) {
    names.push_back(entry.name());
  }
  return names;
}

// The three kinds that buy or bank something are currencies; an ordinary drop
// names no kind and stays on the Etc tab.
TEST(CurrencyTest, TheKindSaysWhatIsCounted) {
  EXPECT_TRUE(IsCurrency(Proto("Spell Trace", ITEM_KIND_SPELL_TRACE)));
  EXPECT_TRUE(IsCurrency(Proto("AbsoLab Coin", ITEM_KIND_TOKEN)));
  EXPECT_TRUE(IsCurrency(Proto("Zakum's Soul Shard", ITEM_KIND_SOUL_SHARD)));
  EXPECT_FALSE(IsCurrency(Proto("Green Snail Shell", ITEM_KIND_UNSPECIFIED)));
}

// One balance per currency however many times it is banked, and nothing caps
// it -- the stack limit that used to is gone with the stack.
TEST(CurrencyTest, BankingTopsUpOneBalance) {
  CurrencyPurse purse;
  ItemPrototype trace = Proto("Spell Trace", ITEM_KIND_SPELL_TRACE);
  purse.Add(trace, 30000);
  purse.Add(trace, 30000);
  EXPECT_EQ(purse.entries().size(), 1u);
  EXPECT_EQ(purse.Count("Spell Trace"), 60000);
  EXPECT_TRUE(purse.Holds("Spell Trace"));
}

// Nothing is banked for a count that is not one, so an empty balance never
// opens a row.
TEST(CurrencyTest, NothingIsBankedForNothing) {
  CurrencyPurse purse;
  purse.Add(Proto("AbsoLab Coin", ITEM_KIND_TOKEN), 0);
  purse.Add(Proto("AbsoLab Coin", ITEM_KIND_TOKEN), -5);
  EXPECT_TRUE(purse.entries().empty());
  EXPECT_EQ(purse.Count("AbsoLab Coin"), 0);
  EXPECT_FALSE(purse.Holds("AbsoLab Coin"));
}

// All or nothing, and the row leaves when the last of it is spent: the purse
// lists what there is, never a balance of zero.
TEST(CurrencyTest, SpendingIsAllOrNothing) {
  CurrencyPurse purse;
  purse.Add(Proto("AbsoLab Coin", ITEM_KIND_TOKEN), 10);
  EXPECT_FALSE(purse.Spend("AbsoLab Coin", 11));
  EXPECT_EQ(purse.Count("AbsoLab Coin"), 10);
  EXPECT_FALSE(purse.Spend("Piece of Time", 1)) << "none of it held";
  EXPECT_FALSE(purse.Spend("AbsoLab Coin", 0));
  EXPECT_TRUE(purse.Spend("AbsoLab Coin", 4));
  EXPECT_EQ(purse.Count("AbsoLab Coin"), 6);
  EXPECT_TRUE(purse.Spend("AbsoLab Coin", 6));
  EXPECT_TRUE(purse.entries().empty());
}

// The trace leads, then the tokens by what they buy -- the best gear first,
// and within a level the weapon ahead of the set -- and the shards last by
// balance. A balance that moves re-files the purse.
TEST(CurrencyTest, ThePurseIsFiledByWhatEachOneBuys) {
  CurrencyPurse purse;
  purse.Add(Proto("Zakum's Soul Shard", ITEM_KIND_SOUL_SHARD), 3);
  purse.Add(Token("Piece of Time", 150, EQUIP_SLOT_TOP), 8);
  purse.Add(Proto("Spell Trace", ITEM_KIND_SPELL_TRACE), 900);
  purse.Add(Token("AbsoLab Coin", 160, EQUIP_SLOT_UNSPECIFIED), 2);
  purse.Add(Token("Frozen Weapon Token", 150, EQUIP_SLOT_PRIMARY_WEAPON), 1);
  purse.Add(Proto("Hilla's Soul Shard", ITEM_KIND_SOUL_SHARD), 1);
  EXPECT_EQ(Names(purse),
            (std::vector<std::string>{
                "Spell Trace", "AbsoLab Coin", "Frozen Weapon Token",
                "Piece of Time", "Zakum's Soul Shard", "Hilla's Soul Shard"}));
  purse.Add(Proto("Hilla's Soul Shard", ITEM_KIND_SOUL_SHARD), 99);
  EXPECT_EQ(Names(purse).back(), "Zakum's Soul Shard")
      << "the bigger balance leads its kind";
}

// The Token tab's two columns: each lists its own kind and neither picks up
// the trace, which is drawn as a balance in the tab bar.
TEST(CurrencyTest, EachColumnListsItsOwnKind) {
  CurrencyPurse purse;
  purse.Add(Proto("Spell Trace", ITEM_KIND_SPELL_TRACE), 900);
  purse.Add(Token("AbsoLab Coin", 160, EQUIP_SLOT_UNSPECIFIED), 2);
  purse.Add(Proto("Zakum's Soul Shard", ITEM_KIND_SOUL_SHARD), 3);
  EXPECT_EQ(CurrenciesOf(purse, ITEM_KIND_TOKEN), (std::vector<int>{1}));
  EXPECT_EQ(CurrenciesOf(purse, ITEM_KIND_SOUL_SHARD), (std::vector<int>{2}));
  EXPECT_TRUE(CurrenciesOf(CurrencyPurse(), ITEM_KIND_TOKEN).empty());
}

// A round trip through the save keeps every balance, and comes back filed.
// A name the catalog has lost is dropped, as a stack naming one already was.
TEST(CurrencyTest, ARoundTripKeepsTheBalancesTheCatalogStillNames) {
  ItemPrototype coin = Token("AbsoLab Coin", 160, EQUIP_SLOT_UNSPECIFIED);
  ItemPrototype shard = Proto("Zakum's Soul Shard", ITEM_KIND_SOUL_SHARD);
  std::map<std::string, const ItemPrototype*> catalog = {
      {coin.name(), &coin}, {shard.name(), &shard}};

  google::protobuf::Map<std::string, int64_t> saved;
  saved[coin.name()] = 7;
  saved[shard.name()] = 12;
  saved["Bygone Token"] = 4;
  saved["Emptied Purse"] = 0;

  CurrencyPurse purse;
  purse.RestoreFrom(saved, catalog);
  EXPECT_EQ(Names(purse),
            (std::vector<std::string>{coin.name(), shard.name()}));
  EXPECT_EQ(purse.Count(coin.name()), 7);

  google::protobuf::Map<std::string, int64_t> written = purse.ToProto();
  EXPECT_EQ(written.size(), 2u);
  EXPECT_EQ(written[coin.name()], 7);
  EXPECT_EQ(written[shard.name()], 12);
}

// Restoring replaces what the purse held: loading a second character does not
// hand them the first one's balances.
TEST(CurrencyTest, RestoringReplacesWhatWasHeld) {
  ItemPrototype coin = Token("AbsoLab Coin", 160, EQUIP_SLOT_UNSPECIFIED);
  std::map<std::string, const ItemPrototype*> catalog = {{coin.name(), &coin}};
  CurrencyPurse purse;
  purse.Add(coin, 5);
  purse.RestoreFrom({}, catalog);
  EXPECT_TRUE(purse.entries().empty());
}

}  // namespace
}  // namespace ms
