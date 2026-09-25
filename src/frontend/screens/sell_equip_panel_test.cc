#include "src/frontend/screens/sell_equip_panel.h"

#include <gtest/gtest.h>

#include <string>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/frontend/testing/screen_text.h"
#include "src/frontend/widgets/confirm_prompt.h"

namespace ms {
namespace {

// confirm_prompt_test covers the [Confirm]/[Cancel] mechanics. These tests
// cover what this dialog says and where its cursor starts.
class SellEquipPanelTest : public testing::Test {
 protected:
  static std::string Render(const SellEquipPanel& panel) {
    ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(44),
                                                 ftxui::Dimension::Fixed(12));
    ftxui::Render(screen, panel.Render());
    return screen.ToString();
  }
};

TEST_F(SellEquipPanelTest, ShowsTheNameAndWhatItSellsFor) {
  SellEquipPanel panel;
  panel.Reset("Hunter's Bow", 1000);
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("Hunter's Bow"), std::string::npos);
  EXPECT_NE(rendered.find("Sell for"), std::string::npos);
  EXPECT_NE(rendered.find("1,000"), std::string::npos);
}

// The starter sword and every trace are sold through here. The row keeps its
// normal layout at a price of zero.
TEST_F(SellEquipPanelTest, WorthlessItemSellsForZero) {
  SellEquipPanel panel;
  panel.Reset("Sword", 0);
  EXPECT_NE(Render(panel).find("Sell for"), std::string::npos);
  EXPECT_NE(Render(panel).find("0"), std::string::npos);
}

// Enter right away sells. The shop keeps the sale on its buyback shelf, so the
// dialog is a confirmation rather than a warning. Reset reopens it, which puts
// the cursor back on [Confirm] between the three answers.
TEST_F(SellEquipPanelTest, EnterSellsAndSteppingOffOrEscapingDoesNot) {
  SellEquipPanel panel;
  panel.Reset("Hunter's Bow", 1000);
  EXPECT_EQ(panel.OnEvent(ftxui::Event::Return), ConfirmChoice::kConfirmed);

  panel.Reset("Hunter's Bow", 1000);
  EXPECT_EQ(panel.OnEvent(ftxui::Event::ArrowRight), ConfirmChoice::kPending);
  EXPECT_EQ(panel.OnEvent(ftxui::Event::Return), ConfirmChoice::kCancelled);

  panel.Reset("Hunter's Bow", 1000);
  EXPECT_EQ(panel.OnEvent(ftxui::Event::Escape), ConfirmChoice::kCancelled);
}

TEST_F(SellEquipPanelTest, ThePriceKeepsOffTheRightBorder) {
  SellEquipPanel panel;
  panel.Reset("Hunter's Bow", 1000);
  EXPECT_TRUE(RowsTouchingTheRightBorder(panel.Render()).empty());
}
}  // namespace
}  // namespace ms
