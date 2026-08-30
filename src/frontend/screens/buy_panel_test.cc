#include "src/frontend/screens/buy_panel.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/screen.hpp"
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

// Whether any cell on the row holding `needle` is painted `color`. Reads the
// screen's pixels rather than its escape codes: ftxui collapses colours into
// whatever palette it believes the terminal has, and a test process has no
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
      // Unpainted cells hold an empty string, not a space; dropping them would
      // join text that is not actually adjacent.
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

// The colour of the cell holding `cell`, on the row holding `row_needle`. A row
// is searched as bytes and read as columns, which are not the same thing: a
// border or a currency mark is one column and three bytes.
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

// A bag with more room than any of these tests is about, so the cap under
// test is the one that bites.
constexpr int kRoomy = 100000;

// A shopper picks a number; the sell dialog's "all of it" default would be an
// odd thing to open a purchase on.
TEST(BuyPanelTest, OpensAtOne) {
  BuyPanel panel;
  panel.Reset("Machete", 10000, /*meso=*/50000, /*room=*/kRoomy, /*owned=*/0);
  EXPECT_EQ(panel.quantity(), 1);
}

// Opening at one is not the same as offering only one: every other quantity
// dialog carries the shortcuts, and [MAX] here is "as many as I can afford".
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

// The cap is what stops the player building a total the shop would refuse.
TEST(BuyPanelTest, CannotTypePastWhatTheBalanceCovers) {
  BuyPanel panel;
  panel.Reset("Machete", 10000, /*meso=*/25000, /*room=*/kRoomy, /*owned=*/0);
  panel.OnEvent(ftxui::Event::Backspace);
  panel.OnEvent(ftxui::Event::Character('9'));
  EXPECT_EQ(panel.quantity(), 2) << "25,000 buys two at 10,000";
}

// A player who cannot afford one still gets the dialog, and it says why
// rather than refusing to open.
TEST(BuyPanelTest, AnUnaffordableItemOpensAtZero) {
  BuyPanel panel;
  panel.Reset("Gladius", 20000, /*meso=*/500, /*room=*/kRoomy, /*owned=*/0);
  EXPECT_EQ(panel.quantity(), 0);
  EXPECT_TRUE(RowIsColored(panel, "Total", kRed))
      << "the zero total should not read as a live purchase";
  panel.OnEvent(ftxui::Event::ArrowDown);  // textbox -> [Confirm]
  EXPECT_EQ(panel.OnEvent(ftxui::Event::Return), ConfirmChoice::kPending);
}

TEST(BuyPanelTest, ConfirmWorksOnAnAffordableAmount) {
  BuyPanel panel;
  panel.Reset("Machete", 10000, /*meso=*/50000, /*room=*/kRoomy, /*owned=*/0);
  panel.OnEvent(ftxui::Event::ArrowDown);  // textbox -> [Confirm]
  EXPECT_EQ(panel.OnEvent(ftxui::Event::Return), ConfirmChoice::kConfirmed);
  EXPECT_EQ(panel.quantity(), 1);
}

TEST(BuyPanelTest, EscapeCancels) {
  BuyPanel panel;
  panel.Reset("Machete", 10000, /*meso=*/50000, /*room=*/kRoomy, /*owned=*/0);
  EXPECT_EQ(panel.OnEvent(ftxui::Event::Escape), ConfirmChoice::kCancelled);
}

// Backspacing to nothing is the one way to reach zero with meso in hand, and
// zero is not a purchase.
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

// A full bag reads like an unaffordable item: the dialog opens and says no,
// rather than offering a number that would be refused.
TEST(BuyPanelTest, AFullBagOpensAtZeroAndCannotConfirm) {
  BuyPanel panel;
  panel.Reset("Machete", 10, /*meso=*/1000000, /*room=*/0, /*owned=*/0);
  EXPECT_EQ(panel.quantity(), 0);
  panel.OnEvent(ftxui::Event::ArrowDown);  // textbox -> [Confirm]
  EXPECT_EQ(panel.OnEvent(ftxui::Event::Return), ConfirmChoice::kPending);
}

// Neither the balance nor the bag is the only ceiling: the field itself stops,
// at a full stack of spell traces.
TEST(BuyPanelTest, CannotTypePastTheQuantityLimit) {
  BuyPanel panel;
  panel.Reset("Machete", 1, /*meso=*/100000000, /*room=*/kRoomy, /*owned=*/0);
  panel.OnEvent(ftxui::Event::Backspace);
  for (int i = 0; i < 6; ++i) {
    panel.OnEvent(ftxui::Event::Character('9'));
  }
  // The literal rather than the constant: a test that reads the limit off the
  // thing under test cannot notice the limit changing.
  EXPECT_EQ(panel.quantity(), 30000);
}

// The limit is a ceiling, not a floor: it does not raise a cap the balance or
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

// The question a shopper asks before the price: buying a second of something
// is a different decision from buying a first.
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

// Zero is an answer. A row that appeared only sometimes would be read as the
// dialog having glitched rather than as "none yet".
TEST(BuyPanelTest, SaysOwnedZeroRatherThanDroppingTheRow) {
  BuyPanel panel;
  panel.Reset("Machete", 10000, /*meso=*/50000, /*room=*/kRoomy, /*owned=*/0);
  EXPECT_NE(Render(panel).find("Owned: 0"), std::string::npos);
}

// The shop never stocks a free item, but the buy-back shelf does -- a trace,
// or anything the shop does not sell, sold for nothing and comes back for it.
// The balance cannot cap what costs nothing.
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
  token.set_category(ITEM_CATEGORY_ETC);
  token.set_currency_mark("●");
  token.set_currency_color(CURRENCY_COLOR_THEME);
  return token;
}

// Same arithmetic, different balance: the dialog counts tokens and draws the
// token's own mark where the coin would be.
TEST(BuyPanelTest, ATokenPriceIsCountedInTokens) {
  ItemPrototype token = WeaponToken();
  BuyPanel panel;
  panel.Reset("Frozen Sword", /*unit_price=*/1, /*balance=*/3,
              /*room=*/kRoomy, /*owned=*/0, &token);
  panel.OnEvent(ftxui::Event::Backspace);
  panel.OnEvent(ftxui::Event::Character('9'));
  EXPECT_EQ(panel.quantity(), 3) << "three tokens buy three";
  // Read cell by cell: the mark is coloured, so ToString threads escapes
  // between it and the number.
  std::string rendered = Render(panel);
  EXPECT_EQ(rendered.find("🪙"), std::string::npos) << "no meso on this shelf";
  EXPECT_NE(rendered.find("each"), std::string::npos);
}

// Red is the reason, and a currency is not a reason -- so the mark stays its
// own colour while the number it is beside goes red.
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

}  // namespace
}  // namespace ms
