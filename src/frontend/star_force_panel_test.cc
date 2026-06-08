#include "src/frontend/star_force_panel.h"

#include <gtest/gtest.h>

#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/equip_instance.h"
#include "src/protos/equip.pb.h"

namespace ms {
namespace {

class StarForcePanelTest : public testing::Test {
 protected:
  EquipInstance MakeItem(int required_level, int stars) {
    EquipPrototype proto;
    proto.set_name("Sword");
    proto.set_required_level(required_level);
    Equip state;
    state.set_stars(stars);
    return EquipInstance(proto, state);
  }

  static std::string Render(StarForcePanel& panel) {
    ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(40),
                                                 ftxui::Dimension::Fixed(15));
    ftxui::Render(screen, panel.Render());
    return screen.ToString();
  }
};

TEST_F(StarForcePanelTest, NullItemShowsPlaceholder) {
  StarForcePanel panel;
  EXPECT_NE(Render(panel).find("(no item)"), std::string::npos);
}

TEST_F(StarForcePanelTest, RenderShowsNameAndUpgradeArrow) {
  EquipInstance item = MakeItem(0, 0);
  StarForcePanel panel;
  panel.SetItem(&item);
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("Sword"), std::string::npos);
  EXPECT_NE(rendered.find("0"), std::string::npos);
  EXPECT_NE(rendered.find("1"), std::string::npos);
}

TEST_F(StarForcePanelTest, RenderShowsSuccessAndFailRates) {
  // At 0★: success=95%, fail=5%.
  EquipInstance item = MakeItem(0, 0);
  StarForcePanel panel;
  panel.SetItem(&item);
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("95%"), std::string::npos);
  EXPECT_NE(rendered.find("5%"), std::string::npos);
}

TEST_F(StarForcePanelTest, RenderHidesDestroyRowWhenZero) {
  EquipInstance item = MakeItem(0, 0);
  StarForcePanel panel;
  panel.SetItem(&item);
  EXPECT_EQ(Render(panel).find("Destroy"), std::string::npos);
}

TEST_F(StarForcePanelTest, RenderShowsDestroyRowWhenNonZero) {
  // At 15★: destroy=2.1%.
  EquipInstance item = MakeItem(/*required_level=*/138, /*stars=*/15);
  StarForcePanel panel;
  panel.SetItem(&item);
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("Destroy"), std::string::npos);
  EXPECT_NE(rendered.find("2.1%"), std::string::npos);
}

TEST_F(StarForcePanelTest, RenderFormatsSubPercentRateCorrectly) {
  // At 21★: destroy=12.75%.
  EquipInstance item = MakeItem(/*required_level=*/138, /*stars=*/21);
  StarForcePanel panel;
  panel.SetItem(&item);
  EXPECT_NE(Render(panel).find("12.75%"), std::string::npos);
}

}  // namespace
}  // namespace ms
