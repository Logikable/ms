#include "src/frontend/screens/options_panel.h"

#include <gtest/gtest.h>

#include <string>

#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/account.h"
#include "src/build_config.h"

namespace ms {
namespace {

class OptionsPanelTest : public testing::Test {
 protected:
  std::string Render() {
    ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(60),
                                                 ftxui::Dimension::Fixed(14));
    ftxui::Render(screen, panel_.Render());
    return screen.ToString();
  }

  // Puts the cursor on `option`, which is its own row number.
  void SelectOption(Option option) {
    panel_.Reset();
    panel_.MoveRow(static_cast<int>(option));
  }

  AccountInstance account_;
  OptionsPanel panel_{account_};
};

TEST_F(OptionsPanelTest, ListsTheSettingsAndTheCloseButton) {
  std::string out = Render();
  EXPECT_NE(out.find("Options"), std::string::npos);
  EXPECT_NE(out.find("Panel Title Blink"), std::string::npos);
  EXPECT_NE(out.find("Close"), std::string::npos);
  // The Option/State header is gone; the names alone say what the rows are.
  EXPECT_EQ(out.find("State"), std::string::npos);
}

TEST_F(OptionsPanelTest, BlinkShipsOffAndEnterThrowsIt) {
  EXPECT_FALSE(account_.panel_title_blink());
  EXPECT_NE(Render().find("[ ]"), std::string::npos);
  panel_.Toggle();
  EXPECT_TRUE(account_.panel_title_blink());
  EXPECT_NE(Render().find("[✓]"), std::string::npos);
  panel_.Toggle();
  EXPECT_FALSE(account_.panel_title_blink());
}

TEST_F(OptionsPanelTest, CursorWrapsThroughCloseAndBack) {
  EXPECT_FALSE(panel_.on_close());
  EXPECT_EQ(panel_.selected_option(), Option::kPanelTitleBlink);
  for (int i = 0; i < kOptionCount; ++i) {
    panel_.MoveRow(1);
  }
  EXPECT_TRUE(panel_.on_close());
  panel_.MoveRow(1);
  EXPECT_FALSE(panel_.on_close());
  // Up from the first setting comes out on Close, the far end of the ring.
  panel_.MoveRow(-1);
  EXPECT_TRUE(panel_.on_close());
}

TEST_F(OptionsPanelTest, CloseChangesNothing) {
  panel_.Reset();
  panel_.MoveRow(-1);
  ASSERT_TRUE(panel_.on_close());
  panel_.Toggle();
  panel_.Adjust(1);
  EXPECT_FALSE(account_.panel_title_blink());
  EXPECT_EQ(account_.map_bgm_volume(), kDefaultBgmVolume);
}

TEST_F(OptionsPanelTest, ResetPutsTheCursorBackOnTheFirstSetting) {
  panel_.MoveRow(1);
  panel_.Reset();
  EXPECT_FALSE(panel_.on_close());
  EXPECT_EQ(panel_.selected_option(), Option::kPanelTitleBlink);
}

// The list is drawn to a fixed height, so a setting arriving later does not
// change the size of the panel the player has learned.
TEST_F(OptionsPanelTest, LeavesRoomForSettingsStillToCome) {
  ftxui::Element card = panel_.Render();
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fit(card));
  ftxui::Render(screen, card);
  // Two borders, five list rows, the rule above the foot and Close.
  EXPECT_EQ(screen.dimy(), 9);
}

class OptionsVolumeTest : public OptionsPanelTest {
 protected:
  void SetUp() override {
    if (!kAudioEnabled) {
      GTEST_SKIP() << "built with --define=audio=off";
    }
  }
};

TEST_F(OptionsVolumeTest, BothVolumesShowAtTen) {
  std::string out = Render();
  EXPECT_NE(out.find("Map BGM Volume"), std::string::npos);
  EXPECT_NE(out.find("Boss BGM Volume"), std::string::npos);
  EXPECT_EQ(account_.map_bgm_volume(), kDefaultBgmVolume);
  EXPECT_EQ(account_.boss_bgm_volume(), kDefaultBgmVolume);
}

TEST_F(OptionsVolumeTest, ArrowsMoveOnlyTheVolumeSelected) {
  SelectOption(Option::kMapBgmVolume);
  panel_.Adjust(5);
  EXPECT_EQ(account_.map_bgm_volume(), kDefaultBgmVolume + 5);
  EXPECT_EQ(account_.boss_bgm_volume(), kDefaultBgmVolume);

  SelectOption(Option::kBossBgmVolume);
  panel_.Adjust(-4);
  EXPECT_EQ(account_.boss_bgm_volume(), kDefaultBgmVolume - 4);
  EXPECT_EQ(account_.map_bgm_volume(), kDefaultBgmVolume + 5);
}

// A held key runs into the end of the scale and stays there.
TEST_F(OptionsVolumeTest, VolumeStopsAtBothEnds) {
  SelectOption(Option::kMapBgmVolume);
  for (int i = 0; i < kMaxBgmVolume + 20; ++i) {
    panel_.Adjust(1);
  }
  EXPECT_EQ(account_.map_bgm_volume(), kMaxBgmVolume);
  EXPECT_NE(Render().find("100"), std::string::npos);
  for (int i = 0; i < kMaxBgmVolume + 20; ++i) {
    panel_.Adjust(-1);
  }
  EXPECT_EQ(account_.map_bgm_volume(), 0);
}

TEST_F(OptionsVolumeTest, EnterOnAVolumeThrowsNoSwitch) {
  SelectOption(Option::kMapBgmVolume);
  panel_.Toggle();
  EXPECT_FALSE(account_.panel_title_blink());
  EXPECT_EQ(account_.map_bgm_volume(), kDefaultBgmVolume);
}

}  // namespace
}  // namespace ms
