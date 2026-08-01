#include "src/frontend/equipped_panel.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "src/equip_instance.h"
#include "src/frontend/panel_test_base.h"
#include "src/frontend/types.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"

namespace ms {
namespace {

class EquippedPanelTest : public PanelTest {
 protected:
  // Every entry the player can actually land on, in the order Down walks them.
  // Disabled entries are skipped rather than merely dimmed, so what this does
  // not contain is what the menu does not offer.
  std::vector<int> ReachableMenuEntries(ItemMenu& menu) {
    std::vector<int> seen{menu.selected()};
    for (;;) {
      menu.Down();
      if (menu.selected() == seen.back()) {
        return seen;
      }
      seen.push_back(menu.selected());
    }
  }
};

// The single rendered line holding `needle`, escape codes and all.
std::string LineWith(const std::string& rendered, const std::string& needle) {
  size_t at = rendered.find(needle);
  if (at == std::string::npos) {
    return "";
  }
  size_t begin = rendered.rfind('\n', at);
  begin = begin == std::string::npos ? 0 : begin + 1;
  size_t end = rendered.find('\n', at);
  return rendered.substr(begin, end - begin);
}

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

// The main-stat column follows the wearer's job, not the item: the same gear
// reads STR to a Swordman and DEX to an Archer. The panel gets that from
// PrimaryStatField rather than its own switch, so this is what would break if
// the two ever disagreed.
TEST_F(EquippedPanelTest, MainStatColumnFollowsTheWearersJob) {
  EquipPrototype hat;
  hat.set_name("Bandana");
  hat.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  hat.mutable_base_stats()->set_str(4);
  hat.mutable_base_stats()->set_dex(6);

  Character warrior_proto;
  warrior_proto.set_job(JOB_SWORDMAN);
  CharacterInstance warrior(rng_, std::move(warrior_proto));
  warrior.PickUp(std::make_unique<EquipInstance>(hat));
  warrior.Equip(0);
  EquippedPanel warrior_panel(warrior, panel_focus_);
  std::string worn_by_warrior =
      RenderComponent(warrior_panel.MakeComponent([]() {}));
  EXPECT_NE(worn_by_warrior.find("+4 STR"), std::string::npos);
  EXPECT_EQ(worn_by_warrior.find("+6 DEX"), std::string::npos);

  Character archer_proto;
  archer_proto.set_job(JOB_ARCHER);
  CharacterInstance archer(rng_, std::move(archer_proto));
  archer.PickUp(std::make_unique<EquipInstance>(hat));
  archer.Equip(0);
  EquippedPanel archer_panel(archer, panel_focus_);
  std::string worn_by_archer =
      RenderComponent(archer_panel.MakeComponent([]() {}));
  EXPECT_NE(worn_by_archer.find("+6 DEX"), std::string::npos);
  EXPECT_EQ(worn_by_archer.find("+4 STR"), std::string::npos);
}

// Stars worn without a claw keep their number on screen but their whole row is
// drawn dim, because the character's totals are not counting them.
// The same refusal the bag menu honours, on the panel the stars are worn in.
TEST_F(EquippedPanelTest, WornThrowingStarsOfferNoScrollOrStarForce) {
  EquipPrototype stars;
  stars.set_name("Subi Throwing-Stars");
  stars.set_equip_type(EQUIP_TYPE_THROWING_STAR);
  stars.set_equip_slot(EQUIP_SLOT_STARS);
  stars.add_unsupported_upgrades(UPGRADE_SCROLL);
  stars.add_unsupported_upgrades(UPGRADE_STAR_FORCE);
  c_.PickUp(std::make_unique<EquipInstance>(stars));
  c_.Equip(0);

  EquippedPanel panel(c_, panel_focus_);
  // The slot list is built during render, which is the order the app runs in:
  // the menu opens on a row the player is already looking at.
  RenderComponent(panel.MakeComponent([]() {}));
  panel.OpenMenu();
  ASSERT_EQ(panel.selected_slot(), EQUIP_SLOT_STARS);
  std::vector<int> reachable = ReachableMenuEntries(panel.menu());
  EXPECT_EQ(std::count(reachable.begin(), reachable.end(), kMenuScroll), 0);
  EXPECT_EQ(std::count(reachable.begin(), reachable.end(), kMenuStarForce), 0);
  EXPECT_NE(std::count(reachable.begin(), reachable.end(), kMenuInspect), 0);
}

TEST_F(EquippedPanelTest, DimsAnItemThatIsNotCounting) {
  EquipPrototype dagger;
  dagger.set_name("Reef Claw");
  dagger.set_equip_type(EQUIP_TYPE_DAGGER);
  dagger.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  dagger.mutable_base_stats()->set_attack(45);
  EquipPrototype stars;
  stars.set_name("Steely Throwing-Knives");
  stars.set_equip_type(EQUIP_TYPE_THROWING_STAR);
  stars.set_equip_slot(EQUIP_SLOT_STARS);
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
  // Color codes sit between the dim marker and the text, so the row is checked
  // as a whole rather than for an exact prefix.
  std::string stars_row = LineWith(rendered, "Steely");
  EXPECT_NE(stars_row.find("+25 ATT"), std::string::npos);  // still shown
  EXPECT_NE(stars_row.find("\033[2m"), std::string::npos);
  // The dagger is counting, so its row is drawn plainly.
  EXPECT_EQ(LineWith(rendered, "Reef Claw").find("\033[2m"), std::string::npos);
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
