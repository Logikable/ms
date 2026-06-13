#include "src/frontend/inspect_panel.h"

#include <gtest/gtest.h>

#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/frontend/panel_test_base.h"
#include "src/item.h"
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
  EXPECT_NE(Render(panel).find("Req Lev: 30"), std::string::npos);
}

TEST_F(InspectPanelTest, ShowsJobCategory) {
  EquipInstance item(sword_);
  InspectPanel panel;
  panel.SetItem(&item);
  EXPECT_NE(Render(panel).find("Warrior"), std::string::npos);
}

TEST_F(InspectPanelTest, IneligibleJobsStillRendered) {
  EquipInstance item(sword_);
  InspectPanel panel;
  panel.SetItem(&item);
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("Bowman"), std::string::npos);
  EXPECT_NE(rendered.find("Magician"), std::string::npos);
  EXPECT_NE(rendered.find("Pirate"), std::string::npos);
}

TEST_F(InspectPanelTest, UniversalShowsAllJobGroups) {
  sword_.clear_equip_job_categories();
  sword_.add_equip_job_categories(EQUIP_JOB_CATEGORY_UNIVERSAL);
  EquipInstance item(sword_);
  InspectPanel panel;
  panel.SetItem(&item);
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("Beginner"), std::string::npos);
  EXPECT_NE(rendered.find("Warrior"), std::string::npos);
  EXPECT_NE(rendered.find("Bowman"), std::string::npos);
  EXPECT_NE(rendered.find("Magician"), std::string::npos);
  EXPECT_NE(rendered.find("Thief"), std::string::npos);
  EXPECT_NE(rendered.find("Pirate"), std::string::npos);
}

TEST_F(InspectPanelTest, ShowsEquipType) {
  sword_.set_equip_type(EQUIP_TYPE_ONE_HANDED_SWORD);
  EquipInstance item(sword_);
  InspectPanel panel;
  panel.SetItem(&item);
  EXPECT_NE(Render(panel).find("Type: One-Handed Sword"), std::string::npos);
}

TEST_F(InspectPanelTest, ShowsAttackSpeedStage) {
  sword_.set_attack_speed(ATTACK_SPEED_AVERAGE);
  EquipInstance item(sword_);
  InspectPanel panel;
  panel.SetItem(&item);
  EXPECT_NE(Render(panel).find("Attack Speed: Stage 4 (Average)"),
            std::string::npos);
}

TEST_F(InspectPanelTest, ShowsBaseStat) {
  sword_.mutable_base_stats()->set_attack(7);
  EquipInstance item(sword_);
  InspectPanel panel;
  panel.SetItem(&item);
  EXPECT_NE(Render(panel).find("+7 (7 +0)"), std::string::npos);
}

TEST_F(InspectPanelTest, ShowsScrollStatBreakdown) {
  sword_.mutable_base_stats()->set_attack(5);
  Equip state;
  state.set_equip_name("Sword");
  state.set_remaining_upgrade_slots(3);
  state.mutable_scroll_stats()->set_attack(3);
  EquipInstance item(sword_, state);
  InspectPanel panel;
  panel.SetItem(&item);
  EXPECT_NE(Render(panel).find("+8 (5 +3)"), std::string::npos);
}

TEST_F(InspectPanelTest, ShowsRemainingEnhancements) {
  sword_.set_upgrade_slots(7);
  Equip state;
  state.set_equip_name("Sword");
  state.set_remaining_upgrade_slots(4);
  EquipInstance item(sword_, state);
  InspectPanel panel;
  panel.SetItem(&item);
  EXPECT_NE(Render(panel).find("Remaining Enhancements: 4"), std::string::npos);
}

TEST_F(InspectPanelTest, ZeroStatRowNotShown) {
  EquipInstance item(sword_);
  InspectPanel panel;
  panel.SetItem(&item);
  EXPECT_EQ(Render(panel).find("DEF"), std::string::npos);
}

TEST_F(InspectPanelTest, NoStatsShowsNoStatsText) {
  EquipInstance item(sword_);
  InspectPanel panel;
  panel.SetItem(&item);
  EXPECT_NE(Render(panel).find("(no stats)"), std::string::npos);
}

TEST_F(InspectPanelTest, ShowsStarForceStatBreakdown) {
  sword_.mutable_base_stats()->set_attack(7);
  sword_.set_required_level(10);
  Equip state;
  state.set_equip_name("Sword");
  state.set_stars(5);
  EquipInstance item(sword_, state);
  InspectPanel panel;
  panel.SetItem(&item);
  // 5★ on a level-10 weapon (max 5★, warrior): SF gives STR+DEX but no ATK
  // gains (non-weapon slot logic). ATT row shows only base: +7 (7 +0 +0) →
  // sf=0 so format stays "+7 (7 +0)".
  // STR gains: 2+2+2+2+2 = 10. Row shows "+10 (0 +0 +10)".
  EXPECT_NE(Render(panel).find("+10 (0 +0 +10)"), std::string::npos);
}

TEST_F(InspectPanelTest, StarBarShowsFilledAndEmptyStars) {
  // Level 0 item (max 5★) at 3★: expect 3 filled and 2 empty.
  Equip state;
  state.set_stars(3);
  EquipInstance item(sword_, state);
  InspectPanel panel;
  panel.SetItem(&item);
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("★★★☆☆"), std::string::npos);
}

TEST_F(InspectPanelTest, StarBarLengthReflectsItemMaxStars) {
  // Level 95 item has max 8★; bar is split into two groups of 5 and 3.
  sword_.set_required_level(95);
  EquipInstance item(sword_);
  InspectPanel panel;
  panel.SetItem(&item);
  // All-empty 8★ bar: "☆☆☆☆☆ ☆☆☆" (5 + space + 3).
  EXPECT_NE(Render(panel).find("☆☆☆☆☆"), std::string::npos);
}

TEST_F(InspectPanelTest, ShowsTraceNameWithSuffix) {
  sword_.set_name("Iron Sword");
  Equip state;
  state.set_equip_name("Iron Sword");
  EquipTrace trace(sword_, state);
  InspectPanel panel;
  panel.SetItem(&trace);
  EXPECT_NE(Render(panel).find("Iron Sword Trace"), std::string::npos);
}

}  // namespace
}  // namespace ms
