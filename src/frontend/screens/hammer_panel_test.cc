#include "src/frontend/screens/hammer_panel.h"

#include <gtest/gtest.h>

#include <string>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/frontend/testing/screen_text.h"
#include "src/frontend/widgets/confirm_prompt.h"
#include "src/item/equip_instance.h"

namespace ms {
namespace {

// confirm_prompt_test covers the [Confirm]/[Cancel] mechanics. These tests
// cover what this dialog says and what it does with an answer it can't carry
// out.
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

// A purse that can't cover it gets the question shown and refused, instead of a
// dialog that closes as if something happened.
TEST_F(HammerPanelTest, AnUnaffordableHammerCannotBeConfirmed) {
  HammerPanel panel;
  panel.Reset(kGoldenHammerCost - 1);
  EXPECT_FALSE(panel.affordable());
  EXPECT_EQ(panel.OnEvent(ftxui::Event::Return), ConfirmChoice::kPending);
  // It is still open, showing the same question.
  EXPECT_NE(Render(panel).find("Apply a Golden Hammer?"), std::string::npos);
  // Leaving still works.
  EXPECT_EQ(panel.OnEvent(ftxui::Event::Escape), ConfirmChoice::kCancelled);
}

TEST_F(HammerPanelTest, AnAffordableHammerConfirms) {
  HammerPanel panel;
  panel.Reset(kGoldenHammerCost);
  EXPECT_EQ(panel.OnEvent(ftxui::Event::Return), ConfirmChoice::kConfirmed);
}

TEST_F(HammerPanelTest, ThePriceKeepsOffTheRightBorder) {
  HammerPanel panel;
  panel.Reset(kGoldenHammerCost);
  EXPECT_TRUE(RowsTouchingTheRightBorder(panel.Render()).empty());
}
}  // namespace
}  // namespace ms
