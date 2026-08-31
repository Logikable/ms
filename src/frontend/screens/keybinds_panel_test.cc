#include "src/frontend/screens/keybinds_panel.h"

#include <gtest/gtest.h>

#include <string>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/frontend/keybinds.h"
#include "src/protos/keybinds.pb.h"

namespace ms {
namespace {

class KeybindsPanelTest : public testing::Test {
 protected:
  std::string Render() {
    ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(60),
                                                 ftxui::Dimension::Fixed(20));
    ftxui::Render(screen, panel_.Render());
    return screen.ToString();
  }

  Keybinds binds_;
  KeyMap keys_{&binds_};
  KeybindsPanel panel_{keys_};
};

TEST_F(KeybindsPanelTest, ListsEveryActionAndItsKeys) {
  std::string out = Render();
  EXPECT_NE(out.find("Keybinds"), std::string::npos);
  EXPECT_NE(out.find("Move Up"), std::string::npos);
  EXPECT_NE(out.find("Previous Panel"), std::string::npos);
  EXPECT_NE(out.find("Shift+Tab"), std::string::npos);
  EXPECT_NE(out.find("Close"), std::string::npos);
  EXPECT_NE(out.find("Enter to rebind"), std::string::npos);
  EXPECT_NE(out.find("Esc to unbind"), std::string::npos);
}

// The locked slot is not a stop, and neither end of the row rolls over.
TEST_F(KeybindsPanelTest, TheCursorStepsOverTheLockedSlotAndStopsAtTheEnds) {
  EXPECT_EQ(panel_.selected_slot(), 1);
  panel_.MoveSlot(-1);
  EXPECT_EQ(panel_.selected_slot(), 1);
  panel_.MoveSlot(1);
  EXPECT_EQ(panel_.selected_slot(), 2);
  panel_.MoveSlot(1);
  EXPECT_EQ(panel_.selected_slot(), 2);
}

TEST_F(KeybindsPanelTest, RowsRingThroughTheCloseButton) {
  EXPECT_EQ(panel_.selected_action(), KEY_ACTION_UP);
  EXPECT_FALSE(panel_.on_close());
  panel_.MoveRow(-1);
  EXPECT_TRUE(panel_.on_close());
  panel_.MoveRow(1);
  EXPECT_EQ(panel_.selected_action(), KEY_ACTION_UP);
  for (int i = 0; i < kKeyActionCount; ++i) {
    panel_.MoveRow(1);
  }
  EXPECT_TRUE(panel_.on_close());
  // Left and Right have nothing to move on the button.
  panel_.MoveSlot(1);
  EXPECT_TRUE(panel_.on_close());
}

TEST_F(KeybindsPanelTest, ASlotWaitingForAKeySaysSo) {
  EXPECT_EQ(Render().find("Press"), std::string::npos);
  panel_.StartCapture();
  EXPECT_TRUE(panel_.capturing());
  EXPECT_NE(Render().find("Press"), std::string::npos);
  panel_.StopCapture();
  EXPECT_EQ(Render().find("Press"), std::string::npos);
}

TEST_F(KeybindsPanelTest, ARefusalTakesTheFootersPlaceUntilTheCursorMoves) {
  panel_.ShowRefusal("Esc is reserved for Cancel.");
  std::string out = Render();
  EXPECT_NE(out.find("reserved for Cancel"), std::string::npos);
  EXPECT_EQ(out.find("Enter to rebind"), std::string::npos);
  panel_.MoveRow(1);
  EXPECT_NE(Render().find("Enter to rebind"), std::string::npos);
}

TEST_F(KeybindsPanelTest, AnEmptySlotReadsAsEmpty) {
  EXPECT_NE(Render().find("—"), std::string::npos);
  keys_.Bind(KEY_ACTION_UP, 1, ftxui::Event::w);
  EXPECT_NE(Render().find("W"), std::string::npos);
}

}  // namespace
}  // namespace ms
