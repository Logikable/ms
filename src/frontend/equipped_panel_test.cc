#include "src/frontend/equipped_panel.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>

#include "src/equip_instance.h"
#include "src/frontend/panel_test_base.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"

namespace ms {
namespace {

class EquippedPanelTest : public PanelTest {};

TEST_F(EquippedPanelTest, ShowsEmptyWhenNothingEquipped) {
  EquippedPanel panel(c_, panel_focus_);
  EXPECT_NE(RenderComponent(panel.MakeComponent([]() {})).find("(empty)"),
            std::string::npos);
}

TEST_F(EquippedPanelTest, ShowsEquippedItemName) {
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  c_.Equip(0);
  EquippedPanel panel(c_, panel_focus_);
  EXPECT_NE(RenderComponent(panel.MakeComponent([]() {})).find("Sword"),
            std::string::npos);
}

// A wand carries weapon and magic attack both; which one the row shows is the
// one the wielder actually swings with.
TEST_F(EquippedPanelTest, ShowsMagicAttackForAMagician) {
  EquipPrototype wand;
  wand.set_name("Metal Wand");
  wand.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  wand.mutable_base_stats()->set_attack(21);
  wand.mutable_base_stats()->set_magic_attack(33);

  Character proto;
  proto.set_job(JOB_MAGICIAN);
  CharacterInstance mage(rng_, std::move(proto));
  mage.PickUp(std::make_unique<EquipInstance>(wand));
  mage.Equip(0);

  EquippedPanel panel(mage, panel_focus_);
  std::string rendered = RenderComponent(panel.MakeComponent([]() {}));
  EXPECT_NE(rendered.find("+33 MATT"), std::string::npos);
  EXPECT_EQ(rendered.find("+21 ATT"), std::string::npos);
}

TEST_F(EquippedPanelTest, ShowsWeaponAttackForEveryoneElse) {
  EquipPrototype wand;
  wand.set_name("Metal Wand");
  wand.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  wand.mutable_base_stats()->set_attack(21);
  wand.mutable_base_stats()->set_magic_attack(33);

  Character proto;
  proto.set_job(JOB_SWORDMAN);
  CharacterInstance warrior(rng_, std::move(proto));
  warrior.PickUp(std::make_unique<EquipInstance>(wand));
  warrior.Equip(0);

  EquippedPanel panel(warrior, panel_focus_);
  EXPECT_NE(RenderComponent(panel.MakeComponent([]() {})).find("+21 ATT"),
            std::string::npos);
}

TEST_F(EquippedPanelTest, ShowsSelectionCursorByDefault) {
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  c_.Equip(0);
  EquippedPanel panel(c_, panel_focus_);
  EXPECT_NE(RenderComponent(panel.MakeComponent([]() {})).find("> Sword"),
            std::string::npos);
}

TEST_F(EquippedPanelTest, ShowsColumnHeader) {
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  c_.Equip(0);
  EquippedPanel panel(c_, panel_focus_);
  std::string rendered = RenderComponent(panel.MakeComponent([]() {}));
  EXPECT_NE(rendered.find("Name"), std::string::npos);
  EXPECT_NE(rendered.find("Equip Slot"), std::string::npos);
  EXPECT_NE(rendered.find("Scrolls"), std::string::npos);
}

TEST_F(EquippedPanelTest, ShowsEquipSlotName) {
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  c_.Equip(0);
  EquippedPanel panel(c_, panel_focus_);
  EXPECT_NE(RenderComponent(panel.MakeComponent([]() {})).find("Weapon"),
            std::string::npos);
}

TEST_F(EquippedPanelTest, SelectedSlotReturnsEquippedSlot) {
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  c_.Equip(0);
  EquippedPanel panel(c_, panel_focus_);
  RenderComponent(panel.MakeComponent([]() {}));
  EXPECT_EQ(panel.selected_slot(), EQUIP_SLOT_PRIMARY_WEAPON);
}

TEST_F(EquippedPanelTest, SelectedSlotReturnsUnspecifiedWhenEmpty) {
  EquippedPanel panel(c_, panel_focus_);
  EXPECT_EQ(panel.selected_slot(), EQUIP_SLOT_UNSPECIFIED);
}

}  // namespace
}  // namespace ms
