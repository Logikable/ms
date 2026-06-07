#include "src/frontend/inspect_panel.h"

#include <gtest/gtest.h>

#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/frontend/panel_test_base.h"
#include "src/protos/equip.pb.h"

namespace ms {
namespace {

class InspectPanelTest : public PanelTest {
 protected:
  static std::string Render(InspectPanel& panel) {
    ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80),
                                                 ftxui::Dimension::Fixed(20));
    ftxui::Render(screen, panel.Render());
    return screen.ToString();
  }
};

TEST_F(InspectPanelTest, NullItemShowsPlaceholder) {
  InspectPanel panel;
  EXPECT_NE(Render(panel).find("(none)"), std::string::npos);
}

TEST_F(InspectPanelTest, ShowsItemName) {
  sword_.set_name("Iron Sword");
  EquipInstance item(sword_);
  InspectPanel panel;
  panel.SetItem(&item);
  EXPECT_NE(Render(panel).find("Iron Sword"), std::string::npos);
}

TEST_F(InspectPanelTest, ShowsLevel) {
  sword_.set_required_level(30);
  EquipInstance item(sword_);
  InspectPanel panel;
  panel.SetItem(&item);
  EXPECT_NE(Render(panel).find("Lv 30"), std::string::npos);
}

TEST_F(InspectPanelTest, ShowsJobCategory) {
  EquipInstance item(sword_);
  InspectPanel panel;
  panel.SetItem(&item);
  EXPECT_NE(Render(panel).find("Warrior"), std::string::npos);
}

TEST_F(InspectPanelTest, ShowsBaseStat) {
  sword_.mutable_base_stats()->set_attack(7);
  EquipInstance item(sword_);
  InspectPanel panel;
  panel.SetItem(&item);
  // Total equals base when no scrolls applied: +7 (+7+0)
  EXPECT_NE(Render(panel).find("+7 (7 +0)"), std::string::npos);
}

TEST_F(InspectPanelTest, ShowsScrollStatBreakdown) {
  sword_.mutable_base_stats()->set_attack(5);
  Equip state;
  state.set_remaining_upgrade_slots(3);
  state.mutable_scroll_stats()->set_attack(3);
  EquipInstance item(sword_, state);
  InspectPanel panel;
  panel.SetItem(&item);
  EXPECT_NE(Render(panel).find("+8 (5 +3)"), std::string::npos);
}

TEST_F(InspectPanelTest, ShowsSlotsRemaining) {
  sword_.set_upgrade_slots(7);
  Equip state;
  state.set_remaining_upgrade_slots(4);
  EquipInstance item(sword_, state);
  InspectPanel panel;
  panel.SetItem(&item);
  EXPECT_NE(Render(panel).find("4 upgrade slots remaining"), std::string::npos);
}

TEST_F(InspectPanelTest, ZeroStatRowNotShown) {
  // DEF is 0 on sword_ — should not appear in output.
  EquipInstance item(sword_);
  InspectPanel panel;
  panel.SetItem(&item);
  EXPECT_EQ(Render(panel).find("DEF"), std::string::npos);
}

TEST_F(InspectPanelTest, NoStatsShowsNoStatsText) {
  // sword_ has no base_stats set; render should degrade gracefully.
  EquipInstance item(sword_);
  InspectPanel panel;
  panel.SetItem(&item);
  EXPECT_NE(Render(panel).find("(no stats)"), std::string::npos);
}

}  // namespace
}  // namespace ms
