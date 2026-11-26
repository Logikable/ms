#include "src/frontend/screens/hexa/hexa_stat_activate_panel.h"

#include <gtest/gtest.h>

#include <string>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/frontend/testing/screen_text.h"
#include "src/protos/character.pb.h"

namespace ms {
namespace {

using Result = HexaStatActivatePanel::Result;

class HexaStatActivatePanelTest : public testing::Test {
 protected:
  void SetUp() override {
    HexaStatNode& node = *stats_.add_nodes();
    node.set_primary(HEXA_STAT_BOSS_DAMAGE);
    node.set_first(HEXA_STAT_ATTACK);
    node.set_second(HEXA_STAT_DAMAGE);
    HexaStatNode& second = *stats_.add_nodes();
    second.set_primary(HEXA_STAT_CRIT_DAMAGE);
    second.set_first(HEXA_STAT_ATTACK);
    second.set_second(HEXA_STAT_MAIN_STAT);
    panel_.Open(stats_, STAT_FIELD_LUK);
  }

  Result Press(const ftxui::Event& event, int times = 1) {
    Result result = Result::kPending;
    for (int i = 0; i < times; ++i) {
      result = panel_.OnEvent(event);
    }
    return result;
  }

  ftxui::Screen Draw() {
    ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(60),
                                                 ftxui::Dimension::Fixed(20));
    ftxui::Render(screen, ftxui::dbox({panel_.Render(), ftxui::filler()}));
    // A second pass with the dropdown, which reads where the row was drawn.
    // Cleared first: inverted toggles, so two passes would cancel it out.
    screen.Clear();
    ftxui::Render(screen,
                  ftxui::dbox({panel_.Render(), panel_.RenderDropdown()}));
    return screen;
  }

  HexaStats stats_;
  HexaStatActivatePanel panel_;
};

TEST_F(HexaStatActivatePanelTest, RowsStartUnselectedAndActivateWaits) {
  ftxui::Screen screen = Draw();
  const std::string text = ScreenText(screen);
  EXPECT_NE(text.find("Activate HEXA Stat"), std::string::npos);
  EXPECT_NE(text.find("Pick stats:"), std::string::npos);
  EXPECT_NE(text.find("M: Unselected"), std::string::npos);
  EXPECT_TRUE(PixelOf(screen, "[Activate]").dim);
  EXPECT_EQ(Press(ftxui::Event::ArrowDown, 3), Result::kPending);
  EXPECT_EQ(Press(ftxui::Event::Return), Result::kPending)
      << "nothing picked yet";
}

TEST_F(HexaStatActivatePanelTest, TheMainStatDropdownGreysPrimariesTaken) {
  Press(ftxui::Event::Return);
  ASSERT_TRUE(panel_.dropdown_open());
  ftxui::Screen screen = Draw();
  EXPECT_TRUE(PixelOf(screen, "Crit Damage").dim);
  EXPECT_TRUE(PixelOf(screen, "Boss Damage").dim);
  EXPECT_FALSE(PixelOf(screen, "Ignore DEF").dim);
  EXPECT_FALSE(PixelOf(screen, "ATT ").dim) << "the cap is on additionals";
  EXPECT_NE(FindOnScreen(screen, "> Ignore DEF").y, -1)
      << "on the first stat it allows";
  Press(ftxui::Event::Return);
  EXPECT_FALSE(panel_.dropdown_open());
  EXPECT_EQ(panel_.draft().primary(), HEXA_STAT_IGNORE_DEFENSE);
}

TEST_F(HexaStatActivatePanelTest, AnAdditionalDropdownGreysTheDraftAndTheCap) {
  Press(ftxui::Event::Return);
  Press(ftxui::Event::Return);  // Ignore DEF as the main stat
  Press(ftxui::Event::ArrowDown);
  Press(ftxui::Event::Return);
  ftxui::Screen screen = Draw();
  EXPECT_FALSE(PixelOf(screen, "Crit Damage").dim)
      << "a primary elsewhere may be an additional";
  EXPECT_TRUE(PixelOf(screen, "  Ignore DEF").dim) << "this node's main stat";
  EXPECT_TRUE(PixelOf(screen, "ATT ").dim) << "on two nodes already";
}

TEST_F(HexaStatActivatePanelTest, EscapeClosesTheDropdownThenTheDialog) {
  Press(ftxui::Event::Return);
  EXPECT_EQ(Press(ftxui::Event::Escape), Result::kPending);
  EXPECT_FALSE(panel_.dropdown_open());
  EXPECT_EQ(panel_.draft().primary(), HEXA_STAT_UNSPECIFIED);
  EXPECT_EQ(Press(ftxui::Event::Escape), Result::kCancel);
}

TEST_F(HexaStatActivatePanelTest, ThreePicksActivateAndCancelLeaves) {
  Press(ftxui::Event::Return);
  Press(ftxui::Event::Return);  // M: Ignore DEF
  Press(ftxui::Event::ArrowDown);
  Press(ftxui::Event::Return);
  Press(ftxui::Event::Return);  // A: Crit Damage
  Press(ftxui::Event::ArrowDown);
  Press(ftxui::Event::Return);
  Press(ftxui::Event::Return);  // A: Boss Damage, the first left
  EXPECT_TRUE(panel_.Complete());
  EXPECT_EQ(panel_.draft().first(), HEXA_STAT_CRIT_DAMAGE);
  EXPECT_EQ(panel_.draft().second(), HEXA_STAT_BOSS_DAMAGE);
  Press(ftxui::Event::ArrowDown);
  ftxui::Screen screen = Draw();
  EXPECT_FALSE(PixelOf(screen, "[Activate]").dim);
  EXPECT_TRUE(PixelOf(screen, "[Activate]").inverted);

  Press(ftxui::Event::ArrowRight);
  EXPECT_EQ(Press(ftxui::Event::Return), Result::kCancel);
  Press(ftxui::Event::ArrowLeft);
  EXPECT_EQ(Press(ftxui::Event::Return), Result::kActivate);
}

TEST_F(HexaStatActivatePanelTest, ReopeningARowStartsOnItsPick) {
  Press(ftxui::Event::Return);
  Press(ftxui::Event::ArrowDown);
  Press(ftxui::Event::Return);  // M: Damage
  ASSERT_EQ(panel_.draft().primary(), HEXA_STAT_DAMAGE);
  Press(ftxui::Event::Return);
  EXPECT_NE(FindOnScreen(Draw(), "> Damage").y, -1);
}

}  // namespace
}  // namespace ms
