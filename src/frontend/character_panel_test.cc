#include "src/frontend/character_panel.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "src/equip_instance.h"
#include "src/frontend/panel_test_base.h"
#include "src/frontend/types.h"

namespace ms {
namespace {

class CharacterPanelTest : public PanelTest {};

TEST_F(CharacterPanelTest, ShowsLevel) {
  CharacterPanel panel(c_, panel_focus_);
  EXPECT_NE(RenderElement(panel.Render()).find("Lv  1"), std::string::npos);
}

TEST_F(CharacterPanelTest, ShowsJobName) {
  CharacterPanel panel(c_, panel_focus_);
  EXPECT_NE(RenderElement(panel.Render()).find("Beginner"), std::string::npos);
}

TEST_F(CharacterPanelTest, ShowsBothTabs) {
  CharacterPanel panel(c_, panel_focus_);
  std::string rendered = RenderElement(panel.Render());
  EXPECT_NE(rendered.find("Stats"), std::string::npos);
  EXPECT_NE(rendered.find("Skills"), std::string::npos);
}

TEST_F(CharacterPanelTest, StatsTabIsShownByDefault) {
  CharacterPanel panel(c_, panel_focus_);
  EXPECT_NE(RenderElement(panel.Render()).find("HP:"), std::string::npos);
}

TEST_F(CharacterPanelTest, ArrowKeysSwitchTabs) {
  CharacterPanel panel(c_, panel_focus_);  // panel_focus_ == kCharPanel
  ftxui::Component comp = panel.MakeComponent([] {});
  EXPECT_NE(RenderComponent(comp).find("HP:"), std::string::npos);

  comp->OnEvent(ftxui::Event::ArrowRight);  // Stats -> Skills
  std::string skills = RenderComponent(comp);
  EXPECT_NE(skills.find("No skills yet."), std::string::npos);
  EXPECT_EQ(skills.find("HP:"), std::string::npos);

  comp->OnEvent(ftxui::Event::ArrowLeft);  // Skills -> Stats
  EXPECT_NE(RenderComponent(comp).find("HP:"), std::string::npos);
}

TEST_F(CharacterPanelTest, EnterOnStatsTabOpensApWhenThereIsAp) {
  CharacterInstance c = MakeCharacter(/*level=*/1, /*ap=*/5);
  CharacterPanel panel(c, panel_focus_);
  bool ap = false;
  ftxui::Component comp = panel.MakeComponent([&] { ap = true; });
  comp->OnEvent(ftxui::Event::Return);  // Stats tab is the default
  EXPECT_TRUE(ap);
}

TEST_F(CharacterPanelTest, EnterOnStatsTabDoesNothingWithoutAp) {
  CharacterInstance c = MakeCharacter(/*level=*/1, /*ap=*/0);
  CharacterPanel panel(c, panel_focus_);
  bool ap = false;
  ftxui::Component comp = panel.MakeComponent([&] { ap = true; });
  comp->OnEvent(ftxui::Event::Return);
  EXPECT_FALSE(ap);
}

TEST_F(CharacterPanelTest, ShowsEquipAttackFromEquippedItem) {
  sword_.mutable_base_stats()->set_attack(10);
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  c_.Equip(0);
  CharacterPanel panel(c_, panel_focus_);
  EXPECT_NE(RenderElement(panel.Render()).find("ATT: 10"), std::string::npos);
}

TEST_F(CharacterPanelTest, ShowsStrWithBreakdownWhenGearContributes) {
  sword_.mutable_base_stats()->set_str(5);
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  c_.Equip(0);
  CharacterPanel panel(c_, panel_focus_);
  // base AP STR is 0 for the test character; gear adds 5; total = 5.
  EXPECT_NE(RenderElement(panel.Render()).find("STR: 5 (0+5)"),
            std::string::npos);
}

TEST_F(CharacterPanelTest, StressTestStatRowWidth) {
  // Exercises the widest realistic stat strings to verify kContentWidth holds.
  // " LUK: 999999 (1300+998699)" is the longest at 26 chars.
  Character proto;
  proto.set_level(1);
  proto.set_job(JOB_BEGINNER);
  proto.mutable_allocated_stats()->set_str(4);
  proto.mutable_allocated_stats()->set_dex(4);
  proto.mutable_allocated_stats()->set_int_(4);
  proto.mutable_allocated_stats()->set_luk(1300);
  CharacterInstance c(rng_, std::move(proto));
  EquipPrototype gear;
  gear.set_name("StressTest");
  gear.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  gear.mutable_base_stats()->set_str(995);
  gear.mutable_base_stats()->set_dex(9995);
  gear.mutable_base_stats()->set_int_(99995);
  gear.mutable_base_stats()->set_luk(998699);
  c.PickUp(std::make_unique<EquipInstance>(gear));
  c.Equip(0);
  CharacterPanel panel(c, panel_focus_);
  std::string rendered = RenderElement(panel.Render());
  EXPECT_NE(rendered.find("STR: 999 (4+995)"), std::string::npos);
  EXPECT_NE(rendered.find("DEX: 9999 (4+9995)"), std::string::npos);
  EXPECT_NE(rendered.find("INT: 99999 (4+99995)"), std::string::npos);
  EXPECT_NE(rendered.find("LUK: 999999 (1300+998699)"), std::string::npos);
}

}  // namespace
}  // namespace ms
