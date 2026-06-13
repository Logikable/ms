#include "src/frontend/bag_panel.h"

#include <gtest/gtest.h>

#include <string>

#include "src/frontend/panel_test_base.h"
#include "src/frontend/types.h"
#include "src/protos/equip.pb.h"

namespace ms {
namespace {

class BagPanelTest : public PanelTest {};

TEST_F(BagPanelTest, ShowsEmptyWhenBagIsEmpty) {
  BagPanel panel(c_, panel_focus_);
  EXPECT_NE(RenderComponent(panel.MakeComponent([]() {})).find("(empty)"),
            std::string::npos);
}

TEST_F(BagPanelTest, ShowsItemName) {
  c_.PickUp(sword_);
  BagPanel panel(c_, panel_focus_);
  EXPECT_NE(RenderComponent(panel.MakeComponent([]() {})).find("Sword"),
            std::string::npos);
}

TEST_F(BagPanelTest, ShowsSelectionCursorByDefault) {
  c_.PickUp(sword_);
  BagPanel panel(c_, panel_focus_);
  EXPECT_NE(RenderComponent(panel.MakeComponent([]() {})).find("> Sword"),
            std::string::npos);
}

TEST_F(BagPanelTest, ShowsColumnHeader) {
  c_.PickUp(sword_);
  BagPanel panel(c_, panel_focus_);
  std::string rendered = RenderComponent(panel.MakeComponent([]() {}));
  EXPECT_NE(rendered.find("Name"), std::string::npos);
  EXPECT_NE(rendered.find("Equip Slot"), std::string::npos);
  EXPECT_NE(rendered.find("Scrolls"), std::string::npos);
}

TEST_F(BagPanelTest, ShowsEquipSlotName) {
  c_.PickUp(sword_);
  BagPanel panel(c_, panel_focus_);
  EXPECT_NE(RenderComponent(panel.MakeComponent([]() {})).find("Weapon"),
            std::string::npos);
}

TEST_F(BagPanelTest, ShowsSlotsRemaining) {
  sword_.set_upgrade_slots(7);
  c_.PickUp(sword_);
  BagPanel panel(c_, panel_focus_);
  EXPECT_NE(RenderComponent(panel.MakeComponent([]() {})).find("7 slots"),
            std::string::npos);
}

TEST_F(BagPanelTest, ShowsItemLevel) {
  c_.PickUp(sword_);
  BagPanel panel(c_, panel_focus_);
  EXPECT_NE(RenderComponent(panel.MakeComponent([]() {})).find("Lv10"),
            std::string::npos);
}

TEST_F(BagPanelTest, ShowsWarriorJobCategory) {
  c_.PickUp(sword_);  // sword_ has EQUIP_JOB_CATEGORY_WARRIOR
  BagPanel panel(c_, panel_focus_);
  EXPECT_NE(RenderComponent(panel.MakeComponent([]() {})).find("Warrior"),
            std::string::npos);
}

TEST_F(BagPanelTest, ShowsAllForUniversalItem) {
  EquipPrototype axe;
  axe.set_name("Axe");
  axe.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  axe.add_equip_job_categories(EQUIP_JOB_CATEGORY_UNIVERSAL);
  c_.PickUp(axe);
  BagPanel panel(c_, panel_focus_);
  EXPECT_NE(RenderComponent(panel.MakeComponent([]() {})).find("All"),
            std::string::npos);
}

TEST_F(BagPanelTest, TraceMenuDisablesAllExceptInspect) {
  // Trigger a star force destroy to place a trace in inventory.
  EquipPrototype proto;
  proto.set_name("Sword");
  proto.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  proto.set_required_level(138);
  Equip state;
  state.set_stars(19);
  c_.PickUp(proto, state);
  bool saw_destroy = false;
  for (int i = 0; i < 100 && !saw_destroy; ++i) {
    if (c_.StarForceInventory(0) == kStarForceDestroy) {
      saw_destroy = true;
    }
  }
  ASSERT_TRUE(saw_destroy);

  BagPanel panel(c_, panel_focus_);
  panel.OpenMenu();
  // Equip/Scroll/StarForce are disabled; only Inspect is selectable.
  EXPECT_EQ(panel.menu().selected(), kMenuInspect);
}

}  // namespace
}  // namespace ms
