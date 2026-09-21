#include "src/frontend/screens/bank_panel.h"

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/frontend/testing/panel_test_base.h"
#include "src/frontend/testing/screen_text.h"
#include "src/item/equip_instance.h"
#include "src/item/item.h"
#include "src/protos/item.pb.h"

namespace ms {
namespace {

ItemPrototype Stack(const std::string& name, ItemKind kind, int max_stack) {
  ItemPrototype proto;
  proto.set_name(name);
  proto.set_category(ITEM_CATEGORY_ETC);
  proto.set_kind(kind);
  proto.set_max_stack(max_stack);
  return proto;
}

class BankPanelTest : public PanelTest {
 protected:
  void SetUp() override {
    PanelTest::SetUp();
    UnlockEverything();
    items_[kSpellTraceName] = Stack(kSpellTraceName, ITEM_KIND_SPELL_TRACE, 0);
    items_["chaos"] = Stack("Chaos Scroll", ITEM_KIND_UNSPECIFIED, 100);
    items_["shell"] = Stack("Green Snail Shell", ITEM_KIND_UNSPECIFIED, 100);
    c_.AddMeso(1234567);
    c_.AddItem(items_[kSpellTraceName], 900);
    c_.AddItem(items_["chaos"], 12);
    c_.AddItem(items_["shell"], 40);
    c_.PickUp(std::make_unique<EquipInstance>(sword_));
    panel_ = std::make_unique<BankPanel>(c_, account_, items_);
    panel_->Reset();
  }

  std::string Text() {
    ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(120),
                                                 ftxui::Dimension::Fixed(40));
    ftxui::Render(screen, ftxui::center(panel_->Render()));
    return ScreenText(screen);
  }

  // Down onto the first row of whichever half holds the cursor.
  void ToList() {
    panel_->MoveRow(1);
  }

  // Right until the cursor is on the named stop of the top row.
  void ToStop(int times) {
    for (int i = 0; i < times; ++i) {
      panel_->MoveCursor(1);
    }
  }

  const BankInstance& bank() const {
    return account_.bank();
  }

  std::mt19937 rng_{1};
  CharacterInstance c_ = MakeCharacter(210);
  AccountInstance account_;
  std::map<std::string, ItemPrototype> items_;
  std::unique_ptr<BankPanel> panel_;
};

// The cursor opens on the bag's Equip chip, Tab crosses to the bank, and each
// half keeps its own tab and row.
TEST_F(BankPanelTest, TabCrossesAndEachHalfKeepsItsPlace) {
  EXPECT_EQ(panel_->zone(), BankZone::kBag);
  EXPECT_EQ(panel_->cursor().kind, BankCursor::Kind::kTab);
  EXPECT_FALSE(panel_->on_etc_tab());

  panel_->MoveCursor(1);
  EXPECT_TRUE(panel_->on_etc_tab()) << "standing on the chip opens the tab";

  panel_->NextZone();
  EXPECT_EQ(panel_->zone(), BankZone::kBank);
  EXPECT_FALSE(panel_->on_etc_tab()) << "the bank's own tab, not the bag's";

  panel_->NextZone();
  EXPECT_EQ(panel_->zone(), BankZone::kBag);
  EXPECT_TRUE(panel_->on_etc_tab());
}

// The top row is a ring of four: the two chips, then the two balances.
TEST_F(BankPanelTest, TheTopRowWalksChipsThenBalances) {
  ToStop(2);
  BankCursor cursor = panel_->cursor();
  EXPECT_EQ(cursor.kind, BankCursor::Kind::kCurrency);
  EXPECT_EQ(cursor.currency, BankCurrency::kMeso);
  EXPECT_EQ(panel_->held(BankCurrency::kMeso), 1234567);
  EXPECT_TRUE(panel_->on_etc_tab())
      << "the tab the cursor passed through stays open";

  ToStop(1);
  EXPECT_EQ(panel_->cursor().currency, BankCurrency::kSpellTraces);
  EXPECT_EQ(panel_->held(BankCurrency::kSpellTraces), 900);

  ToStop(1);
  EXPECT_EQ(panel_->cursor().kind, BankCursor::Kind::kTab) << "round the ring";
}

// Down enters the list and Up off its first row comes back to the chip of the
// tab being shown, not to wherever the cursor left the top row.
TEST_F(BankPanelTest, UpOffTheListReturnsToTheOpenTabsChip) {
  ToStop(2);  // the meso cell
  panel_->MoveRow(1);
  EXPECT_EQ(panel_->cursor().kind, BankCursor::Kind::kRow);
  EXPECT_EQ(panel_->cursor().index, 0);

  panel_->MoveRow(-1);
  EXPECT_EQ(panel_->cursor().kind, BankCursor::Kind::kTab);
  panel_->MoveRow(-1);
  EXPECT_EQ(panel_->cursor().kind, BankCursor::Kind::kRow)
      << "up off the bar goes round to the last row";
}

TEST_F(BankPanelTest, MovingAnEquipCrossesAndBackAgain) {
  ToList();
  ASSERT_EQ(panel_->cursor().kind, BankCursor::Kind::kRow);
  EXPECT_EQ(panel_->MoveSelected(), "");
  EXPECT_EQ(c_.inventory().size(), 0);
  ASSERT_EQ(bank().equips().size(), 1);
  EXPECT_EQ(bank().equips()[0].name(), "Sword");
  EXPECT_EQ(panel_->cursor().kind, BankCursor::Kind::kTab)
      << "nothing left to stand on, so the cursor climbs to the chip";

  panel_->NextZone();
  ToList();
  EXPECT_EQ(panel_->MoveSelected(), "");
  EXPECT_EQ(bank().equips().size(), 0);
  EXPECT_EQ(c_.inventory().size(), 1);
}

// A whole stack row crosses at once, and the row that slid up is where the
// cursor lands.
TEST_F(BankPanelTest, MovingAStackTakesTheWholeRow) {
  panel_->MoveCursor(1);  // the Etc chip
  ToList();
  int rows = static_cast<int>(c_.stackables().size());
  ASSERT_EQ(rows, 2) << "the two drops; the traces are a balance, not a row";

  EXPECT_EQ(panel_->MoveSelected(), "");
  EXPECT_EQ(static_cast<int>(c_.stackables().size()), rows - 1);
  EXPECT_EQ(bank().stacks().size(), 1);
  EXPECT_EQ(panel_->cursor().kind, BankCursor::Kind::kRow)
      << "the next row slid up into this place";
  EXPECT_EQ(panel_->cursor().index, 0);
}

// A symbol is bound to the character who raised it, so it never reaches the
// account's shelf. Only the way in is guarded: one banked before the rule
// stood still comes home.
TEST_F(BankPanelTest, ASymbolWillNotGoIntoTheBank) {
  EquipPrototype symbol;
  symbol.set_name("Arcane Symbol: Vanishing Journey");
  symbol.set_equip_slot(EQUIP_SLOT_SYMBOL_VANISHING_JOURNEY);
  symbol.mutable_arcane_symbol()->set_meso_cost_base(8);
  c_.PickUp(std::make_unique<EquipInstance>(symbol));

  ToList();
  panel_->MoveRow(1);
  ASSERT_EQ(panel_->cursor().index, 1);
  EXPECT_EQ(panel_->MoveSelected(), "Symbols can't be stored.");
  EXPECT_EQ(c_.inventory().size(), 2) << "and nothing left the bag";
  EXPECT_EQ(bank().equips().size(), 0);

  account_.mutable_bank().AddEquip(std::make_unique<EquipInstance>(symbol));
  panel_->NextZone();
  ToList();
  EXPECT_EQ(panel_->MoveSelected(), "");
  EXPECT_EQ(bank().equips().size(), 0);
  EXPECT_EQ(c_.inventory().size(), 3);
}

TEST_F(BankPanelTest, AFullTabRefusesAndSaysWhich) {
  for (int i = 0; i < kTabCapacity; ++i) {
    account_.mutable_bank().AddEquip(std::make_unique<EquipInstance>(sword_));
  }
  ToList();
  EXPECT_EQ(panel_->MoveSelected(), "Bank full.");
  EXPECT_EQ(c_.inventory().size(), 1) << "and nothing left the bag";

  // And the other way: a bag with no slot left refuses what the bank offers.
  while (!c_.inventory().full()) {
    c_.PickUp(std::make_unique<EquipInstance>(sword_));
  }
  panel_->NextZone();
  ToList();
  EXPECT_EQ(panel_->MoveSelected(), "Inventory full.");
  EXPECT_EQ(bank().equips().size(), kTabCapacity);
}

TEST_F(BankPanelTest, BalancesCrossBothWays) {
  ToStop(2);
  panel_->MoveCurrency(BankCurrency::kMeso, 34567);
  EXPECT_EQ(c_.meso(), 1200000);
  EXPECT_EQ(bank().meso(), 34567);

  ToStop(1);
  panel_->MoveCurrency(BankCurrency::kSpellTraces, 400);
  EXPECT_EQ(c_.CountItem(kSpellTraceName), 500);
  EXPECT_EQ(bank().CountCurrency(kSpellTraceName), 400);

  // More than is held is clamped to what is there, and nothing is conjured.
  panel_->NextZone();
  ToStop(2);
  panel_->MoveCurrency(BankCurrency::kMeso, 99999999);
  EXPECT_EQ(bank().meso(), 0);
  EXPECT_EQ(c_.meso(), 1234567);
}

TEST_F(BankPanelTest, SortFilesTheHalfTheCursorIsIn) {
  account_.mutable_bank().AddEquip(std::make_unique<EquipInstance>(sword_));
  EquipPrototype hat;
  hat.set_name("Hat");
  hat.set_equip_slot(EQUIP_SLOT_HAT);
  hat.add_equip_job_categories(EQUIP_JOB_CATEGORY_UNIVERSAL);
  account_.mutable_bank().AddEquip(std::make_unique<EquipInstance>(hat));

  panel_->NextZone();
  panel_->SortActiveTab();
  EXPECT_EQ(bank().equips()[0].name(), "Hat")
      << "the beginner can wear it, and what can be worn leads";
}

// Both halves draw, both are labelled, and the screen fits the shortest
// terminal the game is laid out for.
TEST_F(BankPanelTest, BothHalvesDrawAndTheScreenFits) {
  std::string screen = Text();
  EXPECT_NE(screen.find("Inventory"), std::string::npos);
  EXPECT_NE(screen.find("Bank"), std::string::npos);
  EXPECT_NE(screen.find("Sword"), std::string::npos);
  EXPECT_NE(screen.find("1,234,567"), std::string::npos);

  ftxui::Element element = panel_->Render();
  element->ComputeRequirement();
  EXPECT_LE(element->requirement().min_y, 30);
  EXPECT_LE(element->requirement().min_x, 120);
}

TEST_F(BankPanelTest, TheMenusOpenOnWhatTheCursorIsOn) {
  panel_->OpenMenu();
  EXPECT_FALSE(panel_->menu_open()) << "a chip has no item menu";

  panel_->OpenTabMenu();
  EXPECT_TRUE(panel_->tab_menu_open());
  EXPECT_EQ(panel_->menu_selected(), kBankTabMenuSort);
  panel_->CloseMenu();

  ToList();
  panel_->OpenMenu();
  ASSERT_TRUE(panel_->menu_open());
  EXPECT_FALSE(panel_->tab_menu_open()) << "never both at once";
  EXPECT_EQ(panel_->menu_selected(), kBankMenuInspect);
  panel_->MoveMenuCursor(1);
  EXPECT_EQ(panel_->menu_selected(), kBankMenuMove);
  EXPECT_NE(Text().find("Move"), std::string::npos);
}

TEST_F(BankPanelTest, InspectReachesTheItemInEitherHalf) {
  ToList();
  ASSERT_NE(panel_->selected_equip(), nullptr);
  EXPECT_EQ(panel_->selected_equip()->name(), "Sword");
  EXPECT_EQ(panel_->selected_stack(), nullptr);

  panel_->MoveRow(-1);
  panel_->MoveCursor(1);  // the Etc chip
  ToList();
  EXPECT_EQ(panel_->selected_equip(), nullptr);
  ASSERT_NE(panel_->selected_stack(), nullptr);

  account_.mutable_bank().AddEquip(std::make_unique<EquipInstance>(sword_));
  panel_->NextZone();
  ToList();
  ASSERT_NE(panel_->selected_equip(), nullptr);
  EXPECT_EQ(panel_->selected_equip()->name(), "Sword");
}

// Two windows side by side, each fitted to its own rows: a full purse is the
// widest thing either of them says.
TEST_F(BankPanelTest, NeitherHalfWeldsARowToItsRightBorder) {
  EXPECT_TRUE(RowsTouchingTheRightBorder(panel_->Render()).empty());
}
}  // namespace
}  // namespace ms
