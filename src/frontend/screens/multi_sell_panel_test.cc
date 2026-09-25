#include "src/frontend/screens/multi_sell_panel.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/frontend/testing/panel_test_base.h"
#include "src/frontend/testing/screen_text.h"
#include "src/frontend/widgets/colors.h"
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

  void GiveStack(const std::string& name, int price, int count) {
    ItemPrototype proto;
    proto.set_name(name);
    proto.set_sell_price(price);
    c_.AddItem(proto, count);
  }

  // A currency, kept in the purse rather than on the Etc tab.
  void GiveCurrency(const std::string& name, ItemKind kind, int count) {
    ItemPrototype proto;
    proto.set_name(name);
    proto.set_kind(kind);
    c_.AddItem(proto, count);
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

  // The screen row naming `item`, so a column can be read from its own row
  // rather than from anywhere on screen.
  std::string RowFor(MultiSellPanel& panel, const std::string& item) {
    for (const std::string& row : ScreenRows(panel)) {
      if (row.find(item) != std::string::npos) {
        return row;
      }
    }
    return "";
  }

  // The screen row of the window's top border.
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
  MultiSellPanel panel(c_, account_);
  panel.Reset(kEquipTab, 1);
  EXPECT_EQ(panel.basket().equips, std::set<int>({1}));
  EXPECT_EQ(panel.Total(), 2000);
  // The mark stays on the row it was made on, and no other.
  EXPECT_NE(RowFor(panel, "Axe").find("✓"), std::string::npos);
  EXPECT_EQ(RowFor(panel, "Sword").find("✓"), std::string::npos);
}

// The band under the cursor has to cover the mark and the price as well as the
// item's cells. A band that stopped at the bag's columns would leave out the
// two this screen adds.
TEST_F(MultiSellTest, TheBandUnderTheCursorCoversTheMarkAndThePrice) {
  GiveEquip("Sword", 1000);
  MultiSellPanel panel(c_, account_);
  panel.Reset(kEquipTab, 0);
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(110),
                                               ftxui::Dimension::Fixed(40));
  ftxui::Render(screen, ftxui::center(panel.Render()));

  std::vector<BandSpan> bands = BandSpans(screen, kSelectedRow);
  ASSERT_EQ(bands.size(), 1u) << "one row is selected, so one row is banded";
  int y = bands[0].y;
  ASSERT_NE(ScreenRow(screen, y).find("Sword"), std::string::npos);
  EXPECT_LT(bands[0].first, FindOnScreen(screen, "✓").x)
      << "the mark is outside the band";
  EXPECT_GE(bands[0].last, FindOnScreen(screen, "1,000").x + 4)
      << "the band stops before the last digit of the price";
}

TEST_F(MultiSellTest, EnterTogglesTheMarkAndTheTotalFollows) {
  GiveEquip("Sword", 1000);
  GiveEquip("Axe", 2000);
  MultiSellPanel panel(c_, account_);
  panel.Reset(kEquipTab, 0);
  Press(panel, ftxui::Event::ArrowDown);
  Press(panel, ftxui::Event::Return);
  EXPECT_EQ(panel.Total(), 3000);
  Press(panel, ftxui::Event::Return);
  EXPECT_EQ(panel.Total(), 1000);
}

TEST_F(MultiSellTest, TheBasketRunsAcrossTabs) {
  GiveEquip("Sword", 1000);
  GiveStack("Wild Boar Tooth", 7, 3);
  MultiSellPanel panel(c_, account_);
  panel.Reset(kEquipTab, 0);
  // Up to the tab bar, right to Etc, down onto the stack, and mark it.
  Press(panel, ftxui::Event::ArrowUp);
  Press(panel, ftxui::Event::ArrowRight);
  Press(panel, ftxui::Event::ArrowDown);
  Press(panel, ftxui::Event::Return);
  EXPECT_EQ(panel.Total(), 1000 + 3 * 7) << "the whole stack goes";

  // The bar highlights the tab the rows belong to, not the one at Etc's tab id.
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(110),
                                               ftxui::Dimension::Fixed(40));
  ftxui::Render(screen, ftxui::center(panel.Render()));
  EXPECT_TRUE(PixelOf(screen, " Etc ").inverted);
  EXPECT_FALSE(PixelOf(screen, " Equip ").inverted);
}

TEST_F(MultiSellTest, TheWindowStandsAtTheSameHeightOnEveryTab) {
  GiveEquip("Sword", 1000);
  GiveStack("Wild Boar Tooth", 7, 3);
  MultiSellPanel panel(c_, account_);
  panel.Reset(kEquipTab, 0);
  int top = TopRow(panel);
  EXPECT_GE(top, 0);
  // A tab with one row and a tab with none leave the box the same size.
  Press(panel, ftxui::Event::ArrowUp);
  Press(panel, ftxui::Event::ArrowRight);
  EXPECT_EQ(TopRow(panel), top);
  Press(panel, ftxui::Event::ArrowRight);
  EXPECT_EQ(TopRow(panel), top);
}

// The shop buys what the bag's Etc tab lists, and currencies aren't on it: they
// are a balance on the Token tab, and a balance can't be sold. They take no row
// either, so the one drop is the whole list.
TEST_F(MultiSellTest, TheCurrenciesAreNotOnTheShelf) {
  GiveCurrency("Spell Trace", ITEM_KIND_SPELL_TRACE, 60);
  GiveCurrency("Frozen Weapon Token", ITEM_KIND_TOKEN, 3);
  GiveStack("Wild Boar Tooth", 50, 4);
  GiveCurrency("Zakum's Soul Shard", ITEM_KIND_SOUL_SHARD, 9);
  MultiSellPanel panel(c_, account_);
  panel.Reset(kEtcTab, 0);
  EXPECT_EQ(panel.basket().etc, std::set<int>({0}));
  EXPECT_EQ(panel.Total(), 200);
  EXPECT_TRUE(ScreenHas(panel, "Wild Boar Tooth"));
  EXPECT_FALSE(ScreenHas(panel, "Frozen Weapon Token"));
  EXPECT_FALSE(ScreenHas(panel, "Soul Shard"));
  EXPECT_FALSE(ScreenHas(panel, "Spell Trace"));
}

TEST_F(MultiSellTest, ATraceIsMarkableAndPaysNothing) {
  GiveEquip("Sword", 1000);
  Equip state;
  state.set_equip_name(sword_.name());
  state.set_trace(true);
  c_.PickUp(std::make_unique<EquipTrace>(sword_, state));
  MultiSellPanel panel(c_, account_);
  panel.Reset(kEquipTab, 1);
  EXPECT_EQ(panel.basket().equips, std::set<int>({1}));
  EXPECT_EQ(panel.Total(), 0);
}

TEST_F(MultiSellTest, TheCursorRingRunsBarToRowsToButtons) {
  GiveEquip("Sword", 1000);
  GiveStack("Wild Boar Tooth", 50, 4);
  MultiSellPanel panel(c_, account_);
  panel.Reset(kEquipTab, 0);
  // Right does nothing while the cursor is on a row, since the tabs belong to
  // the bar.
  Press(panel, ftxui::Event::ArrowRight);
  EXPECT_TRUE(ScreenHas(panel, "Sword"));
  // Down from the only row lands on the buttons, and Enter there opens the
  // dialog.
  Press(panel, ftxui::Event::ArrowDown);
  Press(panel, ftxui::Event::Return);
  EXPECT_TRUE(panel.confirming());
  Press(panel, ftxui::Event::Escape);
  // Down again wraps to the bar, where Right switches tabs.
  Press(panel, ftxui::Event::ArrowDown);
  Press(panel, ftxui::Event::ArrowRight);
  EXPECT_TRUE(ScreenHas(panel, "Wild Boar Tooth"));
}

TEST_F(MultiSellTest, ConfirmDoesNothingWithAnEmptyBasket) {
  GiveEquip("Sword", 1000);
  MultiSellPanel panel(c_, account_);
  panel.Reset(kEquipTab, 0);
  Press(panel, ftxui::Event::Return);  // unmark the only row
  Press(panel, ftxui::Event::ArrowDown);
  EXPECT_EQ(Press(panel, ftxui::Event::Return), ConfirmChoice::kPending);
  EXPECT_FALSE(panel.confirming());
}

TEST_F(MultiSellTest, TheDialogOpensOnConfirm) {
  GiveEquip("Sword", 1000);
  MultiSellPanel panel(c_, account_);
  panel.Reset(kEquipTab, 0);
  Press(panel, ftxui::Event::ArrowDown);
  Press(panel, ftxui::Event::Return);
  // Escape backs out of the dialog without selling and leaves the screen open.
  EXPECT_EQ(Press(panel, ftxui::Event::Escape), ConfirmChoice::kPending);
  EXPECT_FALSE(panel.confirming());
  // Enter on the button row opens it again, and Enter confirms, since the
  // cursor is already on Confirm.
  Press(panel, ftxui::Event::Return);
  EXPECT_EQ(Press(panel, ftxui::Event::Return), ConfirmChoice::kConfirmed);
}

TEST_F(MultiSellTest, EscapeLeavesTheScreen) {
  GiveEquip("Sword", 1000);
  MultiSellPanel panel(c_, account_);
  panel.Reset(kEquipTab, 0);
  EXPECT_EQ(Press(panel, ftxui::Event::Escape), ConfirmChoice::kCancelled);
}

TEST_F(MultiSellTest, TheHeaderCarriesTheMesoAndTheRunningTotal) {
  c_.AddMeso(12345);
  GiveEquip("Sword", 1000);
  MultiSellPanel panel(c_, account_);
  panel.Reset(kEquipTab, 0);
  EXPECT_TRUE(ScreenHas(panel, "12,345"));
  EXPECT_TRUE(ScreenHas(panel, "+1,000"));
}

TEST_F(MultiSellTest, EveryRowShowsWhatItWouldPay) {
  GiveStack("Firewood", 0, 60);
  GiveStack("Wild Boar Tooth", 50, 4);
  MultiSellPanel panel(c_, account_);
  panel.Reset(kEtcTab, 0);
  // The whole stack's value, on its own row.
  EXPECT_NE(RowFor(panel, "Wild Boar Tooth").find("200"), std::string::npos);
  // A row worth nothing shows 0 rather than nothing.
  EXPECT_NE(RowFor(panel, "Firewood").find("0"), std::string::npos);
}

TEST_F(MultiSellTest, SellingPaysTheTotalAndEmptiesTheRows) {
  GiveEquip("Sword", 1000);
  GiveStack("Wild Boar Tooth", 50, 4);
  SaleBasket basket;
  basket.equips.insert(0);
  basket.etc.insert(0);
  EXPECT_EQ(BasketTotal(c_, basket), 1200);
  EXPECT_EQ(SellBasket(c_, basket), 1200);
  EXPECT_EQ(c_.inventory().size(), 0);
  EXPECT_TRUE(c_.stackables().empty());
  EXPECT_EQ(c_.meso(), 1200);
}

TEST_F(MultiSellTest, TheShelfReadsEquipThenEtcInBagOrder) {
  GiveEquip("Sword", 1000);
  GiveEquip("Axe", 2000);
  GiveStack("Wild Boar Tooth", 7, 3);
  GiveStack("Zzz Shell", 5, 2);
  SaleBasket basket;
  basket.equips = {0, 1};
  basket.etc = {0, 1};
  SellBasket(c_, basket);
  ASSERT_EQ(c_.buy_backs().size(), 4);
  EXPECT_EQ(c_.buy_backs()[0].equip().equip_name(), "Sword");
  EXPECT_EQ(c_.buy_backs()[1].equip().equip_name(), "Axe");
  EXPECT_EQ(c_.buy_backs()[2].stack().name(), "Wild Boar Tooth");
  EXPECT_EQ(c_.buy_backs()[3].stack().name(), "Zzz Shell");
}

// The list and the confirm window measure themselves separately, so both have
// to request the margin.
TEST_F(MultiSellTest, NeitherWindowWeldsARowToItsRightBorder) {
  GiveEquip("Fafnir Battle Cleaver", 1000000);
  GiveStack("Green Snail Shell", 7, 40);
  MultiSellPanel panel(c_, account_);
  panel.Reset(kEquipTab, 0);
  EXPECT_TRUE(RowsTouchingTheRightBorder(panel.Render()).empty());
  EXPECT_TRUE(RowsTouchingTheRightBorder(panel.RenderConfirm()).empty());
}
}  // namespace
}  // namespace ms
