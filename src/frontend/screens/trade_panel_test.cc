#include "src/frontend/screens/trade_panel.h"

#include <gtest/gtest.h>

#include <cstdlib>
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
#include "src/protos/multiplayer.pb.h"

namespace ms {
namespace {

TradeState Trade(const std::string& partner, bool joined) {
  TradeState trade;
  trade.set_id("t1");
  trade.set_partner_account_id("two");
  trade.set_partner_name(partner);
  trade.set_partner_joined(joined);
  return trade;
}

ItemPrototype Stack(const std::string& name, ItemKind kind) {
  ItemPrototype proto;
  proto.set_name(name);
  proto.set_category(ITEM_CATEGORY_ETC);
  proto.set_kind(kind);
  return proto;
}

class TradePanelTest : public PanelTest {
 protected:
  void SetUp() override {
    PanelTest::SetUp();
    c_.SetUsername("Dagger");
    c_.AddMeso(1234567);
    c_.AddItem(Stack(kSpellTraceName, ITEM_KIND_SPELL_TRACE), 900);
    c_.AddItem(Stack("Chaos Scroll", ITEM_KIND_UNSPECIFIED), 12);
    c_.AddItem(Stack("Zakum's", ITEM_KIND_SOUL_SHARD), 4);
    c_.PickUp(std::make_unique<EquipInstance>(sword_));
    panel_.SetTrade(Trade("Wand", /*joined=*/true));
    panel_.Reset();
  }

  std::vector<std::string> Rows() {
    ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(120),
                                                 ftxui::Dimension::Fixed(40));
    ftxui::Render(screen, ftxui::center(panel_.Render()));
    return ScreenRows(screen);
  }

  std::string Text() {
    std::string whole;
    for (const std::string& row : Rows()) {
      whole += row;
      whole += "\n";
    }
    return whole;
  }

  // The bag row the cursor is on, as the place in the character's own list.
  int BagIndex() {
    return panel_.cursor().index;
  }

  // Where a stack sits in the character's own list. Not its order of arrival:
  // 900 traces fill five stacks before the next item opens one.
  int StackIndex(const std::string& name) {
    for (int i = 0; i < static_cast<int>(c_.stackables().size()); ++i) {
      if (c_.stackables()[i].name() == name) {
        return i;
      }
    }
    return -1;
  }

  // Walks Tab until the bag has the cursor.
  void ToBag() {
    while (panel_.zone() != TradeZone::kBag) {
      panel_.NextZone(1);
    }
  }

  TradePanel panel_{c_, account_};
};

TEST_F(TradePanelTest, DrawsBothOffersAndTheBag) {
  TradeState trade = Trade("Wand", /*joined=*/true);
  trade.mutable_theirs()->set_meso(120);
  panel_.SetTrade(trade);
  panel_.PutUpCurrency(TradeCurrency::kMeso, 5000);
  panel_.PutUpCurrency(TradeCurrency::kSpellTraces, 30);

  std::string screen = Text();
  EXPECT_NE(screen.find("Dagger"), std::string::npos);
  EXPECT_NE(screen.find("Wand"), std::string::npos);
  EXPECT_NE(screen.find("Inventory"), std::string::npos);
  EXPECT_NE(screen.find("Accept"), std::string::npos);
  EXPECT_NE(screen.find("5,000"), std::string::npos);
  EXPECT_NE(screen.find("120"), std::string::npos);

  // The two currencies share one line on each side, the meso first.
  std::vector<std::string> rows = Rows();
  int line = -1;
  for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
    if (rows[i].find("5,000") != std::string::npos) {
      line = i;
    }
  }
  ASSERT_GE(line, 0);
  EXPECT_LT(rows[line].find("5,000"), rows[line].find("30"));
  EXPECT_NE(rows[line].find("120"), std::string::npos)
      << "both sides draw their currencies on the same row";
}

TEST_F(TradePanelTest, TheirWindowHasNoNameUntilTheyJoin) {
  panel_.SetTrade(Trade("Wand", /*joined=*/false));
  EXPECT_EQ(Text().find("Wand"), std::string::npos);

  panel_.SetTrade(Trade("Wand", /*joined=*/true));
  EXPECT_NE(Text().find("Wand"), std::string::npos);
}

TEST_F(TradePanelTest, TheirSideIsMirrored) {
  TradeState trade = Trade("Wand", /*joined=*/true);
  trade.mutable_theirs()->set_meso(120);
  trade.mutable_theirs()->set_spell_traces(77);
  trade.set_theirs_accepted(true);
  panel_.SetTrade(trade);

  std::vector<std::string> rows = Rows();
  int line = -1;
  for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
    if (rows[i].find("120") != std::string::npos) {
      line = i;
    }
  }
  ASSERT_GE(line, 0);
  // Their mark leads their half, their traces follow and their meso ends it;
  // the name sits over the meso rather than over the mark.
  EXPECT_LT(rows[line].find("✓"), rows[line].find("77"));
  EXPECT_LT(rows[line].find("77"), rows[line].find("120"));
  std::string title;
  for (const std::string& row : rows) {
    if (row.find("Wand") != std::string::npos) {
      title = row;
    }
  }
  ASSERT_FALSE(title.empty());
  EXPECT_GT(title.find("Wand"), title.size() / 2);
  // Padded out with the border's own rule rather than blanks, so the run in
  // front of the name reads as the border it sits in.
  EXPECT_NE(title.find("─ Wand "), std::string::npos) << title;
}

TEST_F(TradePanelTest, FocusLightsTheNameAndNotThePaddingBeforeIt) {
  panel_.NextZone(1);
  ASSERT_EQ(panel_.zone(), TradeZone::kTheirs);
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(120),
                                               ftxui::Dimension::Fixed(40));
  ftxui::Render(screen, ftxui::center(panel_.Render()));
  std::string styled = screen.ToString();

  EXPECT_NE(styled.find("\033[7m Wand "), std::string::npos)
      << "the name is the chip";
  EXPECT_EQ(styled.find("\033[7m─"), std::string::npos)
      << "the border run before it is not";
}

TEST_F(TradePanelTest, TheCursorWalksTheTopRowAndTheAcceptButton) {
  EXPECT_EQ(panel_.cursor().kind, TradeCursor::Kind::kCurrency);
  EXPECT_EQ(panel_.cursor().currency, TradeCurrency::kMeso);
  EXPECT_EQ(panel_.held(TradeCurrency::kMeso), 1234567);

  panel_.MoveCursor(1);
  EXPECT_EQ(panel_.cursor().currency, TradeCurrency::kSpellTraces);
  EXPECT_EQ(panel_.held(TradeCurrency::kSpellTraces), 900);

  panel_.MoveCursor(1);
  EXPECT_EQ(panel_.cursor().kind, TradeCursor::Kind::kAccept);

  // A ring of three.
  panel_.MoveCursor(1);
  EXPECT_EQ(panel_.cursor().currency, TradeCurrency::kMeso);
  panel_.MoveCursor(-1);
  EXPECT_EQ(panel_.cursor().kind, TradeCursor::Kind::kAccept);
}

TEST_F(TradePanelTest, TabWalksTheThreeWindows) {
  EXPECT_EQ(panel_.zone(), TradeZone::kMine);
  panel_.NextZone(1);
  EXPECT_EQ(panel_.zone(), TradeZone::kTheirs);
  panel_.NextZone(1);
  EXPECT_EQ(panel_.zone(), TradeZone::kBag);
  panel_.NextZone(1);
  EXPECT_EQ(panel_.zone(), TradeZone::kMine);
  panel_.NextZone(-1);
  EXPECT_EQ(panel_.zone(), TradeZone::kBag);
}

// The Etc tab lists the drops and nothing else: the currencies are counted in
// the purse, which the trade screen does not show, and the spell trace crosses
// on its own line at the top of the offer.
TEST_F(TradePanelTest, TheBagHasTwoTabsAndOnlyTradeableStacks) {
  ToBag();
  EXPECT_FALSE(panel_.on_etc_tab());
  EXPECT_EQ(panel_.cursor().kind, TradeCursor::Kind::kBag);
  EXPECT_EQ(BagIndex(), 0);  // the sword

  panel_.MoveCursor(1);
  ASSERT_TRUE(panel_.on_etc_tab());
  EXPECT_EQ(BagIndex(), StackIndex("Chaos Scroll"));
  panel_.MoveRow(1);
  EXPECT_EQ(BagIndex(), StackIndex("Chaos Scroll"));

  std::string screen = Text();
  EXPECT_NE(screen.find("Chaos Scroll"), std::string::npos);
  EXPECT_EQ(screen.find("Zakum's"), std::string::npos);
}

TEST_F(TradePanelTest, TheBagShowsWhatIsLeft) {
  ToBag();
  panel_.PutUpEquip(0);
  EXPECT_EQ(panel_.cursor().kind, TradeCursor::Kind::kNothing)
      << "the one equip is on the table, so the tab has nothing to stand on";

  panel_.MoveCursor(1);
  panel_.PutUpStack(StackIndex("Chaos Scroll"), 5);
  EXPECT_EQ(panel_.stack_left(StackIndex("Chaos Scroll")), 7);

  std::string screen = Text();
  EXPECT_NE(screen.find("Sword"), std::string::npos) << "on the table";
  EXPECT_NE(screen.find("7"), std::string::npos) << "the rest of the stack";

  // And the header counts what is left of the currencies.
  panel_.PutUpCurrency(TradeCurrency::kMeso, 1000000);
  EXPECT_NE(Text().find("234,567"), std::string::npos);
}

TEST_F(TradePanelTest, TheTableTakesAsMuchAsThePlayerHas) {
  constexpr int kMany = 20;
  for (int i = 0; i < kMany; ++i) {
    c_.PickUp(std::make_unique<EquipInstance>(sword_));
  }
  for (int i = 0; i < kMany; ++i) {
    panel_.PutUpEquip(i);
  }
  EXPECT_EQ(panel_.own().items(), kMany) << "the window scrolls instead";

  // Putting the same one up twice is still one thing on the table, and a
  // stack already up is changed rather than added to.
  panel_.PutUpEquip(0);
  EXPECT_EQ(panel_.own().items(), kMany);
  panel_.PutUpStack(StackIndex("Chaos Scroll"), 5);
  panel_.PutUpStack(StackIndex("Chaos Scroll"), 9);
  EXPECT_EQ(panel_.own().items(), kMany + 1);
  EXPECT_EQ(panel_.stack_offered(StackIndex("Chaos Scroll")), 9);
  panel_.PutUpStack(StackIndex("Chaos Scroll"), 0);
  EXPECT_EQ(panel_.own().items(), kMany);
}

TEST_F(TradePanelTest, WalkingAndTakingBackWhatIsOnTheTable) {
  panel_.PutUpEquip(0);
  panel_.PutUpStack(StackIndex("Chaos Scroll"), 5);

  // Down off the top row drops into the offer.
  panel_.MoveRow(1);
  EXPECT_EQ(panel_.cursor().kind, TradeCursor::Kind::kOffered);
  EXPECT_EQ(panel_.cursor().index, 0);
  panel_.MoveRow(1);
  EXPECT_EQ(panel_.cursor().index, 1);
  panel_.MoveRow(1);
  EXPECT_EQ(panel_.cursor().index, 1) << "the list does not wrap";

  panel_.TakeBack(1);
  EXPECT_EQ(panel_.own().items(), 1);
  EXPECT_EQ(panel_.stack_left(StackIndex("Chaos Scroll")), 12);

  // Up off the first row climbs back to the currencies.
  panel_.MoveRow(-1);
  EXPECT_EQ(panel_.cursor().kind, TradeCursor::Kind::kCurrency);
}

// The bag is the game's own bag: the same tab row with its rule under it, and
// the same column header with a rule of its own. The offer windows follow it.
TEST_F(TradePanelTest, EveryHeaderHasItsRule) {
  // One on the table and one left in the bag, so both lists have a header.
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  panel_.PutUpEquip(0);
  std::vector<std::string> rows = Rows();
  int tabs = -1;
  int columns = -1;
  int offer = -1;
  for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
    if (rows[i].find("Equip") != std::string::npos &&
        rows[i].find("Etc") != std::string::npos) {
      tabs = i;
    }
    if (rows[i].find("Equip Slot") != std::string::npos) {
      columns = i;
    }
    if (rows[i].find("Quantity") != std::string::npos && offer < 0) {
      offer = i;
    }
  }
  ASSERT_GE(tabs, 0);
  ASSERT_GE(columns, 0);
  ASSERT_GE(offer, 0);
  EXPECT_NE(rows[tabs + 1].find("├"), std::string::npos) << rows[tabs + 1];
  EXPECT_NE(rows[columns + 1].find("├"), std::string::npos)
      << rows[columns + 1];
  EXPECT_NE(rows[offer + 1].find("├"), std::string::npos) << rows[offer + 1];
}

// The one the rest of the game draws, with its own column between the caret
// and the name -- and only in the window holding the cursor.
TEST_F(TradePanelTest, OnlyTheFocusedWindowDrawsTheCaret) {
  panel_.PutUpEquip(0);
  panel_.MoveRow(1);
  ASSERT_EQ(panel_.cursor().kind, TradeCursor::Kind::kOffered);
  EXPECT_NE(Text().find("> Sword"), std::string::npos);

  ToBag();
  EXPECT_EQ(Text().find("> Sword"), std::string::npos)
      << "the offer keeps its row, not its caret";
}

// Wherever the cursor is, the menu opens beside it -- which is what reading
// the row off the render buys: all three lists scroll, and a place in the data
// stops agreeing with the row on screen as soon as one of them does.
TEST_F(TradePanelTest, TheMenuOpensBesideTheRowTheCursorIsOn) {
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  panel_.PutUpEquip(0);
  TradeState trade = Trade("Wand", /*joined=*/true);
  trade.mutable_theirs()->add_equips()->set_equip_name("Fafnir Mace");
  panel_.SetTrade(trade);

  for (int zone = 0; zone < 3; ++zone) {
    if (zone == 0) {
      panel_.MoveRow(1);  // down into your own offer
    }
    ASSERT_NE(panel_.cursor().kind, TradeCursor::Kind::kNothing) << zone;
    panel_.OpenMenu();
    ASSERT_TRUE(panel_.menu_open()) << zone;
    std::vector<std::string> rows = Rows();
    int caret = -1;
    int menu = -1;
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
      if (rows[i].find("> ") != std::string::npos && caret < 0) {
        caret = i;
      }
      if (rows[i].find("Inspect") != std::string::npos) {
        menu = i;
      }
    }
    ASSERT_GE(caret, 0) << zone;
    ASSERT_GE(menu, 0) << zone;
    EXPECT_LE(std::abs(menu - caret), 1)
        << "zone " << zone << ": the menu opened " << menu
        << " and the cursor is on " << caret;
    panel_.CloseMenu();
    panel_.NextZone(1);
  }
}

TEST_F(TradePanelTest, TheirRowsAreWalkedAndRead) {
  TradeState trade = Trade("Wand", /*joined=*/true);
  trade.mutable_theirs()->add_equips()->set_equip_name("Fafnir Mace");
  TradeStack* stack = trade.mutable_theirs()->add_stacks();
  stack->set_name("Chaos Scroll");
  stack->set_count(3);
  panel_.SetTrade(trade);
  panel_.NextZone(1);

  EXPECT_EQ(panel_.cursor().kind, TradeCursor::Kind::kTheirs);
  EXPECT_EQ(panel_.cursor().index, 0);
  panel_.MoveRow(1);
  EXPECT_EQ(panel_.cursor().index, 1);

  std::string screen = Text();
  EXPECT_NE(screen.find("Fafnir Mace"), std::string::npos);
  EXPECT_NE(screen.find("Chaos Scroll"), std::string::npos);
}

TEST_F(TradePanelTest, EachWindowsMenuOffersWhatItCanDo) {
  // The bag puts things up.
  ToBag();
  panel_.OpenMenu();
  ASSERT_TRUE(panel_.menu_open());
  std::string screen = Text();
  EXPECT_NE(screen.find("Inspect"), std::string::npos);
  EXPECT_NE(screen.find("Offer"), std::string::npos);
  EXPECT_EQ(screen.find("Remove"), std::string::npos);
  panel_.CloseMenu();

  // Your own side takes them back.
  panel_.PutUpEquip(0);
  panel_.NextZone(1);
  ASSERT_EQ(panel_.zone(), TradeZone::kMine);
  panel_.MoveRow(1);
  panel_.OpenMenu();
  ASSERT_TRUE(panel_.menu_open());
  screen = Text();
  EXPECT_NE(screen.find("Remove"), std::string::npos);
  EXPECT_EQ(screen.find("Offer"), std::string::npos);
  panel_.CloseMenu();

  // Theirs is only ever read.
  TradeState trade = Trade("Wand", /*joined=*/true);
  trade.mutable_theirs()->add_equips()->set_equip_name("Fafnir Mace");
  panel_.SetTrade(trade);
  panel_.NextZone(1);
  panel_.OpenMenu();
  ASSERT_TRUE(panel_.menu_open());
  screen = Text();
  EXPECT_NE(screen.find("Inspect"), std::string::npos);
  EXPECT_EQ(screen.find("Offer"), std::string::npos);
  EXPECT_EQ(screen.find("Remove"), std::string::npos);
}

TEST_F(TradePanelTest, NoMenuOnACurrencyOrTheButton) {
  panel_.OpenMenu();
  EXPECT_FALSE(panel_.menu_open()) << "a currency is its own action";

  panel_.MoveCursor(2);
  ASSERT_EQ(panel_.cursor().kind, TradeCursor::Kind::kAccept);
  panel_.OpenMenu();
  EXPECT_FALSE(panel_.menu_open());
}

TEST_F(TradePanelTest, TheWireCarriesTheWholeItem) {
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  panel_.PutUpEquip(1);
  panel_.PutUpStack(StackIndex("Chaos Scroll"), 5);
  panel_.PutUpCurrency(TradeCurrency::kMeso, 700);

  TradeOffer offer = panel_.own().ToWire(c_);
  EXPECT_EQ(offer.meso(), 700);
  ASSERT_EQ(offer.equips_size(), 1);
  EXPECT_EQ(offer.equips(0).equip_name(), "Sword");
  ASSERT_EQ(offer.stacks_size(), 1);
  EXPECT_EQ(offer.stacks(0).name(), "Chaos Scroll");
  EXPECT_EQ(offer.stacks(0).count(), 5);
}

}  // namespace
}  // namespace ms
