#include "src/frontend/inventory_panel.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "src/equip_instance.h"
#include "src/frontend/panel_test_base.h"
#include "src/frontend/types.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"

namespace ms {
namespace {

class InventoryPanelTest : public PanelTest {
 protected:
  ItemPrototype MakeStackable(const std::string& name, ItemCategory category) {
    ItemPrototype proto;
    proto.set_name(name);
    proto.set_category(category);
    return proto;
  }
};

TEST_F(InventoryPanelTest, ShowsEmptyWhenBagIsEmpty) {
  InventoryPanel panel(c_, panel_focus_);
  EXPECT_NE(RenderComponent(panel.MakeComponent([]() {})).find("(empty)"),
            std::string::npos);
}

TEST_F(InventoryPanelTest, ShowsItemName) {
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  InventoryPanel panel(c_, panel_focus_);
  EXPECT_NE(RenderComponent(panel.MakeComponent([]() {})).find("Sword"),
            std::string::npos);
}

TEST_F(InventoryPanelTest, ShowsSelectionCursorByDefault) {
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  InventoryPanel panel(c_, panel_focus_);
  EXPECT_NE(RenderComponent(panel.MakeComponent([]() {})).find("> Sword"),
            std::string::npos);
}

TEST_F(InventoryPanelTest, ShowsColumnHeader) {
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  InventoryPanel panel(c_, panel_focus_);
  std::string rendered = RenderComponent(panel.MakeComponent([]() {}));
  EXPECT_NE(rendered.find("Name"), std::string::npos);
  EXPECT_NE(rendered.find("Equip Slot"), std::string::npos);
  EXPECT_NE(rendered.find("Scrolls"), std::string::npos);
}

TEST_F(InventoryPanelTest, ShowsEquipSlotName) {
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  InventoryPanel panel(c_, panel_focus_);
  EXPECT_NE(RenderComponent(panel.MakeComponent([]() {})).find("Weapon"),
            std::string::npos);
}

TEST_F(InventoryPanelTest, ShowsSlotsRemaining) {
  sword_.set_upgrade_slots(7);
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  InventoryPanel panel(c_, panel_focus_);
  // Fresh item: 0 pass, 7 left, 0 restores.
  EXPECT_NE(RenderComponent(panel.MakeComponent([]() {})).find("0/7/0"),
            std::string::npos);
}

TEST_F(InventoryPanelTest, ShowsItemLevel) {
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  InventoryPanel panel(c_, panel_focus_);
  EXPECT_NE(RenderComponent(panel.MakeComponent([]() {})).find("Lv10"),
            std::string::npos);
}

TEST_F(InventoryPanelTest, ShowsWarriorJobCategory) {
  c_.PickUp(std::make_unique<EquipInstance>(
      sword_));  // sword_ has EQUIP_JOB_CATEGORY_WARRIOR
  InventoryPanel panel(c_, panel_focus_);
  EXPECT_NE(RenderComponent(panel.MakeComponent([]() {})).find("Warrior"),
            std::string::npos);
}

TEST_F(InventoryPanelTest, ShowsAllForUniversalItem) {
  EquipPrototype axe;
  axe.set_name("Axe");
  axe.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  axe.add_equip_job_categories(EQUIP_JOB_CATEGORY_UNIVERSAL);
  c_.PickUp(std::make_unique<EquipInstance>(axe));
  InventoryPanel panel(c_, panel_focus_);
  EXPECT_NE(RenderComponent(panel.MakeComponent([]() {})).find("All"),
            std::string::npos);
}

TEST_F(InventoryPanelTest, TraceMenuDisablesAllExceptInspect) {
  // Trigger a star force destroy to place a trace in inventory.
  EquipPrototype proto;
  proto.set_name("Sword");
  proto.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  proto.set_required_level(138);
  Equip state;
  state.set_stars(19);
  c_.PickUp(std::make_unique<EquipInstance>(proto, state));
  bool saw_destroy = false;
  for (int i = 0; i < 100 && !saw_destroy; ++i) {
    if (c_.StarForceInventory(0) == kStarForceDestroy) {
      saw_destroy = true;
    }
  }
  ASSERT_TRUE(saw_destroy);

  InventoryPanel panel(c_, panel_focus_);
  panel.OpenMenu();
  // Equip/Scroll/StarForce are disabled; only Inspect is selectable.
  EXPECT_EQ(panel.menu().selected(), kMenuInspect);
}

TEST_F(InventoryPanelTest, ShowsTabBar) {
  InventoryPanel panel(c_, panel_focus_);
  std::string rendered = RenderComponent(panel.MakeComponent([]() {}));
  EXPECT_NE(rendered.find("Equip"), std::string::npos);
  EXPECT_NE(rendered.find("Use"), std::string::npos);
  EXPECT_NE(rendered.find("Etc"), std::string::npos);
}

TEST_F(InventoryPanelTest, UseTabListsUseStacksWithQuantity) {
  c_.AddStackable(MakeStackable("Red Potion", ITEM_CATEGORY_USE), 5);
  InventoryPanel panel(c_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  comp->OnEvent(ftxui::Event::ArrowRight);  // Equip -> Use
  std::string rendered = RenderComponent(comp);
  EXPECT_NE(rendered.find("Quantity"), std::string::npos);
  EXPECT_NE(rendered.find("Red Potion"), std::string::npos);
}

TEST_F(InventoryPanelTest, EtcTabShowsOnlyEtcStacks) {
  c_.AddStackable(MakeStackable("Red Potion", ITEM_CATEGORY_USE), 5);
  c_.AddStackable(MakeStackable("Snail Shell", ITEM_CATEGORY_ETC), 3);
  InventoryPanel panel(c_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  comp->OnEvent(ftxui::Event::ArrowRight);  // Equip -> Use
  comp->OnEvent(ftxui::Event::ArrowRight);  // Use -> Etc
  std::string rendered = RenderComponent(comp);
  EXPECT_NE(rendered.find("Snail Shell"), std::string::npos);
  EXPECT_EQ(rendered.find("Red Potion"), std::string::npos);
}

TEST_F(InventoryPanelTest, EmptyUseTabShowsPlaceholder) {
  InventoryPanel panel(c_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  comp->OnEvent(ftxui::Event::ArrowRight);  // Equip -> Use
  EXPECT_NE(RenderComponent(comp).find("(empty)"), std::string::npos);
}

}  // namespace
}  // namespace ms
