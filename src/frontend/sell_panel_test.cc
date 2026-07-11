#include "src/frontend/sell_panel.h"

#include <gtest/gtest.h>

#include <string>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"

namespace ms {
namespace {

class SellPanelTest : public testing::Test {
 protected:
  static std::string Render(const SellPanel& panel) {
    ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(40),
                                                 ftxui::Dimension::Fixed(15));
    ftxui::Render(screen, panel.Render());
    return screen.ToString();
  }
};

TEST_F(SellPanelTest, DefaultsToWholeStack) {
  SellPanel panel;
  panel.Reset("Green Snail Shell", 7, 10);
  EXPECT_EQ(panel.quantity(), 10);
}

TEST_F(SellPanelTest, RenderShowsNameUnitPriceAndTotal) {
  SellPanel panel;
  panel.Reset("Green Snail Shell", 7, 10);  // default qty 10 -> total 70
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("Green Snail Shell"), std::string::npos);
  EXPECT_NE(rendered.find("each"), std::string::npos);
  EXPECT_NE(rendered.find("Total"), std::string::npos);
  EXPECT_NE(rendered.find("70"), std::string::npos);
}

TEST_F(SellPanelTest, ZeroButtonZeroesQuantity) {
  SellPanel panel;
  panel.Reset("Shell", 7, 10);
  panel.OnEvent(ftxui::Event::ArrowUp);  // Confirm -> "0" button
  panel.OnEvent(ftxui::Event::Return);
  EXPECT_EQ(panel.quantity(), 0);
}

TEST_F(SellPanelTest, MaxButtonRestoresWholeStack) {
  SellPanel panel;
  panel.Reset("Shell", 7, 100);
  panel.OnEvent(ftxui::Event::ArrowUp);     // -> "0" button
  panel.OnEvent(ftxui::Event::Return);      // quantity 0
  panel.OnEvent(ftxui::Event::ArrowRight);  // "0" -> "MAX"
  panel.OnEvent(ftxui::Event::Return);
  EXPECT_EQ(panel.quantity(), 100);
}

TEST_F(SellPanelTest, DigitsEditQuantity) {
  SellPanel panel;
  panel.Reset("Shell", 7, 100);
  panel.OnEvent(ftxui::Event::ArrowUp);  // -> "0" button
  panel.OnEvent(ftxui::Event::Return);   // quantity 0
  panel.OnEvent(ftxui::Event::Character('2'));
  panel.OnEvent(ftxui::Event::Character('5'));
  EXPECT_EQ(panel.quantity(), 25);
}

TEST_F(SellPanelTest, DigitsClampToMax) {
  SellPanel panel;
  panel.Reset("Shell", 7, 100);
  panel.OnEvent(ftxui::Event::ArrowUp);
  panel.OnEvent(ftxui::Event::Return);  // quantity 0
  panel.OnEvent(ftxui::Event::Character('9'));
  panel.OnEvent(ftxui::Event::Character('9'));
  panel.OnEvent(ftxui::Event::Character('9'));  // 999 -> clamp 100
  EXPECT_EQ(panel.quantity(), 100);
}

TEST_F(SellPanelTest, BackspaceDeletesLastDigit) {
  SellPanel panel;
  panel.Reset("Shell", 7, 100);
  panel.OnEvent(ftxui::Event::ArrowUp);
  panel.OnEvent(ftxui::Event::Return);  // quantity 0
  panel.OnEvent(ftxui::Event::Character('2'));
  panel.OnEvent(ftxui::Event::Character('5'));  // 25
  panel.OnEvent(ftxui::Event::Backspace);
  EXPECT_EQ(panel.quantity(), 2);
}

TEST_F(SellPanelTest, ConfirmIsFocusedByDefault) {
  SellPanel panel;
  panel.Reset("Shell", 7, 10);
  panel.OnEvent(ftxui::Event::Return);  // activate default button
  EXPECT_TRUE(panel.TakeConfirmed());
  EXPECT_FALSE(panel.TakeConfirmed());  // resets after read
}

TEST_F(SellPanelTest, RightFromConfirmActivatesCancel) {
  SellPanel panel;
  panel.Reset("Shell", 7, 10);
  panel.OnEvent(ftxui::Event::ArrowRight);  // Confirm -> Cancel
  panel.OnEvent(ftxui::Event::Return);
  EXPECT_TRUE(panel.TakeCancelled());
  EXPECT_FALSE(panel.TakeConfirmed());
}

TEST_F(SellPanelTest, EscapeCancels) {
  SellPanel panel;
  panel.Reset("Shell", 7, 10);
  panel.OnEvent(ftxui::Event::Escape);
  EXPECT_TRUE(panel.TakeCancelled());
}

TEST_F(SellPanelTest, UpFromCancelReachesMax) {
  SellPanel panel;
  panel.Reset("Shell", 7, 100);
  panel.OnEvent(ftxui::Event::ArrowUp);
  panel.OnEvent(ftxui::Event::Return);      // quantity 0
  panel.OnEvent(ftxui::Event::ArrowDown);   // back to Confirm row
  panel.OnEvent(ftxui::Event::ArrowRight);  // Confirm -> Cancel
  panel.OnEvent(ftxui::Event::ArrowUp);     // Cancel -> MAX
  panel.OnEvent(ftxui::Event::Return);
  EXPECT_EQ(panel.quantity(), 100);
}

}  // namespace
}  // namespace ms
