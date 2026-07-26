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

// Attack is what decides a weapon, so it comes before the main stat.
TEST_F(EquippedPanelTest, ShowsAttackAheadOfTheMainStat) {
  EquipPrototype claw;
  claw.set_name("Steel Guards");
  claw.set_equip_type(EQUIP_TYPE_CLAW);
  claw.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  claw.mutable_base_stats()->set_attack(18);
  claw.mutable_base_stats()->set_luk(3);

  Character proto;
  proto.set_job(JOB_ROGUE);
  CharacterInstance rogue(rng_, std::move(proto));
  rogue.PickUp(std::make_unique<EquipInstance>(claw));
  rogue.Equip(0);

  EquippedPanel panel(rogue, panel_focus_);
  std::string rendered = RenderComponent(panel.MakeComponent([]() {}));
  EXPECT_LT(rendered.find("+18 ATT"), rendered.find("+3 LUK"));
}

// Stars worn without a claw keep their number on screen but are drawn dim,
// because the character's totals are not counting them.
TEST_F(EquippedPanelTest, DimsAnAttackThatIsNotCounting) {
  EquipPrototype dagger;
  dagger.set_name("Reef Claw");
  dagger.set_equip_type(EQUIP_TYPE_DAGGER);
  dagger.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  dagger.mutable_base_stats()->set_attack(45);
  EquipPrototype stars;
  stars.set_name("Steely Throwing-Knives");
  stars.set_equip_type(EQUIP_TYPE_THROWING_STAR);
  stars.set_equip_slot(EQUIP_SLOT_SECONDARY_WEAPON);
  stars.mutable_base_stats()->set_attack(25);

  Character proto;
  proto.set_job(JOB_ROGUE);
  CharacterInstance rogue(rng_, std::move(proto));
  rogue.PickUp(std::make_unique<EquipInstance>(dagger));
  rogue.Equip(0);
  rogue.PickUp(std::make_unique<EquipInstance>(stars));
  rogue.Equip(0);
  ASSERT_EQ(rogue.equip_stats().attack(), 45);  // the stars are not counting

  EquippedPanel panel(rogue, panel_focus_);
  std::string rendered = RenderComponent(panel.MakeComponent([]() {}));
  EXPECT_NE(rendered.find("+25 ATT"), std::string::npos);  // still shown
  EXPECT_NE(rendered.find("\033[2m+25 ATT"), std::string::npos);
  // The claw's own attack is counting, so it is drawn plainly.
  EXPECT_EQ(rendered.find("\033[2m+45 ATT"), std::string::npos);
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
