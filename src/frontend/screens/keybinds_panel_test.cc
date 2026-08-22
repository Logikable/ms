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

std::string Render(const KeybindsPanel& panel) {
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(60),
                                               ftxui::Dimension::Fixed(20));
  ftxui::Render(screen, panel.Render());
  return screen.ToString();
}

TEST(KeybindsPanelTest, ListsEveryActionAndItsKeys) {
  Keybinds binds;
  KeyMap keys(&binds);
  KeybindsPanel panel(keys);
  std::string out = Render(panel);
  EXPECT_NE(out.find("Keybinds"), std::string::npos);
  EXPECT_NE(out.find("Move Up"), std::string::npos);
  EXPECT_NE(out.find("Previous Panel"), std::string::npos);
  EXPECT_NE(out.find("Shift+Tab"), std::string::npos);
  EXPECT_NE(out.find("Close"), std::string::npos);
  EXPECT_NE(out.find("Enter to rebind"), std::string::npos);
  EXPECT_NE(out.find("Esc to unbind"), std::string::npos);
}

// The locked slot is not a stop, and neither end of the row rolls over.
TEST(KeybindsPanelTest, TheCursorStepsOverTheLockedSlotAndStopsAtTheEnds) {
  Keybinds binds;
  KeyMap keys(&binds);
  KeybindsPanel panel(keys);
  EXPECT_EQ(panel.selected_slot(), 1);
  panel.MoveSlot(-1);
  EXPECT_EQ(panel.selected_slot(), 1);
  panel.MoveSlot(1);
  EXPECT_EQ(panel.selected_slot(), 2);
  panel.MoveSlot(1);
  EXPECT_EQ(panel.selected_slot(), 2);
}

TEST(KeybindsPanelTest, RowsRingThroughTheCloseButton) {
  Keybinds binds;
  KeyMap keys(&binds);
  KeybindsPanel panel(keys);
  EXPECT_EQ(panel.selected_action(), KEY_ACTION_UP);
  EXPECT_FALSE(panel.on_close());
  panel.MoveRow(-1);
  EXPECT_TRUE(panel.on_close());
  panel.MoveRow(1);
  EXPECT_EQ(panel.selected_action(), KEY_ACTION_UP);
  for (int i = 0; i < kKeyActionCount; ++i) {
    panel.MoveRow(1);
  }
  EXPECT_TRUE(panel.on_close());
  // Left and Right have nothing to move on the button.
  panel.MoveSlot(1);
  EXPECT_TRUE(panel.on_close());
}

TEST(KeybindsPanelTest, ASlotWaitingForAKeySaysSo) {
  Keybinds binds;
  KeyMap keys(&binds);
  KeybindsPanel panel(keys);
  EXPECT_EQ(Render(panel).find("Press"), std::string::npos);
  panel.StartCapture();
  EXPECT_TRUE(panel.capturing());
  EXPECT_NE(Render(panel).find("Press"), std::string::npos);
  panel.StopCapture();
  EXPECT_EQ(Render(panel).find("Press"), std::string::npos);
}

TEST(KeybindsPanelTest, ARefusalTakesTheFootersPlaceUntilTheCursorMoves) {
  Keybinds binds;
  KeyMap keys(&binds);
  KeybindsPanel panel(keys);
  panel.ShowRefusal("Esc is reserved for Cancel.");
  std::string out = Render(panel);
  EXPECT_NE(out.find("reserved for Cancel"), std::string::npos);
  EXPECT_EQ(out.find("Enter to rebind"), std::string::npos);
  panel.MoveRow(1);
  EXPECT_NE(Render(panel).find("Enter to rebind"), std::string::npos);
}

TEST(KeybindsPanelTest, AnEmptySlotReadsAsEmpty) {
  Keybinds binds;
  KeyMap keys(&binds);
  KeybindsPanel panel(keys);
  EXPECT_NE(Render(panel).find("—"), std::string::npos);
  keys.Bind(KEY_ACTION_UP, 1, ftxui::Event::w);
  EXPECT_NE(Render(panel).find("W"), std::string::npos);
}

}  // namespace
}  // namespace ms
