#include "src/frontend/screens/options_panel.h"

#include <gtest/gtest.h>

#include <string>

#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/account.h"

namespace ms {
namespace {

class OptionsPanelTest : public testing::Test {
 protected:
  std::string Render() {
    ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(40),
                                                 ftxui::Dimension::Fixed(14));
    ftxui::Render(screen, panel_.Render());
    return screen.ToString();
  }

  AccountInstance account_;
  OptionsPanel panel_{account_};
};

TEST_F(OptionsPanelTest, ShowsHeadersTheOptionAndTheCloseButton) {
  std::string out = Render();
  EXPECT_NE(out.find("Options"), std::string::npos);
  EXPECT_NE(out.find("Option"), std::string::npos);
  EXPECT_NE(out.find("State"), std::string::npos);
  EXPECT_NE(out.find("Panel Title Blink"), std::string::npos);
  EXPECT_NE(out.find("Close"), std::string::npos);
}

TEST_F(OptionsPanelTest, BlinkShipsOffAndTheBoxSaysSo) {
  EXPECT_FALSE(account_.panel_title_blink());
  EXPECT_NE(Render().find("[ ]"), std::string::npos);
}

TEST_F(OptionsPanelTest, EnterOnTheRowTogglesTheAccountSetting) {
  panel_.Toggle();
  EXPECT_TRUE(account_.panel_title_blink());
  EXPECT_NE(Render().find("[✓]"), std::string::npos);
  panel_.Toggle();
  EXPECT_FALSE(account_.panel_title_blink());
  EXPECT_NE(Render().find("[ ]"), std::string::npos);
}

TEST_F(OptionsPanelTest, CursorWrapsThroughCloseAndBackToTheOption) {
  EXPECT_FALSE(panel_.on_close());
  EXPECT_EQ(panel_.selected_option(), Option::kPanelTitleBlink);
  panel_.MoveRow(1);
  EXPECT_TRUE(panel_.on_close());
  panel_.MoveRow(1);
  EXPECT_FALSE(panel_.on_close());
  // Up from the first option comes out on Close, the far end of the ring.
  panel_.MoveRow(-1);
  EXPECT_TRUE(panel_.on_close());
}

TEST_F(OptionsPanelTest, CloseTogglesNothing) {
  panel_.MoveRow(1);
  panel_.Toggle();
  EXPECT_FALSE(account_.panel_title_blink());
}

TEST_F(OptionsPanelTest, ResetPutsTheCursorBackOnTheFirstOption) {
  panel_.MoveRow(1);
  panel_.Reset();
  EXPECT_FALSE(panel_.on_close());
}

// The list is drawn to a fixed height, so an option arriving later does not
// change the size of the panel the player has learned.
TEST_F(OptionsPanelTest, LeavesRoomForOptionsStillToCome) {
  ftxui::Element card = panel_.Render();
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fit(card));
  ftxui::Render(screen, card);
  // Two borders, the header and its rule, five list rows, the rule above the
  // foot and the Close button.
  EXPECT_EQ(screen.dimy(), 11);
}

}  // namespace
}  // namespace ms
