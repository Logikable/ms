#include "src/frontend/bag_panel.h"

#include <gtest/gtest.h>

#include <string>

#include "src/frontend/panel_test_base.h"

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
  // Bag entries are formatted as "[ 0] Name ..."; cursor appears before index.
  EXPECT_NE(RenderComponent(panel.MakeComponent([]() {})).find("> [ 0]"),
            std::string::npos);
}

// SetShowSelection(false) hides the cursor and keeps the two-space indent so
// text does not shift left when the item menu opens.
TEST_F(BagPanelTest, SetShowSelectionFalseHidesCursorPreservesIndent) {
  c_.PickUp(sword_);
  BagPanel panel(c_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  panel.SetShowSelection(false);
  std::string rendered = RenderComponent(comp);
  EXPECT_EQ(rendered.find("> [ 0]"), std::string::npos);
  EXPECT_NE(rendered.find("  [ 0]"), std::string::npos);
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

}  // namespace
}  // namespace ms
