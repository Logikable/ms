#include "src/frontend/screens/buy_panel.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/frontend/testing/screen_text.h"
#include "src/frontend/widgets/colors.h"
#include "src/protos/item.pb.h"

namespace ms {
namespace {

std::string Render(const BuyPanel& panel) {
  ftxui::Element element = panel.Render();
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fit(element),
                                               ftxui::Dimension::Fit(element));
  ftxui::Render(screen, element);
  return screen.ToString();
}

// Whether any cell on the row containing `needle` has colour `color`. Reads the
// screen's pixels instead of its escape codes, because ftxui maps colours to
// whatever palette it thinks the terminal has, and a test process has no
// terminal, so the escape codes describe the fallback rather than the colour.
bool RowIsColored(const BuyPanel& panel, const std::string& needle,
                  ftxui::Color color) {
  ftxui::Element element = panel.Render();
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fit(element),
                                               ftxui::Dimension::Fit(element));
  ftxui::Render(screen, element);
  for (int y = 0; y < screen.dimy(); ++y) {
    std::string row;
    for (int x = 0; x < screen.dimx(); ++x) {
      // Unpainted cells hold an empty string, not a space. Dropping them would
      // join text that isn't actually adjacent.
      const std::string& ch = screen.PixelAt(x, y).character;
      row += ch.empty() ? " " : ch;
    }
    if (row.find(needle) == std::string::npos) {
      continue;
    }
    for (int x = 0; x < screen.dimx(); ++x) {
      if (screen.PixelAt(x, y).foreground_color == color) {
        return true;
      }
    }
    return false;
  }
  return false;
}

// The colour of the cell containing `cell`, on the row containing `row_needle`.
// A row is searched as bytes and read as columns, which differ: a border or a
// currency mark is one column but three bytes.
ftxui::Color CellColor(const BuyPanel& panel, const std::string& row_needle,
                       const std::string& cell) {
  ftxui::Element element = panel.Render();
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fit(element),
                                               ftxui::Dimension::Fit(element));
  ftxui::Render(screen, element);
  for (int y = 0; y < screen.dimy(); ++y) {
    std::string row;
    std::vector<int> column_of_byte;
    for (int x = 0; x < screen.dimx(); ++x) {
      std::string ch = screen.PixelAt(x, y).character;
      if (ch.empty()) {
        ch = " ";
      }
      row += ch;
      column_of_byte.insert(column_of_byte.end(), ch.size(), x);
    }
    if (row.find(row_needle) == std::string::npos) {
      continue;
    }
    size_t cell_at = row.find(cell);
    EXPECT_NE(cell_at, std::string::npos)
        << "'" << cell << "' is not on the '" << row_needle << "' row";
    if (cell_at == std::string::npos) {
      return ftxui::Color::Default;
    }
    return screen.PixelAt(column_of_byte[cell_at], y).foreground_color;
  }
  ADD_FAILURE() << "no row holding '" << row_needle << "'";
  return ftxui::Color::Default;
}

// A bag with more room than any of these tests needs, so the cap under test is
// the one that applies.
constexpr int kRoomy = 100000;

// A shopper picks a number, so the sell dialog's "all of it" default would be
// odd for a purchase. Zero owned is shown too, since a row that appeared only
// sometimes would look like a glitch.
TEST(BuyPanelTest, OpensAtOneAndSaysNoneAreOwned) {
  BuyPanel panel;
  panel.Reset("Machete", 10000, /*meso=*/50000, /*room=*/kRoomy, /*owned=*/0);
  EXPECT_EQ(panel.quantity(), 1);
  EXPECT_NE(Render(panel).find("Owned: 0"), std::string::npos);
}

// Starting at one doesn't mean offering only one: every other quantity dialog
// has the shortcuts, and [MAX] here means "as many as I can afford".
TEST(BuyPanelTest, HasTheQuickPickShortcuts) {
  BuyPanel panel;
  panel.Reset("Machete", 10000, /*meso=*/50000, /*room=*/kRoomy, /*owned=*/0);
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("[1]"), std::string::npos);
  EXPECT_NE(rendered.find("[MAX]"), std::string::npos);
  panel.OnEvent(ftxui::Event::ArrowRight);  // textbox -> [MAX]
  panel.OnEvent(ftxui::Event::Return);
  EXPECT_EQ(panel.quantity(), 5) << "50,000 buys five at 10,000";
}

TEST(BuyPanelTest, ShowsUnitPriceAndTotal) {
  BuyPanel panel;
  panel.Reset("Machete", 10000, /*meso=*/50000, /*room=*/kRoomy, /*owned=*/0);
  panel.OnEvent(ftxui::Event::Backspace);       // clear the 1
  panel.OnEvent(ftxui::Event::Character('3'));  // 3 of them
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("10,000 each"), std::string::npos);
  EXPECT_NE(rendered.find("Total: 🪙 30,000"), std::string::npos);
}

// The cap stops the player building a total the shop would refuse.
TEST(BuyPanelTest, CannotTypePastWhatTheBalanceCovers) {
  BuyPanel panel;
  panel.Reset("Machete", 10000, /*meso=*/25000, /*room=*/kRoomy, /*owned=*/0);
  panel.OnEvent(ftxui::Event::Backspace);
  panel.OnEvent(ftxui::Event::Character('9'));
  EXPECT_EQ(panel.quantity(), 2) << "25,000 buys two at 10,000";
}

// A player who can't afford one still gets the dialog, and it says why instead
// of refusing to open.
TEST(BuyPanelTest, AnUnaffordableItemOpensAtZero) {
  BuyPanel panel;
  panel.Reset("Gladius", 20000, /*meso=*/500, /*room=*/kRoomy, /*owned=*/0);
  EXPECT_EQ(panel.quantity(), 0);
  EXPECT_TRUE(RowIsColored(panel, "Total", kRed))
      << "the zero total should not read as a live purchase";
  panel.OnEvent(ftxui::Event::ArrowDown);  // textbox -> [Confirm]
  EXPECT_EQ(panel.OnEvent(ftxui::Event::Return), ConfirmChoice::kPending);
}

TEST(BuyPanelTest, ConfirmBuysAnAffordableAmountAndEscapeDoesNot) {
  BuyPanel panel;
  panel.Reset("Machete", 10000, /*meso=*/50000, /*room=*/kRoomy, /*owned=*/0);
  EXPECT_EQ(panel.OnEvent(ftxui::Event::Escape), ConfirmChoice::kCancelled);

  panel.Reset("Machete", 10000, /*meso=*/50000, /*room=*/kRoomy, /*owned=*/0);
  panel.OnEvent(ftxui::Event::ArrowDown);  // textbox -> [Confirm]
  EXPECT_EQ(panel.OnEvent(ftxui::Event::Return), ConfirmChoice::kConfirmed);
  EXPECT_EQ(panel.quantity(), 1);
}

// Backspacing to empty is the only way to reach zero with meso in hand, and
// zero isn't a purchase.
TEST(BuyPanelTest, ZeroIsNotSomethingToConfirm) {
  BuyPanel panel;
  panel.Reset("Machete", 10000, /*meso=*/50000, /*room=*/kRoomy, /*owned=*/0);
  panel.OnEvent(ftxui::Event::Backspace);
  ASSERT_EQ(panel.quantity(), 0);
  panel.OnEvent(ftxui::Event::ArrowDown);
  EXPECT_EQ(panel.OnEvent(ftxui::Event::Return), ConfirmChoice::kPending);
}

// --- the three caps ---

// Room caps the quantity even when the balance would cover far more.
TEST(BuyPanelTest, CannotTypePastWhatTheBagHasRoomFor) {
  BuyPanel panel;
  panel.Reset("Machete", 10, /*meso=*/1000000, /*room=*/3, /*owned=*/0);
  panel.OnEvent(ftxui::Event::Backspace);
  panel.OnEvent(ftxui::Event::Character('9'));
  EXPECT_EQ(panel.quantity(), 3);
}

// A full bag behaves like an unaffordable item: the dialog opens and says no
// instead of offering a number that would be refused. [1] doesn't get around
// it, since a button that set one anyway would give Confirm an amount the shop
// refuses, a purchase that silently does nothing.
TEST(BuyPanelTest, AFullBagOpensAtZeroAndCannotConfirm) {
  BuyPanel panel;
  panel.Reset("Machete", 10, /*meso=*/1000000, /*room=*/0, /*owned=*/0);
  EXPECT_EQ(panel.quantity(), 0);
  panel.OnEvent(ftxui::Event::ArrowLeft);  // textbox -> [1]
  panel.OnEvent(ftxui::Event::Return);
  EXPECT_EQ(panel.quantity(), 0);
  panel.OnEvent(ftxui::Event::ArrowDown);  // [1] -> [Confirm]
  EXPECT_EQ(panel.OnEvent(ftxui::Event::Return), ConfirmChoice::kPending);
}

// Both dead ends show the same 0, so the reason row is all that tells a full
// bag from a purse with no meso.
TEST(BuyPanelTest, AClosedDialogSaysWhichCeilingClosedIt) {
  BuyPanel panel;
  panel.Reset("Machete", 10, /*meso=*/1000000, /*room=*/0, /*owned=*/0);
  EXPECT_NE(Render(panel).find("Bag full"), std::string::npos);
  EXPECT_EQ(CellColor(panel, "Bag full", "B"), kRed);

  // The purse is named only when the bag isn't the problem, since earning more
  // won't help an item with nowhere to go.
  panel.Reset("Machete", 10, /*meso=*/9, /*room=*/kRoomy, /*owned=*/0);
  EXPECT_NE(Render(panel).find("Not enough meso"), std::string::npos);

  // Nothing to explain while the shop can still sell one.
  panel.Reset("Machete", 10, /*meso=*/1000000, /*room=*/kRoomy, /*owned=*/0);
  EXPECT_EQ(Render(panel).find("Bag full"), std::string::npos);
  EXPECT_EQ(Render(panel).find("Not enough"), std::string::npos);
}

// The balance and the bag aren't the only limits: the field itself stops at a
// full stack of spell traces.
TEST(BuyPanelTest, CannotTypePastTheQuantityLimit) {
  BuyPanel panel;
  panel.Reset("Machete", 1, /*meso=*/100000000, /*room=*/kRoomy, /*owned=*/0);
  panel.OnEvent(ftxui::Event::Backspace);
  for (int i = 0; i < 6; ++i) {
    panel.OnEvent(ftxui::Event::Character('9'));
  }
  // The literal rather than the constant, since a test that reads the limit
  // from the code under test can't notice the limit changing.
  EXPECT_EQ(panel.quantity(), 30000);
}

// The limit is a ceiling, not a floor: it doesn't raise a cap the balance or
// the bag has already set lower.
TEST(BuyPanelTest, TheQuantityLimitYieldsToATighterCap) {
  BuyPanel panel;
  panel.Reset("Machete", 1, /*meso=*/50, /*room=*/kRoomy, /*owned=*/0);
  panel.OnEvent(ftxui::Event::Backspace);
  for (int i = 0; i < 6; ++i) {
    panel.OnEvent(ftxui::Event::Character('9'));
  }
  EXPECT_EQ(panel.quantity(), 50);
}

// --- what the player already has ---

// The question a shopper asks before the price: buying a second of something is
// a different decision from buying the first.
TEST(BuyPanelTest, ShowsHowManyAreAlreadyOwned) {
  BuyPanel panel;
  panel.Reset("Machete", 10000, /*meso=*/50000, /*room=*/kRoomy, /*owned=*/3);
  std::string rendered = Render(panel);
  size_t owned = rendered.find("Owned: 3");
  size_t price = rendered.find("10,000 each");
  ASSERT_NE(owned, std::string::npos);
  ASSERT_NE(price, std::string::npos);
  EXPECT_LT(owned, price) << "above the price, which is the later question";
}

// The shop never stocks a free item, but the buyback shelf can: a trace, or
// anything the shop doesn't sell, sells for nothing and is bought back for
// nothing. The balance can't cap what costs nothing.
TEST(BuyPanelTest, AFreeItemCanBeTakenWithNoMeso) {
  BuyPanel panel;
  panel.Reset("Sword Trace", /*unit_price=*/0, /*meso=*/0, /*room=*/1,
              /*owned=*/0);
  EXPECT_EQ(panel.quantity(), 1);
  panel.OnEvent(ftxui::Event::ArrowDown);  // the field -> [Confirm]
  EXPECT_EQ(panel.OnEvent(ftxui::Event::Return), ConfirmChoice::kConfirmed);
}

// --- priced in a token ---

ItemPrototype WeaponToken() {
  ItemPrototype token;
  token.set_name("Frozen Weapon Token");
  token.set_currency_mark("●");
  token.set_currency_color(CURRENCY_COLOR_THEME);
  return token;
}

// Same arithmetic, different balance: the dialog counts tokens and shows the
// token's own mark where the coin would be.
TEST(BuyPanelTest, ATokenPriceIsCountedInTokens) {
  ItemPrototype token = WeaponToken();
  BuyPanel panel;
  panel.Reset("Frozen Sword", /*unit_price=*/1, /*balance=*/3,
              /*room=*/kRoomy, /*owned=*/0, &token);
  panel.OnEvent(ftxui::Event::Backspace);
  panel.OnEvent(ftxui::Event::Character('9'));
  EXPECT_EQ(panel.quantity(), 3) << "three tokens buy three";
  // Read cell by cell, because the mark is coloured and ToString puts escapes
  // between it and the number.
  std::string rendered = Render(panel);
  EXPECT_EQ(rendered.find("🪙"), std::string::npos) << "no meso on this shelf";
  EXPECT_NE(rendered.find("each"), std::string::npos);
}

// A shelf priced in tokens names the token the player is short of, not meso.
TEST(BuyPanelTest, AShortTokenBalanceNamesTheToken) {
  ItemPrototype token = WeaponToken();
  BuyPanel panel;
  panel.Reset("Frozen Sword", /*unit_price=*/1, /*balance=*/0, /*room=*/kRoomy,
              /*owned=*/0, &token);
  EXPECT_NE(Render(panel).find("Not enough Frozen Weapon Token"),
            std::string::npos);
}

// Red means the reason, and a currency isn't a reason, so the mark keeps its
// own colour while the number beside it turns red.
TEST(BuyPanelTest, AnUnaffordableTokenTotalReddensTheNumberOnly) {
  ItemPrototype token = WeaponToken();
  BuyPanel panel;
  panel.Reset("Frozen Sword", /*unit_price=*/1, /*balance=*/0, /*room=*/kRoomy,
              /*owned=*/0, &token);
  EXPECT_EQ(panel.quantity(), 0) << "nothing to buy it with";
  EXPECT_EQ(CellColor(panel, "Total:", "0"), kRed);
  EXPECT_EQ(CellColor(panel, "Total:", "●"), kTheme)
      << "the mark is the currency, not the reason";
  EXPECT_EQ(CellColor(panel, "each", "●"), kTheme);
}

TEST(BuyPanelTest, TheTotalKeepsOffTheRightBorder) {
  BuyPanel panel;
  panel.Reset("Machete", 10000, /*meso=*/50000, /*room=*/kRoomy, /*owned=*/0);
  EXPECT_TRUE(RowsTouchingTheRightBorder(panel.Render()).empty());
}
}  // namespace
}  // namespace ms
