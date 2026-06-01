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

TEST_F(BagPanelTest, ShowsItemLevel) {
  c_.PickUp(sword_);
  BagPanel panel(c_, panel_focus_);
  EXPECT_NE(RenderComponent(panel.MakeComponent([]() {})).find("Lv10"),
            std::string::npos);
}

}  // namespace
}  // namespace ms
