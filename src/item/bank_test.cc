#include "src/item/bank.h"

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <string>

#include "src/item/equip_instance.h"
#include "src/protos/account.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"

namespace ms {
namespace {

EquipPrototype Sword() {
  EquipPrototype proto;
  proto.set_name("Sword");
  proto.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  proto.set_required_level(10);
  return proto;
}

ItemPrototype Shell() {
  ItemPrototype proto;
  proto.set_name("Green Snail Shell");
  proto.set_max_stack(100);
  return proto;
}

ItemPrototype Trace() {
  ItemPrototype proto;
  proto.set_name(kSpellTraceName);
  proto.set_kind(ITEM_KIND_SPELL_TRACE);
  return proto;
}

std::unique_ptr<EquipTabItem> Item(const EquipPrototype& proto, int stars) {
  Equip state;
  state.set_equip_name(proto.name());
  state.set_stars(stars);
  return std::make_unique<EquipInstance>(proto, state);
}

TEST(BankTest, EquipsGoInAndComeBackOut) {
  BankInstance bank;
  EquipPrototype sword = Sword();
  EXPECT_TRUE(bank.AddEquip(2, Item(sword, 3)));
  EXPECT_FALSE(bank.AddEquip(2, nullptr));
  EXPECT_EQ(bank.page(2).size(), 1);
  EXPECT_EQ(bank.page(0).size(), 0) << "only the page named";

  EXPECT_EQ(bank.TakeEquip(2, -1), nullptr);
  EXPECT_EQ(bank.TakeEquip(2, 1), nullptr);
  EXPECT_EQ(bank.TakeEquip(0, 0), nullptr);
  std::unique_ptr<EquipTabItem> back = bank.TakeEquip(2, 0);
  ASSERT_NE(back, nullptr);
  EXPECT_EQ(back->name(), "Sword");
  EXPECT_EQ(back->stars(), 3) << "the item that went in, not a fresh copy";
  EXPECT_EQ(bank.page(2).size(), 0);
}

TEST(BankTest, EachPageFillsUpOnItsOwn) {
  BankInstance bank;
  EquipPrototype sword = Sword();
  for (int i = 0; i < kBankPageCapacity; ++i) {
    ASSERT_TRUE(bank.AddEquip(0, Item(sword, 0)));
  }
  EXPECT_TRUE(bank.PageFull(0));
  EXPECT_FALSE(bank.AddEquip(0, Item(sword, 0)));
  EXPECT_FALSE(bank.PageFull(1));
  EXPECT_TRUE(bank.AddEquip(1, Item(sword, 0)));
}

// A currency is a balance and accepts any amount; an Etc drop takes a slot and
// can run out of room.
TEST(BankTest, DropsAndCurrenciesAreRoutedApart) {
  BankInstance bank;
  ItemPrototype shell = Shell();
  ItemPrototype trace = Trace();

  EXPECT_EQ(bank.AddItem(shell, 150), 150);
  EXPECT_EQ(bank.stacks().size(), 2);
  EXPECT_EQ(bank.AddItem(shell, 0), 0);

  EXPECT_EQ(bank.AddItem(trace, 90000), 90000);
  EXPECT_EQ(bank.stacks().size(), 2) << "a currency costs no slot";
  EXPECT_EQ(bank.CountCurrency(kSpellTraceName), 90000);
  EXPECT_EQ(bank.RoomFor(trace), INT_MAX);

  EXPECT_TRUE(bank.SpendCurrency(kSpellTraceName, 40000));
  EXPECT_FALSE(bank.SpendCurrency(kSpellTraceName, 90000));
  EXPECT_EQ(bank.CountCurrency(kSpellTraceName), 50000);

  EXPECT_EQ(bank.TakeStack(0, 200), 100);
  EXPECT_EQ(bank.stacks().size(), 1);
}

TEST(BankTest, MesoAndVPointsAreAllOrNothing) {
  BankInstance bank;
  bank.AddMeso(-5);
  EXPECT_EQ(bank.meso(), 0);
  bank.AddMeso(1000);
  EXPECT_FALSE(bank.SpendMeso(1001));
  EXPECT_EQ(bank.meso(), 1000);
  EXPECT_TRUE(bank.SpendMeso(1000));
  EXPECT_EQ(bank.meso(), 0);

  bank.AddVPoints(-5);
  EXPECT_EQ(bank.v_points(), 0);
  bank.AddVPoints(300);
  EXPECT_FALSE(bank.SpendVPoints(301));
  EXPECT_TRUE(bank.SpendVPoints(300));
  EXPECT_EQ(bank.v_points(), 0);
}

// Everything survives saving, and a row whose item left the data is dropped
// instead of restored blank.
TEST(BankTest, SaveAndLoadKeepWhatIsStillInTheCatalog) {
  EquipPrototype sword = Sword();
  ItemPrototype shell = Shell();
  ItemPrototype trace = Trace();

  BankInstance bank;
  bank.AddEquip(3, Item(sword, 5));
  bank.AddItem(shell, 30);
  bank.AddItem(trace, 700);
  bank.AddMeso(123456);
  bank.AddVPoints(4505);

  Bank saved = bank.ToProto();
  ASSERT_EQ(saved.equip_pages_size(), kBankPages);
  ASSERT_EQ(saved.equip_pages(3).equips_size(), 1);
  ASSERT_EQ(saved.stacks_size(), 1);
  EXPECT_EQ(saved.meso(), 123456);

  // Two rows naming items the catalogs no longer have.
  saved.mutable_equip_pages(3)->add_equips()->set_equip_name(
      "Sword That Left The Data");
  saved.add_stacks()->set_name("Drop That Left The Data");

  std::map<std::string, const EquipPrototype*> equips = {
      {sword.name(), &sword}};
  std::map<std::string, const ItemPrototype*> items = {{shell.name(), &shell},
                                                       {trace.name(), &trace}};
  BankInstance loaded;
  loaded.RestoreFrom(saved, equips, items);

  ASSERT_EQ(loaded.page(3).size(), 1);
  EXPECT_EQ(loaded.page(3)[0].name(), "Sword");
  EXPECT_EQ(loaded.page(3)[0].stars(), 5);
  ASSERT_EQ(loaded.stacks().size(), 1);
  EXPECT_EQ(loaded.stacks().Count("Green Snail Shell"), 30);
  EXPECT_EQ(loaded.CountCurrency(kSpellTraceName), 700);
  EXPECT_EQ(loaded.meso(), 123456);
  EXPECT_EQ(loaded.v_points(), 4505);
}

// A save from before the pages fills them in order, sixteen to a page, and a
// row whose item left the data still holds its place.
TEST(BankTest, AnOldSaveFillsThePagesInOrder) {
  EquipPrototype sword = Sword();
  Bank saved;
  for (int i = 0; i < kBankPageCapacity + 2; ++i) {
    Equip* state = saved.add_legacy_equip_tab();
    state->set_equip_name(i == 0 ? "Sword That Left The Data" : sword.name());
    state->set_stars(i);
  }
  BankInstance loaded;
  loaded.RestoreFrom(saved, {{sword.name(), &sword}}, {});
  EXPECT_EQ(loaded.page(0).size(), kBankPageCapacity - 1);
  ASSERT_EQ(loaded.page(1).size(), 2);
  EXPECT_EQ(loaded.page(1)[0].stars(), kBankPageCapacity);
  EXPECT_EQ(loaded.ToProto().legacy_equip_tab_size(), 0) << "written as pages";
}

TEST(BankTest, SortingFilesOnePageAndTheEtcTab) {
  EquipPrototype sword = Sword();
  BankInstance bank;
  bank.AddEquip(0, Item(sword, 0));
  bank.AddEquip(0, Item(sword, 7));
  bank.AddEquip(1, Item(sword, 0));
  bank.AddEquip(1, Item(sword, 7));
  bank.AddItem(Shell(), 10);
  bank.AddItem(Trace(), 10);

  bank.SortEquips(0, [](const EquipPrototype&) { return true; });
  EXPECT_EQ(bank.page(0)[0].stars(), 7) << "the most stars leads";
  EXPECT_EQ(bank.page(1)[0].stars(), 0) << "another page is left alone";
  bank.SortStacks();
  EXPECT_EQ(bank.stacks().size(), 1) << "the trace is a balance, not a stack";
}

}  // namespace
}  // namespace ms
