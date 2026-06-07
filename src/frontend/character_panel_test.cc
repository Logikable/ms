#include "src/frontend/character_panel.h"

#include <gtest/gtest.h>

#include <string>

#include "src/frontend/panel_test_base.h"

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

TEST_F(CharacterPanelTest, ShowsAp) {
  c_.LevelUp();  // grants 5 AP; value appears on its own line below "AP"
  CharacterPanel panel(c_, panel_focus_);
  EXPECT_NE(RenderElement(panel.Render()).find("AP"), std::string::npos);
}

TEST_F(CharacterPanelTest, ShowsEquipAttackFromEquippedItem) {
  sword_.mutable_base_stats()->set_attack(10);
  c_.PickUp(sword_);
  c_.Equip(0);
  CharacterPanel panel(c_, panel_focus_);
  EXPECT_NE(RenderElement(panel.Render()).find("ATT: 10"), std::string::npos);
}

TEST_F(CharacterPanelTest, ShowsTotalStrIncludingEquipBonus) {
  sword_.mutable_base_stats()->set_str(5);
  c_.PickUp(sword_);
  c_.Equip(0);
  CharacterPanel panel(c_, panel_focus_);
  EXPECT_NE(RenderElement(panel.Render()).find("STR: 5"), std::string::npos);
}

}  // namespace
}  // namespace ms
