#include "src/frontend/equipped_panel.h"

#include <gtest/gtest.h>

#include <string>

#include "src/frontend/panel_test_base.h"

namespace ms {
namespace {

class EquippedPanelTest : public PanelTest {};

TEST_F(EquippedPanelTest, ShowsEmptyWhenNothingEquipped) {
  EquippedPanel panel(c_, panel_focus_);
  EXPECT_NE(RenderComponent(panel.MakeComponent([]() {})).find("(empty)"),
            std::string::npos);
}

TEST_F(EquippedPanelTest, ShowsEquippedItemName) {
  c_.PickUp(sword_);
  c_.Equip(0);
  EquippedPanel panel(c_, panel_focus_);
  EXPECT_NE(RenderComponent(panel.MakeComponent([]() {})).find("Sword"),
            std::string::npos);
}

TEST_F(EquippedPanelTest, ShowsSelectionCursorByDefault) {
  c_.PickUp(sword_);
  c_.Equip(0);
  EquippedPanel panel(c_, panel_focus_);
  EXPECT_NE(RenderComponent(panel.MakeComponent([]() {})).find("> Sword"),
            std::string::npos);
}

// SetShowSelection(false) hides the cursor and keeps the two-space indent so
// text does not shift left when the item menu opens.
TEST_F(EquippedPanelTest, SetShowSelectionFalseHidesCursorPreservesIndent) {
  c_.PickUp(sword_);
  c_.Equip(0);
  EquippedPanel panel(c_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  panel.SetShowSelection(false);
  std::string rendered = RenderComponent(comp);
  EXPECT_EQ(rendered.find("> Sword"), std::string::npos);
  EXPECT_NE(rendered.find("  Sword"), std::string::npos);
}

}  // namespace
}  // namespace ms
