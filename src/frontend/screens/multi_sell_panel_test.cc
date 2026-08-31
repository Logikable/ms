#include "src/frontend/screens/multi_sell_panel.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/frontend/widgets/panel_test_base.h"
#include "src/frontend/widgets/screen_text.h"
#include "src/item/equip_instance.h"

namespace ms {
namespace {

class MultiSellTest : public PanelTest {
 protected:
  void SetUp() override {
    PanelTest::SetUp();
    sword_.set_sell_price(1000);
  }

  // An equip worth `price`, in the bag.
  void GiveEquip(const std::string& name, int price) {
    EquipPrototype proto = sword_;
    proto.set_name(name);
    proto.set_sell_price(price);
    c_.PickUp(std::make_unique<EquipInstance>(proto));
  }

  void GiveStack(const std::string& name, ItemCategory category, int price,
                 int count) {
    ItemPrototype proto;
    proto.set_name(name);
    proto.set_category(category);
    proto.set_sell_price(price);
    c_.AddStackable(proto, count);
  }

  // The screen as plain characters, one row per line.
  std::vector<std::string> ScreenRows(MultiSellPanel& panel) {
    ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(110),
                                                 ftxui::Dimension::Fixed(40));
    ftxui::Render(screen, ftxui::center(panel.Render()));
    return ms::ScreenRows(screen);
  }

  bool ScreenHas(MultiSellPanel& panel, const std::string& needle) {
    for (const std::string& row : ScreenRows(panel)) {
      if (row.find(needle) != std::string::npos) {
        return true;
      }
    }
    return false;
  }

  // The one screen row naming `item`, so a column can be read off the row it
  // belongs to rather than off the screen at large.
  std::string RowFor(MultiSellPanel& panel, const std::string& item) {
    for (const std::string& row : ScreenRows(panel)) {
      if (row.find(item) != std::string::npos) {
        return row;
      }
    }
    return "";
  }

  // The screen row the window's top border lands on.
  int TopRow(MultiSellPanel& panel) {
    std::vector<std::string> rows = ScreenRows(panel);
    for (int y = 0; y < static_cast<int>(rows.size()); ++y) {
      if (rows[y].find("Multi-Sell") != std::string::npos) {
        return y;
      }
    }
    return -1;
  }

  ConfirmChoice Press(MultiSellPanel& panel, ftxui::Event event,
                      int times = 1) {
    ConfirmChoice choice = ConfirmChoice::kPending;
    for (int i = 0; i < times; ++i) {
      choice = panel.OnEvent(event);
    }
    return choice;
  }
};

TEST_F(MultiSellTest, OpensWithTheChosenRowMarked) {
  GiveEquip("Sword", 1000);
  GiveEquip("Axe", 2000);
  MultiSellPanel panel(c_);
  panel.Reset(kEquipTab, 1);
  EXPECT_EQ(panel.basket().equips, std::set<int>({1}));
  EXPECT_EQ(panel.Total(), 2000);
  // The mark rides the row it was made on, and no other.
  EXPECT_NE(RowFor(panel, "Axe").find("✓"), std::string::npos);
  EXPECT_EQ(RowFor(panel, "Sword").find("✓"), std::string::npos);
}

TEST_F(MultiSellTest, EnterTogglesTheMarkAndTheTotalFollows) {
  GiveEquip("Sword", 1000);
  GiveEquip("Axe", 2000);
  MultiSellPanel panel(c_);
  panel.Reset(kEquipTab, 0);
  Press(panel, ftxui::Event::ArrowDown);
  Press(panel, ftxui::Event::Return);
  EXPECT_EQ(panel.Total(), 3000);
  Press(panel, ftxui::Event::Return);
  EXPECT_EQ(panel.Total(), 1000);
}

TEST_F(MultiSellTest, TheBasketRunsAcrossTabs) {
  GiveEquip("Sword", 1000);
  GiveStack("Red Potion", ITEM_CATEGORY_USE, 50, 4);
  GiveStack("Wild Boar Tooth", ITEM_CATEGORY_ETC, 7, 3);
  MultiSellPanel panel(c_);
  panel.Reset(kEquipTab, 0);
  // Up to the tab bar, right to Use, down onto the stack, mark it.
  Press(panel, ftxui::Event::ArrowUp);
  Press(panel, ftxui::Event::ArrowRight);
  Press(panel, ftxui::Event::ArrowDown);
  Press(panel, ftxui::Event::Return);
  EXPECT_EQ(panel.Total(), 1000 + 4 * 50);  // the whole stack goes
  Press(panel, ftxui::Event::ArrowUp);
  Press(panel, ftxui::Event::ArrowRight);
  Press(panel, ftxui::Event::ArrowDown);
  Press(panel, ftxui::Event::Return);
  EXPECT_EQ(panel.Total(), 1000 + 200 + 21);
}

TEST_F(MultiSellTest, TheWindowStandsAtTheSameHeightOnEveryTab) {
  GiveEquip("Sword", 1000);
  GiveStack("Red Potion", ITEM_CATEGORY_USE, 50, 4);
  MultiSellPanel panel(c_);
  panel.Reset(kEquipTab, 0);
  int top = TopRow(panel);
  EXPECT_GE(top, 0);
  // A tab with one row and a tab with none leave the box the size it was.
  Press(panel, ftxui::Event::ArrowUp);
  Press(panel, ftxui::Event::ArrowRight);
  EXPECT_EQ(TopRow(panel), top);
  Press(panel, ftxui::Event::ArrowRight);
  EXPECT_EQ(TopRow(panel), top);
}

TEST_F(MultiSellTest, ACurrencyStackCannotBeMarked) {
  GiveStack("Spell Trace", ITEM_CATEGORY_ETC, 0, 60);
  MultiSellPanel panel(c_);
  panel.Reset(kEtcTab, 0);
  EXPECT_TRUE(panel.basket().empty());
  Press(panel, ftxui::Event::Return);
  EXPECT_TRUE(panel.basket().empty());
  EXPECT_TRUE(ScreenHas(panel, "Spell Trace"));
}

TEST_F(MultiSellTest, ATraceIsMarkableAndPaysNothing) {
  GiveEquip("Sword", 1000);
  Equip state;
  state.set_equip_name(sword_.name());
  state.set_trace(true);
  c_.PickUp(std::make_unique<EquipTrace>(sword_, state));
  MultiSellPanel panel(c_);
  panel.Reset(kEquipTab, 1);
  EXPECT_EQ(panel.basket().equips, std::set<int>({1}));
  EXPECT_EQ(panel.Total(), 0);
}

TEST_F(MultiSellTest, TheCursorRingRunsBarToRowsToButtons) {
  GiveEquip("Sword", 1000);
  GiveStack("Red Potion", ITEM_CATEGORY_USE, 50, 4);
  MultiSellPanel panel(c_);
  panel.Reset(kEquipTab, 0);
  // Right does nothing while a row holds the cursor: the tabs are the bar's.
  Press(panel, ftxui::Event::ArrowRight);
  EXPECT_TRUE(ScreenHas(panel, "Sword"));
  // Down off the only row lands on the buttons, and Enter there opens the
  // dialog.
  Press(panel, ftxui::Event::ArrowDown);
  Press(panel, ftxui::Event::Return);
  EXPECT_TRUE(panel.confirming());
  Press(panel, ftxui::Event::Escape);
  // Down again comes out on the bar, where Right does switch tabs.
  Press(panel, ftxui::Event::ArrowDown);
  Press(panel, ftxui::Event::ArrowRight);
  EXPECT_TRUE(ScreenHas(panel, "Red Potion"));
}

TEST_F(MultiSellTest, ConfirmDoesNothingWithAnEmptyBasket) {
  GiveEquip("Sword", 1000);
  MultiSellPanel panel(c_);
  panel.Reset(kEquipTab, 0);
  Press(panel, ftxui::Event::Return);  // unmark the only row
  Press(panel, ftxui::Event::ArrowDown);
  EXPECT_EQ(Press(panel, ftxui::Event::Return), ConfirmChoice::kPending);
  EXPECT_FALSE(panel.confirming());
}

TEST_F(MultiSellTest, TheDialogOpensOnConfirm) {
  GiveEquip("Sword", 1000);
  MultiSellPanel panel(c_);
  panel.Reset(kEquipTab, 0);
  Press(panel, ftxui::Event::ArrowDown);
  Press(panel, ftxui::Event::Return);
  // Escape backs out of the dialog without selling, and leaves the screen up.
  EXPECT_EQ(Press(panel, ftxui::Event::Escape), ConfirmChoice::kPending);
  EXPECT_FALSE(panel.confirming());
  // Enter on the button row opens it again, and Enter answers it: the cursor
  // is already on Confirm.
  Press(panel, ftxui::Event::Return);
  EXPECT_EQ(Press(panel, ftxui::Event::Return), ConfirmChoice::kConfirmed);
}

TEST_F(MultiSellTest, EscapeLeavesTheScreen) {
  GiveEquip("Sword", 1000);
  MultiSellPanel panel(c_);
  panel.Reset(kEquipTab, 0);
  EXPECT_EQ(Press(panel, ftxui::Event::Escape), ConfirmChoice::kCancelled);
}

TEST_F(MultiSellTest, TheHeaderCarriesTheMesoAndTheRunningTotal) {
  c_.AddMeso(12345);
  GiveEquip("Sword", 1000);
  MultiSellPanel panel(c_);
  panel.Reset(kEquipTab, 0);
  EXPECT_TRUE(ScreenHas(panel, "12,345"));
  EXPECT_TRUE(ScreenHas(panel, "+1,000"));
}

TEST_F(MultiSellTest, EveryRowShowsWhatItWouldPay) {
  GiveStack("Red Potion", ITEM_CATEGORY_USE, 50, 4);
  GiveStack("Spell Trace", ITEM_CATEGORY_ETC, 0, 60);
  MultiSellPanel panel(c_);
  panel.Reset(kUseTab, 0);
  // The whole stack, on the row it belongs to.
  EXPECT_NE(RowFor(panel, "Red Potion").find("200"), std::string::npos);
  panel.Reset(kEtcTab, 0);
  EXPECT_NE(RowFor(panel, "Spell Trace").find("-"), std::string::npos);
}

TEST_F(MultiSellTest, SellingPaysTheTotalAndEmptiesTheRows) {
  GiveEquip("Sword", 1000);
  GiveStack("Red Potion", ITEM_CATEGORY_USE, 50, 4);
  SaleBasket basket;
  basket.equips.insert(0);
  basket.use.insert(0);
  EXPECT_EQ(BasketTotal(c_, basket), 1200);
  EXPECT_EQ(SellBasket(c_, basket), 1200);
  EXPECT_EQ(c_.inventory().size(), 0);
  EXPECT_TRUE(c_.stackables(ITEM_CATEGORY_USE).empty());
  EXPECT_EQ(c_.meso(), 1200);
}

TEST_F(MultiSellTest, TheShelfReadsEquipThenUseThenEtcInBagOrder) {
  GiveEquip("Sword", 1000);
  GiveEquip("Axe", 2000);
  GiveStack("Red Potion", ITEM_CATEGORY_USE, 50, 4);
  GiveStack("Wild Boar Tooth", ITEM_CATEGORY_ETC, 7, 3);
  SaleBasket basket;
  basket.equips = {0, 1};
  basket.use = {0};
  basket.etc = {0};
  SellBasket(c_, basket);
  ASSERT_EQ(c_.buy_backs().size(), 4);
  EXPECT_EQ(c_.buy_backs()[0].equip().equip_name(), "Sword");
  EXPECT_EQ(c_.buy_backs()[1].equip().equip_name(), "Axe");
  EXPECT_EQ(c_.buy_backs()[2].stack().name(), "Red Potion");
  EXPECT_EQ(c_.buy_backs()[3].stack().name(), "Wild Boar Tooth");
}

}  // namespace
}  // namespace ms
