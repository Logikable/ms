#include "src/frontend/screens/buy_panel.h"

#include <gtest/gtest.h>

#include <string>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/frontend/widgets/colors.h"

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

// A shopper picks a number; the sell dialog's "all of it" default would be an
// odd thing to open a purchase on.
TEST(BuyPanelTest, OpensAtOne) {
  BuyPanel panel;
  panel.Reset("Machete", 10000, /*meso=*/50000);
  EXPECT_EQ(panel.quantity(), 1);
}

TEST(BuyPanelTest, HasNoQuickPickShortcuts) {
  BuyPanel panel;
  panel.Reset("Machete", 10000, /*meso=*/50000);
  std::string rendered = Render(panel);
  EXPECT_EQ(rendered.find("[1]"), std::string::npos);
  EXPECT_EQ(rendered.find("[MAX]"), std::string::npos);
}

TEST(BuyPanelTest, ShowsUnitPriceAndTotal) {
  BuyPanel panel;
  panel.Reset("Machete", 10000, /*meso=*/50000);
  panel.OnEvent(ftxui::Event::Backspace);       // clear the 1
  panel.OnEvent(ftxui::Event::Character('3'));  // 3 of them
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("10,000 each"), std::string::npos);
  EXPECT_NE(rendered.find("Total: 🪙 30,000"), std::string::npos);
}

// The cap is what stops the player building a total the shop would refuse.
TEST(BuyPanelTest, CannotTypePastWhatTheBalanceCovers) {
  BuyPanel panel;
  panel.Reset("Machete", 10000, /*meso=*/25000);
  panel.OnEvent(ftxui::Event::Backspace);
  panel.OnEvent(ftxui::Event::Character('9'));
  EXPECT_EQ(panel.quantity(), 2) << "25,000 buys two at 10,000";
}

// A player who cannot afford one still gets the dialog, and it says why
// rather than refusing to open.
TEST(BuyPanelTest, AnUnaffordableItemOpensAtZeroAndCannotConfirm) {
  BuyPanel panel;
  panel.Reset("Gladius", 20000, /*meso=*/500);
  EXPECT_EQ(panel.quantity(), 0);
  EXPECT_TRUE(RowIsColored(panel, "Total", kRed))
      << "the zero total should not read as a live purchase";
  panel.OnEvent(ftxui::Event::ArrowDown);  // textbox -> [Confirm]
  panel.OnEvent(ftxui::Event::Return);
  EXPECT_FALSE(panel.TakeConfirmed());
}

TEST(BuyPanelTest, ConfirmWorksOnAnAffordableAmount) {
  BuyPanel panel;
  panel.Reset("Machete", 10000, /*meso=*/50000);
  panel.OnEvent(ftxui::Event::ArrowDown);  // textbox -> [Confirm]
  panel.OnEvent(ftxui::Event::Return);
  EXPECT_TRUE(panel.TakeConfirmed());
  EXPECT_EQ(panel.quantity(), 1);
}

TEST(BuyPanelTest, EscapeCancels) {
  BuyPanel panel;
  panel.Reset("Machete", 10000, /*meso=*/50000);
  panel.OnEvent(ftxui::Event::Escape);
  EXPECT_TRUE(panel.TakeCancelled());
}

// Backspacing to nothing is the one way to reach zero with meso in hand, and
// zero is not a purchase.
TEST(BuyPanelTest, ZeroIsNotSomethingToConfirm) {
  BuyPanel panel;
  panel.Reset("Machete", 10000, /*meso=*/50000);
  panel.OnEvent(ftxui::Event::Backspace);
  ASSERT_EQ(panel.quantity(), 0);
  panel.OnEvent(ftxui::Event::ArrowDown);
  panel.OnEvent(ftxui::Event::Return);
  EXPECT_FALSE(panel.TakeConfirmed());
}

}  // namespace
}  // namespace ms
