#include "src/frontend/screens/dailies_panel.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/frontend/testing/screen_text.h"

namespace ms {
namespace {

// Beside a filler, the way the dialog is centred on screen: the window keeps
// its own width rather than stretching to the terminal's.
ftxui::Screen Render(const DailiesPanel& panel) {
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(60),
                                               ftxui::Dimension::Fixed(12));
  ftxui::Render(screen, ftxui::hbox({panel.Render(), ftxui::filler()}));
  return screen;
}

// The rows line their counts up under each other, and neither end of a row
// sits against the border.
TEST(DailiesPanelTest, TheRowsAreSpacedOffBothBorders) {
  DailiesPanel panel;
  panel.Reset({{"Arcane Symbol: Vanishing Journey", 20},
               {"Arcane Symbol: Chu Chu Island", 20}});
  ftxui::Screen screen = Render(panel);
  EXPECT_NE(ScreenRow(screen, 1).find("Claim today's dailies?"),
            std::string::npos);
  EXPECT_NE(
      ScreenRow(screen, 3).find("│ Arcane Symbol: Vanishing Journey  x20 │"),
      std::string::npos)
      << ScreenText(screen);
  EXPECT_NE(
      ScreenRow(screen, 4).find("│ Arcane Symbol: Chu Chu Island     x20 │"),
      std::string::npos)
      << ScreenText(screen);
}

TEST(DailiesPanelTest, PassesTheAnswerThrough) {
  DailiesPanel panel;
  panel.Reset({{"Arcane Symbol: Vanishing Journey", 20}});
  EXPECT_EQ(panel.OnEvent(ftxui::Event::Escape), ConfirmChoice::kCancelled);

  // It opens on [Confirm]: the claim is what the player pressed Enter for.
  panel.Reset({{"Arcane Symbol: Vanishing Journey", 20}});
  EXPECT_EQ(panel.OnEvent(ftxui::Event::Return), ConfirmChoice::kConfirmed);
}

TEST(DailiesPanelTest, TheClaimListKeepsOffTheRightBorder) {
  DailiesPanel panel;
  panel.Reset({{"Arcane Symbol: Vanishing Journey", 20},
               {"Arcane Symbol: Chu Chu Island", 20}});
  EXPECT_TRUE(RowsTouchingTheRightBorder(panel.Render()).empty());
}
}  // namespace
}  // namespace ms
