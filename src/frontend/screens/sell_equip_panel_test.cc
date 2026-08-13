#include "src/frontend/screens/sell_equip_panel.h"

#include <gtest/gtest.h>

#include <string>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/frontend/widgets/confirm_prompt.h"

namespace ms {
namespace {

// The [Confirm]/[Cancel] mechanics belong to confirm_prompt_test; these cover
// what this dialog says and where it opens.
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

// The starter sword and every trace come through here. The row keeps its
// shape rather than saying something else for nothing.
TEST_F(SellEquipPanelTest, WorthlessItemSellsForZero) {
  SellEquipPanel panel;
  panel.Reset("Sword", 0);
  EXPECT_NE(Render(panel).find("Sell for"), std::string::npos);
  EXPECT_NE(Render(panel).find("0"), std::string::npos);
}

// Enter on arrival sells. The shop keeps the sale on its buy-back shelf, so
// the dialog is a confirmation and not a warning.
TEST_F(SellEquipPanelTest, OpensOnConfirm) {
  SellEquipPanel panel;
  panel.Reset("Hunter's Bow", 1000);
  EXPECT_EQ(panel.OnEvent(ftxui::Event::Return), ConfirmChoice::kConfirmed);
}

TEST_F(SellEquipPanelTest, CancelsAfterSteppingOntoIt) {
  SellEquipPanel panel;
  panel.Reset("Hunter's Bow", 1000);
  EXPECT_EQ(panel.OnEvent(ftxui::Event::ArrowRight), ConfirmChoice::kPending);
  EXPECT_EQ(panel.OnEvent(ftxui::Event::Return), ConfirmChoice::kCancelled);
}

TEST_F(SellEquipPanelTest, EscapeCancels) {
  SellEquipPanel panel;
  panel.Reset("Hunter's Bow", 1000);
  EXPECT_EQ(panel.OnEvent(ftxui::Event::Escape), ConfirmChoice::kCancelled);
}

}  // namespace
}  // namespace ms
