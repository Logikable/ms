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

TEST_F(SellEquipPanelTest, ShowsTheNameAndWhatItPays) {
  SellEquipPanel panel;
  panel.Reset("Hunter's Bow", 1000, /*upgraded=*/false);
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("Hunter's Bow"), std::string::npos);
  EXPECT_NE(rendered.find("1,000"), std::string::npos);
  EXPECT_EQ(rendered.find("not paid for"), std::string::npos);
}

// A worthless item is being thrown away, not sold badly, and "0" alone does not
// say so.
TEST_F(SellEquipPanelTest, WorthlessItemSaysSoInWords) {
  SellEquipPanel panel;
  panel.Reset("Sword", 0, /*upgraded=*/false);
  EXPECT_NE(Render(panel).find("Pays nothing"), std::string::npos);
}

// The warning the dialog exists for: the price covers the base item only.
TEST_F(SellEquipPanelTest, WarnsWhenTheCopyCarriesUpgrades) {
  SellEquipPanel panel;
  panel.Reset("Zard ★12", 3000, /*upgraded=*/true);
  EXPECT_NE(Render(panel).find("not paid for"), std::string::npos);
}

// Enter on arrival must not sell. A sale cannot be undone, so the cursor opens
// on the way out.
TEST_F(SellEquipPanelTest, OpensOnCancel) {
  SellEquipPanel panel;
  panel.Reset("Hunter's Bow", 1000, /*upgraded=*/false);
  EXPECT_EQ(panel.OnEvent(ftxui::Event::Return), ConfirmChoice::kCancelled);
}

TEST_F(SellEquipPanelTest, ConfirmsAfterSteppingOntoIt) {
  SellEquipPanel panel;
  panel.Reset("Hunter's Bow", 1000, /*upgraded=*/false);
  EXPECT_EQ(panel.OnEvent(ftxui::Event::ArrowLeft), ConfirmChoice::kPending);
  EXPECT_EQ(panel.OnEvent(ftxui::Event::Return), ConfirmChoice::kConfirmed);
}

TEST_F(SellEquipPanelTest, EscapeCancels) {
  SellEquipPanel panel;
  panel.Reset("Hunter's Bow", 1000, /*upgraded=*/false);
  EXPECT_EQ(panel.OnEvent(ftxui::Event::Escape), ConfirmChoice::kCancelled);
}

}  // namespace
}  // namespace ms
