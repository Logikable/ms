#include "src/frontend/sell_panel.h"

#include <gtest/gtest.h>

#include <string>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"

namespace ms {
namespace {

// The quantity textbox + [1]/[MAX] + Confirm/Cancel mechanics are covered by
// amount_selector_test; these cover the sell-specific header and wiring.
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

TEST_F(SellPanelTest, TotalTracksTheChosenQuantity) {
  SellPanel panel;
  panel.Reset("Shell", 7, 10);
  panel.OnEvent(ftxui::Event::ArrowLeft);  // textbox -> [1]
  panel.OnEvent(ftxui::Event::Return);     // quantity 1 -> total 7
  EXPECT_EQ(panel.quantity(), 1);
  EXPECT_EQ(Render(panel).find("70"), std::string::npos);  // total 70 -> 7
}

TEST_F(SellPanelTest, ConfirmAndCancelPassThrough) {
  SellPanel panel;
  panel.Reset("Shell", 7, 10);
  panel.OnEvent(ftxui::Event::ArrowDown);  // textbox -> [Confirm]
  panel.OnEvent(ftxui::Event::Return);
  EXPECT_TRUE(panel.TakeConfirmed());

  panel.Reset("Shell", 7, 10);
  panel.OnEvent(ftxui::Event::Escape);
  EXPECT_TRUE(panel.TakeCancelled());
}

}  // namespace
}  // namespace ms
