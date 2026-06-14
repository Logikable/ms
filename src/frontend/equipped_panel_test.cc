#include "src/frontend/equipped_panel.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "src/equip_instance.h"
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
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  c_.Equip(0);
  EquippedPanel panel(c_, panel_focus_);
  EXPECT_NE(RenderComponent(panel.MakeComponent([]() {})).find("Sword"),
            std::string::npos);
}

TEST_F(EquippedPanelTest, ShowsSelectionCursorByDefault) {
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  c_.Equip(0);
  EquippedPanel panel(c_, panel_focus_);
  EXPECT_NE(RenderComponent(panel.MakeComponent([]() {})).find("> Sword"),
            std::string::npos);
}

TEST_F(EquippedPanelTest, ShowsColumnHeader) {
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  c_.Equip(0);
  EquippedPanel panel(c_, panel_focus_);
  std::string rendered = RenderComponent(panel.MakeComponent([]() {}));
  EXPECT_NE(rendered.find("Name"), std::string::npos);
  EXPECT_NE(rendered.find("Equip Slot"), std::string::npos);
  EXPECT_NE(rendered.find("Scrolls"), std::string::npos);
}

TEST_F(EquippedPanelTest, ShowsEquipSlotName) {
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  c_.Equip(0);
  EquippedPanel panel(c_, panel_focus_);
  EXPECT_NE(RenderComponent(panel.MakeComponent([]() {})).find("Weapon"),
            std::string::npos);
}

TEST_F(EquippedPanelTest, SelectedSlotReturnsEquippedSlot) {
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  c_.Equip(0);
  EquippedPanel panel(c_, panel_focus_);
  RenderComponent(panel.MakeComponent([]() {}));
  EXPECT_EQ(panel.selected_slot(), EQUIP_SLOT_PRIMARY_WEAPON);
}

TEST_F(EquippedPanelTest, SelectedSlotReturnsUnspecifiedWhenEmpty) {
  EquippedPanel panel(c_, panel_focus_);
  EXPECT_EQ(panel.selected_slot(), EQUIP_SLOT_UNSPECIFIED);
}

}  // namespace
}  // namespace ms
