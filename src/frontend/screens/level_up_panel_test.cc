#include "src/frontend/screens/level_up_panel.h"

#include <gtest/gtest.h>

#include <string>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/frontend/testing/screen_text.h"
#include "src/frontend/widgets/confirm_prompt.h"
#include "src/frontend/widgets/format.h"

namespace ms {
namespace {

// confirm_prompt_test covers the [Confirm]/[Cancel] mechanics. These tests
// cover what this dialog says and what it does with an answer it can't carry
// out.
class LevelUpPanelTest : public testing::Test {
 protected:
  static std::string Render(const LevelUpPanel& panel) {
    ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(48),
                                                 ftxui::Dimension::Fixed(12));
    ftxui::Render(screen, panel.Render());
    return screen.ToString();
  }
};

TEST_F(LevelUpPanelTest, ShowsTheRungAndItsPrice) {
  LevelUpPanel panel;
  panel.Reset("Arcane Symbol: Vanishing Journey", 8, FormatMeso(1'810'000),
              5'000'000 >= 1'810'000);
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("Vanishing Journey"), std::string::npos);
  EXPECT_NE(rendered.find("8"), std::string::npos);
  EXPECT_NE(rendered.find("9"), std::string::npos);
  EXPECT_NE(rendered.find("1,810,000"), std::string::npos);
  EXPECT_TRUE(panel.affordable());
}

// A purse that can't cover the level gets the question shown and refused,
// instead of a dialog that closes as if something happened.
TEST_F(LevelUpPanelTest, AnUnaffordableRungCannotBeConfirmed) {
  LevelUpPanel panel;
  panel.Reset("Arcane Symbol: Chu Chu Island", 3, FormatMeso(1'810'000),
              100 >= 1'810'000);
  EXPECT_FALSE(panel.affordable());
  EXPECT_EQ(panel.OnEvent(ftxui::Event::Return), ConfirmChoice::kPending);
  // It is still open, showing the same question.
  EXPECT_NE(Render(panel).find("Chu Chu Island"), std::string::npos);
  // Leaving still works.
  EXPECT_EQ(panel.OnEvent(ftxui::Event::Escape), ConfirmChoice::kCancelled);
}

TEST_F(LevelUpPanelTest, AnAffordableRungConfirms) {
  LevelUpPanel panel;
  panel.Reset("Arcane Symbol: Morass", 1, FormatMeso(970'000),
              970'000 >= 970'000);
  EXPECT_EQ(panel.OnEvent(ftxui::Event::Return), ConfirmChoice::kConfirmed);
}

TEST_F(LevelUpPanelTest, ThePriceKeepsOffTheRightBorder) {
  LevelUpPanel panel;
  panel.Reset("Arcane Symbol: Vanishing Journey", 8, FormatMeso(1'810'000),
              5'000'000 >= 1'810'000);
  EXPECT_TRUE(RowsTouchingTheRightBorder(panel.Render()).empty());
}
}  // namespace
}  // namespace ms
