#include "src/frontend/stack_panel.h"

#include <gtest/gtest.h>

#include <string>

#include "src/frontend/panel_test_base.h"
#include "src/protos/item.pb.h"

namespace ms {
namespace {

class StackPanelTest : public PanelTest {
 protected:
  ItemPrototype MakeEtc(const std::string& name) {
    ItemPrototype proto;
    proto.set_name(name);
    proto.set_category(ITEM_CATEGORY_ETC);
    return proto;
  }
};

TEST_F(StackPanelTest, EmptyShowsPlaceholder) {
  StackPanel panel(c_);
  EXPECT_NE(RenderElement(panel.Render()).find("(empty)"), std::string::npos);
}

TEST_F(StackPanelTest, ShowsStackNameAndCount) {
  c_.AddStackable(MakeEtc("Green Snail Shell"), 42);
  StackPanel panel(c_);
  std::string rendered = RenderElement(panel.Render());
  EXPECT_NE(rendered.find("Green Snail Shell"), std::string::npos);
  EXPECT_NE(rendered.find("x42"), std::string::npos);
}

TEST_F(StackPanelTest, ShowsOneRowPerStackOnOverflow) {
  // Etc caps at 200 per stack, so 250 splits into a full stack plus 50.
  c_.AddStackable(MakeEtc("Green Snail Shell"), 250);
  StackPanel panel(c_);
  std::string rendered = RenderElement(panel.Render());
  EXPECT_NE(rendered.find("x200"), std::string::npos);
  EXPECT_NE(rendered.find("x50"), std::string::npos);
}

}  // namespace
}  // namespace ms
