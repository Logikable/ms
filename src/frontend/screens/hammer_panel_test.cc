#include "src/frontend/screens/hammer_panel.h"

#include <gtest/gtest.h>

#include <string>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/frontend/widgets/confirm_prompt.h"
#include "src/item/equip_instance.h"

namespace ms {
namespace {

// The [Confirm]/[Cancel] mechanics belong to confirm_prompt_test; these cover
// what this dialog says and what it does with an answer it cannot honour.
class HammerPanelTest : public testing::Test {
 protected:
  static std::string Render(const HammerPanel& panel) {
    ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(48),
                                                 ftxui::Dimension::Fixed(12));
    ftxui::Render(screen, panel.Render());
    return screen.ToString();
  }
};

TEST_F(HammerPanelTest, AsksTheQuestionAndNamesThePrice) {
  HammerPanel panel;
  panel.Reset(kGoldenHammerCost);
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("Apply a Golden Hammer?"), std::string::npos);
  EXPECT_NE(rendered.find("10,000,000"), std::string::npos);
  EXPECT_TRUE(panel.affordable());
}

// A purse that cannot cover it gets the question asked and refused rather than
// a dialog that closes as though something happened.
TEST_F(HammerPanelTest, AnUnaffordableHammerCannotBeConfirmed) {
  HammerPanel panel;
  panel.Reset(kGoldenHammerCost - 1);
  EXPECT_FALSE(panel.affordable());
  EXPECT_EQ(panel.OnEvent(ftxui::Event::Return), ConfirmChoice::kPending);
  // And it is still up, saying the same thing.
  EXPECT_NE(Render(panel).find("Apply a Golden Hammer?"), std::string::npos);
  // Leaving still works.
  EXPECT_EQ(panel.OnEvent(ftxui::Event::Escape), ConfirmChoice::kCancelled);
}

TEST_F(HammerPanelTest, AnAffordableHammerConfirms) {
  HammerPanel panel;
  panel.Reset(kGoldenHammerCost);
  EXPECT_EQ(panel.OnEvent(ftxui::Event::Return), ConfirmChoice::kConfirmed);
}

}  // namespace
}  // namespace ms
