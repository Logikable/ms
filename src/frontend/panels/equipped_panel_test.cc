#include "src/frontend/panels/equipped_panel.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/character/progression.h"
#include "src/frontend/panel_widths.h"
#include "src/frontend/testing/panel_test_base.h"
#include "src/frontend/testing/screen_text.h"
#include "src/frontend/types.h"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/keys.h"
#include "src/item/equip_instance.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"

namespace ms {
namespace {

// No context menu has anywhere near this many entries, so a walk that takes
// this many steps is going in circles.
constexpr int kMenuWalkLimit = 32;

class EquippedPanelTest : public PanelTest {
 protected:
  // A ScrollPanel holds its catalog by reference, so the catalog must outlive
  // it.
  const std::map<std::string, Scroll> no_scrolls_;

  // Moves through the menu to `entry`, giving up after one full round. The
  // limit is deliberate: an unreachable entry is a test failure, not a reason
  // to loop.
  static bool StepTo(ItemMenu& menu, int entry) {
    for (int step = 0; step < kMenuWalkLimit; ++step) {
      if (menu.selected() == entry) {
        return true;
      }
      menu.Down();
    }
    return false;
  }

  // Every entry the player can land on, in the order Down visits them. Disabled
  // entries are skipped rather than just dimmed, so anything missing here is
  // something the menu doesn't offer.
  std::vector<int> ReachableMenuEntries(ItemMenu& menu) {
    std::vector<int> seen{menu.selected()};
    for (int step = 0; step < kMenuWalkLimit; ++step) {
      menu.Down();
      // The walk ends back where it started, since the list wraps. Waiting for
      // the cursor to stop moving would never end, except on a menu with one
      // reachable entry, where the two are the same.
      if (menu.selected() == seen.front()) {
        return seen;
      }
      seen.push_back(menu.selected());
    }
    return seen;
  }

  // The rendered Equipped panel of a `job` wearing a 45-attack weapon and a
  // 25-attack projectile. Nothing here asserts which one counts; the caller
  // reads that from the rows.
  std::string RenderWorn(Job job, const std::string& weapon_name,
                         EquipType weapon_type, const std::string& ammo_name,
                         EquipType ammo_type) {
    EquipPrototype weapon;
    weapon.set_name(weapon_name);
    weapon.set_equip_type(weapon_type);
    weapon.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
    weapon.mutable_base_stats()->set_attack(45);
    EquipPrototype ammo;
    ammo.set_name(ammo_name);
    ammo.set_equip_type(ammo_type);
    ammo.set_equip_slot(EQUIP_SLOT_PROJECTILE);
    ammo.mutable_base_stats()->set_attack(25);

    Character proto;
    proto.set_job(job);
    characters_.push_back(
        std::make_unique<CharacterInstance>(rng_, std::move(proto)));
    CharacterInstance& character = *characters_.back();
    character.PickUp(std::make_unique<EquipInstance>(weapon));
    character.Equip(0);
    character.PickUp(std::make_unique<EquipInstance>(ammo));
    character.Equip(0);
    EXPECT_EQ(character.equip_stats().attack(), 45) << ammo_name << " counted";

    EquippedPanel panel(character, account_, panel_focus_);
    return RenderComponent(panel.MakeComponent([]() {}));
  }

  // The panel holds a reference, so each character must outlive its render.
  std::vector<std::unique_ptr<CharacterInstance>> characters_;
};

// The single rendered line containing `needle`, escape codes included.
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

// On the narrowest panel the list fills its width exactly, so the columns must
// be measured to leave a blank column inside the right border. Nothing is kept
// inside the left one, since that column is the cursor's.
TEST_F(EquippedPanelTest, TheListKeepsAGutterInsideTheRightBorder) {
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  c_.Equip(0);
  EquippedPanel panel(c_, account_, panel_focus_);
  panel.SetWidth(kRightColumnMin);
  ftxui::Component comp = panel.MakeComponent([]() {});
  ftxui::Screen screen = ftxui::Screen::Create(
      ftxui::Dimension::Fixed(kRightColumnMin), ftxui::Dimension::Fixed(10));
  ftxui::Element card = comp->Render();
  ftxui::Render(screen, card);

  // The header row and the row under the rule. The rule itself spans the panel,
  // like every rule in the game.
  for (int y : {1, 3}) {
    const std::string& cell = screen.PixelAt(kRightColumnMin - 2, y).character;
    EXPECT_TRUE(cell.empty() || cell == " ")
        << "row " << y << " runs into the border";
  }
}

TEST_F(EquippedPanelTest, ShowsEmptyWhenNothingEquipped) {
  EquippedPanel panel(c_, account_, panel_focus_);
  EXPECT_NE(RenderComponent(panel.MakeComponent([]() {})).find("(empty)"),
            std::string::npos);
}

TEST_F(EquippedPanelTest, ShowsTheEquippedItemAndTheSlotItIsIn) {
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  c_.Equip(0);
  EquippedPanel panel(c_, account_, panel_focus_);
  std::string rendered = RenderComponent(panel.MakeComponent([]() {}));
  EXPECT_NE(rendered.find("Sword"), std::string::npos);
  EXPECT_NE(rendered.find("Weapon"), std::string::npos);
}

// A staff carries both weapon and magic attack, and the row shows the one the
// wearer actually uses. Every magician branch is checked, because a
// hand-written list of jobs once went stale and left the 3rd-job mages showing
// ATT.
TEST_F(EquippedPanelTest, ShowsMagicAttackForEveryMagician) {
  EquipPrototype staff;
  staff.set_name("Old Wooden Staff");
  staff.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  staff.mutable_base_stats()->set_attack(26);
  staff.mutable_base_stats()->set_magic_attack(35);

  const Job kMagicians[] = {
      JOB_MAGICIAN, JOB_ICE_LIGHTNING_WIZARD, JOB_FIRE_POISON_WIZARD,
      JOB_CLERIC,   JOB_ICE_LIGHTNING_MAGE,   JOB_FIRE_POISON_MAGE,
      JOB_PRIEST,
  };
  for (Job job : kMagicians) {
    Character proto;
    proto.set_job(job);
    CharacterInstance mage(rng_, std::move(proto));
    mage.PickUp(std::make_unique<EquipInstance>(staff));
    mage.Equip(0);

    EquippedPanel panel(mage, account_, panel_focus_);
    std::string rendered = RenderComponent(panel.MakeComponent([]() {}));
    EXPECT_NE(rendered.find("+35 MATT"), std::string::npos) << Job_Name(job);
    EXPECT_EQ(rendered.find("+26 ATT"), std::string::npos) << Job_Name(job);
  }
}

TEST_F(EquippedPanelTest, ShowsWeaponAttackForEveryoneElse) {
  EquipPrototype staff;
  staff.set_name("Old Wooden Staff");
  staff.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  staff.mutable_base_stats()->set_attack(26);
  staff.mutable_base_stats()->set_magic_attack(35);

  Character proto;
  proto.set_job(JOB_SWORDMAN);
  CharacterInstance warrior(rng_, std::move(proto));
  warrior.PickUp(std::make_unique<EquipInstance>(staff));
  warrior.Equip(0);

  EquippedPanel panel(warrior, account_, panel_focus_);
  EXPECT_NE(RenderComponent(panel.MakeComponent([]() {})).find("+26 ATT"),
            std::string::npos);
}

// Attack decides a weapon, so it comes before the main stat.
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

  EquippedPanel panel(rogue, account_, panel_focus_);
  std::string rendered = RenderComponent(panel.MakeComponent([]() {}));
  EXPECT_LT(rendered.find("+18 ATT"), rendered.find("+3 LUK"));
}

// The main-stat column follows the wearer's job, not the item: the same gear
// shows STR to a Swordman and DEX to an Archer. The panel gets this from
// PrimaryStatField rather than its own switch, so this catches the two
// disagreeing.
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
  EquippedPanel warrior_panel(warrior, account_, panel_focus_);
  std::string worn_by_warrior =
      RenderComponent(warrior_panel.MakeComponent([]() {}));
  EXPECT_NE(worn_by_warrior.find("+4 STR"), std::string::npos);
  EXPECT_EQ(worn_by_warrior.find("+6 DEX"), std::string::npos);

  Character archer_proto;
  archer_proto.set_job(JOB_ARCHER);
  CharacterInstance archer(rng_, std::move(archer_proto));
  archer.PickUp(std::make_unique<EquipInstance>(hat));
  archer.Equip(0);
  EquippedPanel archer_panel(archer, account_, panel_focus_);
  std::string worn_by_archer =
      RenderComponent(archer_panel.MakeComponent([]() {}));
  EXPECT_NE(worn_by_archer.find("+6 DEX"), std::string::npos);
  EXPECT_EQ(worn_by_archer.find("+4 STR"), std::string::npos);
}

// Throwing stars can't be scrolled or starred, and the menu on this panel
// refuses them the same way the bag's does. Stars worn without a claw still
// show their number, but their row is dimmed because the character's totals
// don't count them.
TEST_F(EquippedPanelTest, WornThrowingStarsOfferNoScrollOrStarForce) {
  EquipPrototype stars;
  stars.set_name("Subi Throwing-Stars");
  stars.set_equip_type(EQUIP_TYPE_THROWING_STAR);
  stars.set_equip_slot(EQUIP_SLOT_PROJECTILE);
  stars.add_unsupported_upgrades(UPGRADE_SCROLL);
  stars.add_unsupported_upgrades(UPGRADE_STAR_FORCE);
  // Past both gates, or neither entry would be offered on any item and the
  // checks below would say nothing about throwing stars.
  // ScrollAndStarForceArriveOnTime is the control.
  LevelTo(UnlockLevel(Feature::kStarForce));
  c_.PickUp(std::make_unique<EquipInstance>(stars));
  c_.Equip(0);

  EquippedPanel panel(c_, account_, panel_focus_);
  // The slot list is built during render, matching the order the app runs in:
  // the menu opens on a row the player is already looking at.
  RenderComponent(panel.MakeComponent([]() {}));
  panel.OpenMenu();
  ASSERT_EQ(panel.selected_slot(), EQUIP_SLOT_PROJECTILE);
  std::vector<int> reachable = ReachableMenuEntries(panel.menu());
  EXPECT_EQ(std::count(reachable.begin(), reachable.end(), kGearMenuScroll), 0);
  EXPECT_EQ(std::count(reachable.begin(), reachable.end(), kGearMenuStarForce),
            0);
  EXPECT_NE(std::count(reachable.begin(), reachable.end(), kGearMenuInspect),
            0);
  // Removed from the menu, not greyed. ReachableMenuEntries can't tell the two
  // apart, so the rendered menu is checked too.
  std::string rendered = RenderElement(panel.menu().Render(0, 0));
  EXPECT_EQ(rendered.find("Scroll"), std::string::npos);
  EXPECT_EQ(rendered.find("Star Force"), std::string::npos);
}

TEST_F(EquippedPanelTest, DimsAnItemThatIsNotCounting) {
  // A rogue with a dagger and an archer with a bow. Neither uses the ammunition
  // worn beside the weapon, so neither projectile counts.
  std::string rogue =
      RenderWorn(JOB_ROGUE, "Reef Claw", EQUIP_TYPE_DAGGER,
                 "Steely Throwing-Knives", EQUIP_TYPE_THROWING_STAR);
  std::string archer =
      RenderWorn(JOB_ARCHER, "War Bow", EQUIP_TYPE_BOW, "Bronze Arrow",
                 EQUIP_TYPE_ARROW_FOR_CROSSBOW);
  // Colour codes sit between the dim marker and the text, so the whole row is
  // checked rather than an exact prefix.
  std::string stars_row = LineWith(rogue, "Steely");
  EXPECT_NE(stars_row.find("+25 ATT"), std::string::npos);  // still shown
  EXPECT_NE(stars_row.find("\033[2m"), std::string::npos);
  EXPECT_NE(LineWith(archer, "Bronze").find("\033[2m"), std::string::npos);
  // Each weapon counts, so its row is drawn normally.
  EXPECT_EQ(LineWith(rogue, "Reef Claw").find("\033[2m"), std::string::npos);
  EXPECT_EQ(LineWith(archer, "War Bow").find("\033[2m"), std::string::npos);
}

TEST_F(EquippedPanelTest, ShowsSelectionCursorByDefault) {
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  c_.Equip(0);
  panel_focus_ = kEquipPanel;
  EquippedPanel panel(c_, account_, panel_focus_);
  EXPECT_NE(RenderComponent(panel.MakeComponent([]() {})).find("> Sword"),
            std::string::npos);
}

// An upgrade the item can't take shows a dash instead of a zero, which would
// look like an upgrade ready to use. The bag draws the same row.
TEST_F(EquippedPanelTest, TheUpgradeColumnsReadADashWhenRefused) {
  EquipPrototype stars;
  stars.set_name("Subi Throwing-Stars");
  stars.set_equip_slot(EQUIP_SLOT_PROJECTILE);
  stars.add_unsupported_upgrades(UPGRADE_SCROLL);
  stars.add_unsupported_upgrades(UPGRADE_STAR_FORCE);
  sword_.set_upgrade_slots(7);
  UnlockEverything();
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  c_.PickUp(std::make_unique<EquipInstance>(stars));
  c_.Equip(0);
  c_.Equip(0);

  EquippedPanel panel(c_, account_, panel_focus_);
  std::string rendered = RenderComponent(panel.MakeComponent([]() {}));
  EXPECT_NE(LineWith(rendered, "Sword").find("0/7"), std::string::npos);
  EXPECT_NE(LineWith(rendered, "Sword").find("0\u2605"), std::string::npos);
  EXPECT_EQ(LineWith(rendered, "Subi").find("/"), std::string::npos);
  EXPECT_EQ(LineWith(rendered, "Subi").find("\u2605"), std::string::npos);
}

// The upgrade columns appear with their mechanics, so a player who has never
// scrolled doesn't see an empty Scroll column.
TEST_F(EquippedPanelTest, ShowsColumnHeader) {
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  c_.Equip(0);
  EquippedPanel locked(c_, account_, panel_focus_);
  std::string rendered = RenderComponent(locked.MakeComponent([]() {}));
  EXPECT_NE(rendered.find("Name"), std::string::npos);
  EXPECT_NE(rendered.find("Equip Slot"), std::string::npos);
  EXPECT_EQ(rendered.find("Scroll"), std::string::npos);
  EXPECT_EQ(rendered.find("Stars"), std::string::npos);
  EXPECT_EQ(rendered.find("Potential"), std::string::npos);

  UnlockEverything();
  EquippedPanel open(c_, account_, panel_focus_);
  rendered = RenderComponent(open.MakeComponent([]() {}));
  EXPECT_NE(rendered.find("Scroll"), std::string::npos);
  EXPECT_NE(rendered.find("Stars"), std::string::npos);
  EXPECT_NE(rendered.find("Potential"), std::string::npos);
}

TEST_F(EquippedPanelTest, SelectedSlotReturnsEquippedSlot) {
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  c_.Equip(0);
  EquippedPanel panel(c_, account_, panel_focus_);
  RenderComponent(panel.MakeComponent([]() {}));
  EXPECT_EQ(panel.selected_slot(), EQUIP_SLOT_PRIMARY_WEAPON);
}

TEST_F(EquippedPanelTest, SelectedSlotReturnsUnspecifiedWhenEmpty) {
  EquippedPanel panel(c_, account_, panel_focus_);
  EXPECT_EQ(panel.selected_slot(), EQUIP_SLOT_UNSPECIFIED);
}

// --- the list is a ring ---

namespace {

// Throwing stars, so a test can wear two items and have a list worth moving
// through. equipped() is keyed by slot, so this lands below the weapon.
EquipPrototype MakeStars() {
  EquipPrototype stars;
  stars.set_name("Subi Throwing-Stars");
  stars.set_equip_slot(EQUIP_SLOT_PROJECTILE);
  stars.add_equip_job_categories(EQUIP_JOB_CATEGORY_UNIVERSAL);
  return stars;
}

}  // namespace

class EquippedPanelRingTest : public EquippedPanelTest {
 protected:
  void SetUp() override {
    EquippedPanelTest::SetUp();
    c_.PickUp(std::make_unique<EquipInstance>(sword_));
    c_.Equip(0);
    c_.PickUp(std::make_unique<EquipInstance>(MakeStars()));
    c_.Equip(0);
    panel_ = std::make_unique<EquippedPanel>(c_, account_, panel_focus_);
    comp_ = panel_->MakeComponent([]() {});
    // Fills the entry list the menu walks and the wrap measures against.
    // Nothing has drawn this panel yet.
    RenderComponent(comp_);
  }

  std::unique_ptr<EquippedPanel> panel_;
  ftxui::Component comp_;
};

// The bar is above the list at every level, so it is the stop past either end:
// the ring passes through it rather than going row to row.
TEST_F(EquippedPanelRingTest, TheBarIsTheStopPastEitherEndOfTheList) {
  ASSERT_EQ(c_.equipped().size(), 2u);
  ASSERT_EQ(panel_->selected(), 0);
  comp_->OnEvent(ftxui::Event::ArrowUp);
  comp_->OnEvent(ftxui::Event::ArrowUp);
  EXPECT_EQ(panel_->selected(), 1) << "the bar, then the bottom row";

  comp_->OnEvent(ftxui::Event::ArrowDown);
  comp_->OnEvent(ftxui::Event::ArrowDown);
  EXPECT_EQ(panel_->selected(), 0) << "the bar, then the top row";
}

// Steps away from the edges still belong to the menu underneath.
TEST_F(EquippedPanelRingTest, WalksTheListNormallyInTheMiddle) {
  comp_->OnEvent(ftxui::Event::ArrowDown);
  EXPECT_EQ(panel_->selected(), 1);
  comp_->OnEvent(ftxui::Event::ArrowUp);
  EXPECT_EQ(panel_->selected(), 0);
}

// --- an empty list ---

// Container::Tab asks its active panel whether it is focusable and drops every
// key when it isn't, and the ftxui::Menu behind this panel says it isn't once
// the list is empty. Nothing here reads a key today, but a panel that silently
// stops receiving keys is a trap for whatever does next.
TEST_F(EquippedPanelTest, StaysFocusableWithNothingEquipped) {
  EquippedPanel panel(c_, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  ASSERT_TRUE(c_.equipped().empty());
  EXPECT_TRUE(comp->Focusable());
}

// Arrows on an empty list leave the cursor alone. The ftxui::Menu underneath
// would move its index anyway, putting selected() at -1, and selected_slot()
// would then read before the start of an empty slot list.
TEST_F(EquippedPanelTest, ArrowsDoNothingWithNothingEquipped) {
  EquippedPanel panel(c_, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  RenderComponent(comp);
  ASSERT_TRUE(c_.equipped().empty());

  comp->OnEvent(ftxui::Event::ArrowUp);
  EXPECT_EQ(panel.selected(), 0);
  comp->OnEvent(ftxui::Event::ArrowDown);
  EXPECT_EQ(panel.selected(), 0);
  EXPECT_EQ(panel.selected_slot(), EQUIP_SLOT_UNSPECIFIED);
}

// The menu opens on whatever selected_slot() returns, which on an empty list is
// EQUIP_SLOT_UNSPECIFIED. equipped() has no entry for that, and the Scroll
// action would look it up with std::map::at and throw.
TEST_F(EquippedPanelTest, SpaceOpensNoMenuWithNothingEquipped) {
  bool opened = false;
  EquippedPanel panel(c_, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([&opened]() { opened = true; });
  comp->OnEvent(ftxui::Event::Character(' '));
  EXPECT_FALSE(opened);
}

TEST_F(EquippedPanelTest, SpaceOpensTheMenuOnAWornItem) {
  bool opened = false;
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  c_.Equip(0);
  EquippedPanel panel(c_, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([&opened]() { opened = true; });
  comp->OnEvent(ftxui::Event::Character(' '));
  EXPECT_TRUE(opened);
}

// --- cursor_row ---

// A rogue with both slots filled, so the list has two rows to tell apart.
CharacterInstance MakeRogueWithTwoItems(std::mt19937& rng) {
  EquipPrototype dagger;
  dagger.set_name("Reef Claw");
  dagger.set_equip_type(EQUIP_TYPE_DAGGER);
  dagger.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  EquipPrototype stars;
  stars.set_name("Steely Throwing-Knives");
  stars.set_equip_type(EQUIP_TYPE_THROWING_STAR);
  stars.set_equip_slot(EQUIP_SLOT_PROJECTILE);
  Character proto;
  proto.set_job(JOB_ROGUE);
  CharacterInstance rogue(rng, std::move(proto));
  rogue.PickUp(std::make_unique<EquipInstance>(dagger));
  rogue.Equip(0);
  rogue.PickUp(std::make_unique<EquipInstance>(stars));
  rogue.Equip(0);
  return rogue;
}

// The screen row a list cursor was drawn on, or -1.
int RowWithCursor(const ftxui::Screen& screen) {
  for (int y = 0; y < screen.dimy(); ++y) {
    for (int x = 0; x + 1 < screen.dimx(); ++x) {
      if (screen.PixelAt(x, y).character == ">" &&
          screen.PixelAt(x + 1, y).character == " ") {
        return y;
      }
    }
  }
  return -1;
}

// The caret shows where the cursor is, and the band shows how far the row
// reaches, so a stat eight columns away can be traced back to its item. The
// band has to cross the whole panel, and it is drawn under the same condition
// as the caret.
TEST_F(EquippedPanelTest, TheSelectedRowWearsABandAcrossThePanel) {
  CharacterInstance rogue = MakeRogueWithTwoItems(rng_);
  panel_focus_ = kEquipPanel;
  EquippedPanel panel(rogue, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80),
                                               ftxui::Dimension::Fixed(20));
  ftxui::Render(screen, comp->Render());

  std::vector<BandSpan> bands = BandSpans(screen, kSelectedRow);
  ASSERT_EQ(bands.size(), 1u) << "one row is selected, so one row is banded";
  EXPECT_EQ(bands[0].y, RowWithCursor(screen)) << "the band is under the caret";
  EXPECT_EQ(bands[0].first, 1) << "starts in the column inside the left border";
  // Two columns short of the right border, not one: the list reserves the
  // innermost column for its scroll bar, which isn't part of the row.
  EXPECT_EQ(bands[0].last, screen.dimx() - 3) << "and runs to the scroll bar";

  panel_focus_ = kInventoryPanel;
  ftxui::Screen away = ftxui::Screen::Create(ftxui::Dimension::Fixed(80),
                                             ftxui::Dimension::Fixed(20));
  ftxui::Render(away, comp->Render());
  EXPECT_TRUE(BandSpans(away, kSelectedRow).empty())
      << "another panel holds focus, so nothing here is selected";
}

// What the item menu is placed against. It is read from the render rather than
// counted from the header rows above the list.
TEST_F(EquippedPanelTest, CursorRowIsTheRowTheCursorWasDrawnOn) {
  CharacterInstance rogue = MakeRogueWithTwoItems(rng_);
  panel_focus_ = kEquipPanel;
  EquippedPanel panel(rogue, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80),
                                               ftxui::Dimension::Fixed(20));
  ftxui::Render(screen, comp->Render());
  int drawn = RowWithCursor(screen);
  ASSERT_GE(drawn, 0) << "no cursor was drawn";
  EXPECT_EQ(panel.cursor_row(), drawn);
}

// The list wraps, and WrappingList moves around the end by writing selected_
// itself, which the ftxui::Menu never sees. The Menu's current row then stays
// where the player left it, and a caret drawn from it would point at one row
// while Enter acts on another: the player wraps to the top and the caret stays
// at the bottom.
TEST_F(EquippedPanelTest, TheCursorFollowsTheSelectionAroundTheRing) {
  CharacterInstance rogue = MakeRogueWithTwoItems(rng_);
  panel_focus_ = kEquipPanel;
  EquippedPanel panel(rogue, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  RenderComponent(comp);
  int top = panel.cursor_row();
  comp->OnEvent(ftxui::Event::ArrowDown);  // to the second row
  RenderComponent(comp);
  ASSERT_EQ(panel.cursor_row(), top + 1);
  comp->OnEvent(ftxui::Event::ArrowDown);  // out to the bar
  comp->OnEvent(ftxui::Event::ArrowDown);  // round to the first
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80),
                                               ftxui::Dimension::Fixed(20));
  ftxui::Render(screen, comp->Render());
  ASSERT_EQ(panel.selected(), 0) << "the ring did not wrap";
  EXPECT_EQ(RowWithCursor(screen), top) << "the caret stayed behind the wrap";
}

// It follows the cursor instead of staying at the top of the list.
TEST_F(EquippedPanelTest, CursorRowMovesDownWithTheCursor) {
  CharacterInstance rogue = MakeRogueWithTwoItems(rng_);
  panel_focus_ = kEquipPanel;
  EquippedPanel panel(rogue, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  RenderComponent(comp);
  int first = panel.cursor_row();
  comp->OnEvent(ftxui::Event::ArrowDown);
  RenderComponent(comp);
  EXPECT_EQ(panel.cursor_row(), first + 1);
}

// --- scrolling a panel with more gear than room ---

// A character wearing nine items named Gear1 to Gear9 after the rows they
// appear in. The slots are listed in the window's order, so a test can say
// which row it means.
CharacterInstance MakeFullyGeared(std::mt19937& rng) {
  const EquipSlot kSlots[] = {
      EQUIP_SLOT_PRIMARY_WEAPON,
      EQUIP_SLOT_HAT,
      EQUIP_SLOT_TOP,
      EQUIP_SLOT_BOTTOM,
      EQUIP_SLOT_CAPE,
      EQUIP_SLOT_FACE_ACCESSORY,
      EQUIP_SLOT_EYE_ACCESSORY,
      EQUIP_SLOT_PROJECTILE,
      EQUIP_SLOT_SECONDARY,
  };
  Character proto;
  proto.set_job(JOB_SWORDMAN);
  CharacterInstance geared(rng, std::move(proto));
  int row = 0;
  for (EquipSlot slot : kSlots) {
    EquipPrototype item;
    item.set_name("Gear" + std::to_string(++row));
    item.set_equip_slot(slot);
    geared.PickUp(std::make_unique<EquipInstance>(item));
    geared.Equip(0);
  }
  return geared;
}

// The panel rendered into a screen `rows` tall, as the half-height cap gives it
// on a real terminal: fewer rows than it has gear.
std::string RenderShort(ftxui::Component component, int rows) {
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80),
                                               ftxui::Dimension::Fixed(rows));
  ftxui::Render(screen, component->Render());
  return screen.ToString();
}

// Five list rows for nine items. The rest wait below instead of pushing the
// panel past its space.
TEST_F(EquippedPanelTest, AShortPanelShowsWhatFits) {
  CharacterInstance geared = MakeFullyGeared(rng_);
  panel_focus_ = kEquipPanel;
  EquippedPanel panel(geared, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  std::string rendered = RenderShort(comp, 10);
  EXPECT_NE(rendered.find("Gear1"), std::string::npos) << "the first row";
  EXPECT_EQ(rendered.find("Gear9"), std::string::npos) << "the last one";
}

// The list follows the cursor down to them, which is the reason for capping the
// panel instead of letting it run off the screen.
TEST_F(EquippedPanelTest, AShortPanelScrollsToTheCursor) {
  CharacterInstance geared = MakeFullyGeared(rng_);
  panel_focus_ = kEquipPanel;
  EquippedPanel panel(geared, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  RenderShort(comp, 10);
  for (int i = 0; i < 8; ++i) {
    comp->OnEvent(ftxui::Event::ArrowDown);
  }
  ASSERT_EQ(panel.selected(), 8) << "not on the last row";
  std::string rendered = RenderShort(comp, 10);
  EXPECT_NE(rendered.find("> Gear9"), std::string::npos)
      << "the cursor walked off the bottom of the panel";
  EXPECT_EQ(rendered.find("Gear1 "), std::string::npos)
      << "the first row should have scrolled away";
}

// The bar shows only while there is something to scroll to.
TEST_F(EquippedPanelTest, TheScrollBarShowsOnlyOnAShortPanel) {
  CharacterInstance geared = MakeFullyGeared(rng_);
  panel_focus_ = kEquipPanel;
  EquippedPanel panel(geared, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  EXPECT_NE(RenderShort(comp, 10).find("\u2503"), std::string::npos)
      << "no scroll bar on a panel with more gear than room";
  EXPECT_EQ(RenderShort(comp, 20).find("\u2503"), std::string::npos)
      << "a scroll bar on a panel with room for everything";
}

// --- level-gated menu entries ---

// Unequipping needs somewhere to put the item, and the bag isn't open yet. A
// grey Unequip would point to a screen that doesn't exist.
TEST_F(EquippedPanelTest, UnequipWaitsForTheBag) {
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  c_.Equip(0);
  EquippedPanel panel(c_, account_, panel_focus_);
  RenderComponent(panel.MakeComponent([]() {}));

  LevelTo(UnlockLevel(Feature::kBag) - 1);
  panel.OpenMenu();
  std::vector<int> before = ReachableMenuEntries(panel.menu());
  EXPECT_EQ(std::count(before.begin(), before.end(), kGearMenuUnequip), 0);
  EXPECT_EQ(RenderElement(panel.menu().Render(0, 0)).find("Unequip"),
            std::string::npos);

  LevelTo(UnlockLevel(Feature::kBag));
  panel.OpenMenu();
  std::vector<int> after = ReachableMenuEntries(panel.menu());
  EXPECT_NE(std::count(after.begin(), after.end(), kGearMenuUnequip), 0);
}

// --- the gold trail to a new upgrade ---

// The first step of the trail: something has unlocked, and the worn weapon is
// where the player goes to use it. Only the name turns gold; the columns after
// it are unchanged.
TEST_F(EquippedPanelTest, TheWornWeaponsNameGoesGoldForANewUpgrade) {
  LevelTo(UnlockLevel(Feature::kScrolling));
  EquipPrototype stars;
  stars.set_name("Subi Throwing-Stars");
  stars.set_equip_type(EQUIP_TYPE_THROWING_STAR);
  stars.set_equip_slot(EQUIP_SLOT_PROJECTILE);
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  c_.Equip(0);
  c_.PickUp(std::make_unique<EquipInstance>(stars));
  c_.Equip(0);
  EquippedPanel panel(c_, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  EXPECT_EQ(LabelColor(comp->Render(), "Sword"), kYellow);
  EXPECT_NE(LabelColor(comp->Render(), "Weapon"), kYellow)
      << "the slot column is not being pointed at";
  // Only the weapon. Everything else worn is unchanged, and making it all gold
  // would point at nothing in particular.
  EXPECT_NE(LabelColor(comp->Render(), "Subi"), kYellow);
}

TEST_F(EquippedPanelTest, NoGoldOnTheWeaponBeforeAnythingOpens) {
  LevelTo(UnlockLevel(Feature::kScrolling) - 1);
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  c_.Equip(0);
  EquippedPanel panel(c_, account_, panel_focus_);
  EXPECT_NE(LabelColor(panel.MakeComponent([]() {})->Render(), "Sword"),
            kYellow);
}

// Opening the menu completes the step: the player looked, so the gold goes out
// whether or not they press anything.
TEST_F(EquippedPanelTest, OpeningTheMenuPutsTheWeaponsGoldOut) {
  LevelTo(UnlockLevel(Feature::kScrolling));
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  c_.Equip(0);
  EquippedPanel panel(c_, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  RenderComponent(comp);
  ASSERT_EQ(LabelColor(comp->Render(), "Sword"), kYellow);

  panel.OpenMenu();
  EXPECT_NE(LabelColor(comp->Render(), "Sword"), kYellow);
}

// The same trail as the bag menu, on the panel the player is led to first: the
// entry stays gold until they press it.
TEST_F(EquippedPanelTest, ANewUpgradeIsGoldOnTheMenu) {
  LevelTo(UnlockLevel(Feature::kScrolling));
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  c_.Equip(0);
  EquippedPanel panel(c_, account_, panel_focus_);
  RenderComponent(panel.MakeComponent([]() {}));
  panel.OpenMenu();
  EXPECT_EQ(LabelColor(panel.menu().Render(0, 0), "Scroll"), kYellow);
}

// Pressing it in either place turns it off in both: the player has learned what
// the entry is, and the bag's copy has nothing left to teach.
TEST_F(EquippedPanelTest, PressingTheUpgradePutsItsGoldOut) {
  LevelTo(UnlockLevel(Feature::kScrolling));
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  c_.Equip(0);
  EquippedPanel panel(c_, account_, panel_focus_);
  ScrollPanel sp(c_, no_scrolls_);
  RenderComponent(panel.MakeComponent([]() {}));
  panel.OpenMenu();
  ASSERT_TRUE(StepTo(panel.menu(), kGearMenuScroll));
  panel.OnMenuEvent(ftxui::Event::Return, sp);

  panel.OpenMenu();
  EXPECT_NE(LabelColor(panel.menu().Render(0, 0), "Scroll"), kYellow);
}

// Star force lights only its menu entry. By 120 the player has been opening
// this menu since level 40, so the weapon needs no gold, and a second gold item
// on screen would pull the eye from the new entry.
TEST_F(EquippedPanelTest, StarForceIsGoldOnTheMenuAlone) {
  sword_.set_upgrade_slots(1);
  Equip spent;
  spent.set_equip_name(sword_.name());
  spent.set_remaining_upgrade_slots(0);
  c_.PickUp(std::make_unique<EquipInstance>(sword_, spent));
  c_.Equip(0);
  EquippedPanel panel(c_, account_, panel_focus_);
  ScrollPanel sp(c_, no_scrolls_);
  ftxui::Component comp = panel.MakeComponent([]() {});

  // Scrolling's trail is completed first, or its gold would still be lit and
  // the checks below couldn't tell the two upgrades apart.
  LevelTo(UnlockLevel(Feature::kScrolling));
  RenderComponent(comp);
  panel.OpenMenu();
  ASSERT_TRUE(StepTo(panel.menu(), kGearMenuScroll));
  panel.OnMenuEvent(ftxui::Event::Return, sp);

  LevelTo(UnlockLevel(Feature::kStarForce));
  // The weapon is checked first, because opening the menu turns its gold off,
  // so checking afterwards would pass for the wrong reason.
  EXPECT_NE(LabelColor(comp->Render(), "Sword"), kYellow)
      << "star force lit the weapon as well";
  panel.OpenMenu();
  EXPECT_EQ(LabelColor(panel.menu().Render(0, 0), "Star Force"), kYellow);
  EXPECT_NE(LabelColor(panel.menu().Render(0, 0), "Scroll"), kYellow);
}

TEST_F(EquippedPanelTest, ScrollAndStarForceArriveOnTime) {
  // A weapon with no slots left. Star force refuses an item with upgrade slots
  // remaining, and this test is about the level gate, not that refusal.
  sword_.set_upgrade_slots(1);
  Equip spent;
  spent.set_equip_name(sword_.name());
  spent.set_remaining_upgrade_slots(0);
  c_.PickUp(std::make_unique<EquipInstance>(sword_, spent));
  c_.Equip(0);
  EquippedPanel panel(c_, account_, panel_focus_);
  RenderComponent(panel.MakeComponent([]() {}));

  LevelTo(UnlockLevel(Feature::kScrolling));
  panel.OpenMenu();
  std::vector<int> scrolling = ReachableMenuEntries(panel.menu());
  EXPECT_NE(std::count(scrolling.begin(), scrolling.end(), kGearMenuScroll), 0);
  EXPECT_EQ(std::count(scrolling.begin(), scrolling.end(), kGearMenuStarForce),
            0);

  LevelTo(UnlockLevel(Feature::kStarForce));
  panel.OpenMenu();
  std::vector<int> star_force = ReachableMenuEntries(panel.menu());
  EXPECT_NE(
      std::count(star_force.begin(), star_force.end(), kGearMenuStarForce), 0);
}

// The hammer's own gate, and an item it has nothing to do to. It sits between
// the other two upgrades: scrolls use the slots, and a hammer adds one.
TEST_F(EquippedPanelTest, TheHammerArrivesLastAndOnlyOnAPieceWithSlots) {
  sword_.set_upgrade_slots(1);
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  c_.Equip(0);
  EquippedPanel panel(c_, account_, panel_focus_);
  RenderComponent(panel.MakeComponent([]() {}));

  LevelTo(UnlockLevel(Feature::kStarForce));
  panel.OpenMenu();
  std::vector<int> before = ReachableMenuEntries(panel.menu());
  EXPECT_EQ(std::count(before.begin(), before.end(), kGearMenuHammer), 0);

  LevelTo(UnlockLevel(Feature::kHammer));
  panel.OpenMenu();
  std::vector<int> after = ReachableMenuEntries(panel.menu());
  EXPECT_NE(std::count(after.begin(), after.end(), kGearMenuHammer), 0);
  std::string rendered = RenderElement(panel.menu().Render(0, 0));
  EXPECT_LT(rendered.find("Scroll"), rendered.find("Hammer"));
  EXPECT_LT(rendered.find("Hammer"), rendered.find("Star Force"));
}

// Cubing's own gate, above every other upgrade, and the slot that refuses cubes
// outright.
TEST_F(EquippedPanelTest, CubingArrivesLastAndOnlyWherePotentialReaches) {
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  c_.Equip(0);
  EquippedPanel panel(c_, account_, panel_focus_);
  RenderComponent(panel.MakeComponent([]() {}));

  LevelTo(UnlockLevel(Feature::kHammer));
  panel.OpenMenu();
  std::vector<int> before = ReachableMenuEntries(panel.menu());
  EXPECT_EQ(std::count(before.begin(), before.end(), kGearMenuCube), 0);

  LevelTo(UnlockLevel(Feature::kPotential));
  panel.OpenMenu();
  std::vector<int> after = ReachableMenuEntries(panel.menu());
  EXPECT_NE(std::count(after.begin(), after.end(), kGearMenuCube), 0);
  std::string rendered = RenderElement(panel.menu().Render(0, 0));
  EXPECT_LT(rendered.find("Star Force"), rendered.find("Cube"));
  // It is gold until the player presses it, since cubing is the end of a trail
  // the level-up card starts.
  EXPECT_EQ(LabelColor(panel.menu().Render(0, 0), "Cube"), kYellow);
}

TEST_F(EquippedPanelTest, AMedalIsOfferedNoCube) {
  EquipPrototype medal;
  medal.set_name("Ludibrium Medal");
  medal.set_equip_slot(EQUIP_SLOT_MEDAL);
  LevelTo(UnlockLevel(Feature::kPotential));
  c_.PickUp(std::make_unique<EquipInstance>(medal));
  c_.Equip(0);
  EquippedPanel panel(c_, account_, panel_focus_);
  RenderComponent(panel.MakeComponent([]() {}));
  panel.OpenMenu();

  EXPECT_EQ(RenderElement(panel.menu().Render(0, 0)).find("Cube"),
            std::string::npos);
}

// An item a hammer can't improve gets no entry, just as Scroll is hidden on an
// item that refuses scrolls.
TEST_F(EquippedPanelTest, NoHammerEntryWithoutASlotToWiden) {
  sword_.set_upgrade_slots(0);
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  c_.Equip(0);
  LevelTo(UnlockLevel(Feature::kHammer));
  EquippedPanel panel(c_, account_, panel_focus_);
  RenderComponent(panel.MakeComponent([]() {}));
  panel.OpenMenu();

  EXPECT_EQ(RenderElement(panel.menu().Render(0, 0)).find("Hammer"),
            std::string::npos);
}

// Both hammers used, and the entry stays dim. If it vanished, it would look
// like the feature going away.
TEST_F(EquippedPanelTest, TheHammerGreysOnAFullyHammeredPiece) {
  sword_.set_upgrade_slots(1);
  Equip state;
  state.set_equip_name(sword_.name());
  state.set_hammers(kMaxHammers);
  c_.PickUp(std::make_unique<EquipInstance>(sword_, state));
  c_.Equip(0);
  LevelTo(UnlockLevel(Feature::kHammer));
  EquippedPanel panel(c_, account_, panel_focus_);
  RenderComponent(panel.MakeComponent([]() {}));
  panel.OpenMenu();

  std::vector<int> reachable = ReachableMenuEntries(panel.menu());
  EXPECT_EQ(std::count(reachable.begin(), reachable.end(), kGearMenuHammer), 0)
      << "a finished piece let the player onto the entry";
  EXPECT_NE(RenderElement(panel.menu().Render(0, 0)).find("Hammer"),
            std::string::npos)
      << "greyed, not gone";
}

// Every item that can take stars has the entry, greyed until its slots are
// used. Hiding it would keep the order secret: a player scrolling a weapon
// would never see what the scrolling leads to.
TEST_F(EquippedPanelTest, StarForceGreysWhileSlotsRemain) {
  sword_.set_upgrade_slots(1);
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  c_.Equip(0);
  LevelTo(UnlockLevel(Feature::kStarForce));
  EquippedPanel panel(c_, account_, panel_focus_);
  RenderComponent(panel.MakeComponent([]() {}));
  panel.OpenMenu();

  std::vector<int> reachable = ReachableMenuEntries(panel.menu());
  EXPECT_EQ(std::count(reachable.begin(), reachable.end(), kGearMenuStarForce),
            0)
      << "an unspent item let the player onto the entry";
  std::string rendered = RenderElement(panel.menu().Render(0, 0));
  EXPECT_NE(rendered.find("Star Force"), std::string::npos)
      << "greyed, not gone";
  // The gold waits too, since an entry nobody can press can't be the end of a
  // trail.
  EXPECT_NE(LabelColor(panel.menu().Render(0, 0), "Star Force"), kYellow);
}

// --- highlighting ---

// This panel arrives at level 3, and a card in the middle of the screen doesn't
// say where to look. The gold border points at it.
TEST_F(EquippedPanelTest, LightsItsBorderGoldWhenHighlighted) {
  EquippedPanel panel(c_, account_, panel_focus_);
  ftxui::Component component = panel.MakeComponent([]() {});
  ASSERT_EQ(BorderColor(component->Render()), kTheme);
  panel.SetHighlighted(true);
  EXPECT_EQ(BorderColor(component->Render()), kYellow);
  panel.SetHighlighted(false);
  EXPECT_EQ(BorderColor(component->Render()), kTheme);
}

// The rule under the column headers is this panel's only rule, and it appears
// only once something is worn.
TEST_F(EquippedPanelTest, LightsItsInnerRuleGoldToo) {
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  c_.Equip(0);
  EquippedPanel panel(c_, account_, panel_focus_);
  ftxui::Component component = panel.MakeComponent([]() {});
  ASSERT_EQ(InnerRuleColor(component->Render()), kTheme);
  panel.SetHighlighted(true);
  EXPECT_EQ(InnerRuleColor(component->Render()), kYellow);
  panel.SetHighlighted(false);
  EXPECT_EQ(InnerRuleColor(component->Render()), kTheme);
}

// An empty panel takes a different path through Render, and level 3 is when
// this one is most likely to be empty.
TEST_F(EquippedPanelTest, LightsUpEvenWithNothingEquipped) {
  EquippedPanel panel(c_, account_, panel_focus_);
  ftxui::Component component = panel.MakeComponent([]() {});
  ASSERT_NE(RenderComponent(component).find("(empty)"), std::string::npos);
  panel.SetHighlighted(true);
  EXPECT_EQ(BorderColor(component->Render()), kYellow);
}

// --- The gear preset row ---

class GearPresetTest : public EquippedPanelTest {
 protected:
  // A character at the level cubing unlocks, which is when presets unlock too.
  // Holds the panel's character for the life of the test.
  CharacterInstance& Cuber() {
    Character proto;
    proto.set_level(kPotentialUnlockLevel);
    proto.set_job(JOB_HERO);
    proto.set_job_stage(4);
    characters_.push_back(
        std::make_unique<CharacterInstance>(rng_, std::move(proto)));
    return *characters_.back();
  }

  void Wear(CharacterInstance& c, const std::string& name, EquipSlot slot,
            StatPreset preset) {
    EquipPrototype proto;
    proto.set_name(name);
    proto.set_equip_slot(slot);
    c.PickUp(std::make_unique<EquipInstance>(proto));
    ASSERT_TRUE(c.Equip(c.inventory().size() - 1, preset));
  }
};

// Below the cubing level there is only one set of gear, so the row isn't drawn
// and the panel looks as it always did.
TEST_F(GearPresetTest, NoRowBeforeCubing) {
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  c_.Equip(0);
  EquippedPanel panel(c_, account_, panel_focus_);
  std::string rendered = RenderComponent(panel.MakeComponent([]() {}));
  EXPECT_EQ(rendered.find("Farm"), std::string::npos);
  EXPECT_EQ(rendered.find("Drop"), std::string::npos);
}

// With the autoswap on, all three are named for their use, including the third
// (unlike the stat allocations), because the boss drop roll reads it.
TEST_F(GearPresetTest, NamesTheThreeWithTheAutoswapOn) {
  CharacterInstance& c = Cuber();
  c.set_autoswap_presets(true);
  Wear(c, "Sword", EQUIP_SLOT_PRIMARY_WEAPON, StatPreset::kFirst);
  EquippedPanel panel(c, account_, panel_focus_);
  std::string rendered = RenderComponent(panel.MakeComponent([]() {}));
  EXPECT_NE(rendered.find("Farm"), std::string::npos);
  EXPECT_NE(rendered.find("Boss"), std::string::npos);
  EXPECT_NE(rendered.find("Drop"), std::string::npos);
}

// With it off they are numbered, and the worn one has a mark.
TEST_F(GearPresetTest, NumbersThemWithTheAutoswapOff) {
  CharacterInstance& c = Cuber();
  c.SetSlotInUse(PresetKind::kEquip, StatPreset::kSecond);
  Wear(c, "Sword", EQUIP_SLOT_PRIMARY_WEAPON, StatPreset::kFirst);
  EquippedPanel panel(c, account_, panel_focus_);
  std::string rendered = RenderComponent(panel.MakeComponent([]() {}));
  EXPECT_EQ(rendered.find("Farm"), std::string::npos);
  EXPECT_NE(rendered.find("2 \u2713"), std::string::npos);
}

// Stepping right shows what that preset wears. A slot where it has nothing of
// its own is dimmed: the item is on the character, but it belongs to the Farm
// preset.
TEST_F(GearPresetTest, TheRowPicksWhichPresetIsListed) {
  CharacterInstance& c = Cuber();
  c.set_autoswap_presets(true);
  Wear(c, "Farm Sword", EQUIP_SLOT_PRIMARY_WEAPON, StatPreset::kFirst);
  Wear(c, "Farm Hat", EQUIP_SLOT_HAT, StatPreset::kFirst);
  Wear(c, "Boss Sword", EQUIP_SLOT_PRIMARY_WEAPON, StatPreset::kSecond);
  panel_focus_ = kEquipPanel;
  EquippedPanel panel(c, account_, panel_focus_);
  ftxui::Component component = panel.MakeComponent([]() {});
  RenderComponent(component);
  component->OnEvent(ftxui::Event::ArrowUp);  // the list to the preset row
  component->OnEvent(ftxui::Event::ArrowRight);

  std::string rendered = RenderComponent(component);
  EXPECT_EQ(panel.gear_preset(), StatPreset::kSecond);
  EXPECT_NE(rendered.find("Boss Sword"), std::string::npos);
  EXPECT_EQ(rendered.find("Farm Sword"), std::string::npos);
  EXPECT_EQ(LineWith(rendered, "Boss Sword").find("\033[2m"), std::string::npos)
      << "its own, so it is drawn plainly";
  EXPECT_NE(LineWith(rendered, "Farm Hat").find("\033[2m"), std::string::npos)
      << "inherited, so it is dimmed";
}

// The tab opens on the gear the player is wearing, so the list, the item menu
// and every item comparison card describe what is worn. The autoswap doesn't
// pick one preset, and its first chip is Farm.
TEST_F(GearPresetTest, OpensOnThePresetInUse) {
  CharacterInstance& c = Cuber();
  c.SetSlotInUse(PresetKind::kEquip, StatPreset::kSecond);
  Wear(c, "Boss Sword", EQUIP_SLOT_PRIMARY_WEAPON, StatPreset::kSecond);
  panel_focus_ = kEquipPanel;
  EquippedPanel panel(c, account_, panel_focus_);
  EXPECT_EQ(panel.gear_preset(), StatPreset::kSecond);
  EXPECT_NE(RenderComponent(panel.MakeComponent([]() {})).find("Boss Sword"),
            std::string::npos);

  c.set_autoswap_presets(true);
  EquippedPanel swapping(c, account_, panel_focus_);
  EXPECT_EQ(swapping.gear_preset(), StatPreset::kFirst);
}

// The row is three chips and doesn't wrap: Left from the first stays there, and
// so does Right from the last.
TEST_F(GearPresetTest, TheRowDoesNotWrap) {
  CharacterInstance& c = Cuber();
  Wear(c, "Sword", EQUIP_SLOT_PRIMARY_WEAPON, StatPreset::kFirst);
  panel_focus_ = kEquipPanel;
  EquippedPanel panel(c, account_, panel_focus_);
  ftxui::Component component = panel.MakeComponent([]() {});
  RenderComponent(component);
  component->OnEvent(ftxui::Event::ArrowUp);
  component->OnEvent(ftxui::Event::ArrowLeft);
  EXPECT_EQ(panel.gear_preset(), StatPreset::kFirst);
  for (int i = 0; i < 5; ++i) {
    component->OnEvent(ftxui::Event::ArrowRight);
  }
  EXPECT_EQ(panel.gear_preset(), StatPreset::kThird);
}

// With the autoswap off the player picks which preset is worn, and Enter on the
// chip does it. It is one action, so no menu opens.
TEST_F(GearPresetTest, EnterWearsThePresetWithTheAutoswapOff) {
  CharacterInstance& c = Cuber();
  Wear(c, "Farm Sword", EQUIP_SLOT_PRIMARY_WEAPON, StatPreset::kFirst);
  Wear(c, "Boss Sword", EQUIP_SLOT_PRIMARY_WEAPON, StatPreset::kSecond);
  panel_focus_ = kEquipPanel;
  EquippedPanel panel(c, account_, panel_focus_);
  ftxui::Component component = panel.MakeComponent([]() {});
  RenderComponent(component);
  component->OnEvent(ftxui::Event::ArrowUp);
  component->OnEvent(ftxui::Event::ArrowRight);
  component->OnEvent(ftxui::Event::Return);

  EXPECT_EQ(c.SlotInUse(PresetKind::kEquip), StatPreset::kSecond);
  EXPECT_EQ(c.weapon_type(c.SlotFor(PresetKind::kEquip, Activity::kFarming)),
            c.WornAt(StatPreset::kSecond, EQUIP_SLOT_PRIMARY_WEAPON)
                ->prototype()
                .equip_type());
  EXPECT_NE(RenderComponent(component).find("2 \u2713"), std::string::npos);
}

// With the autoswap on there is nothing to pick: the activity already wears the
// preset it names.
TEST_F(GearPresetTest, EnterPicksNothingWithTheAutoswapOn) {
  CharacterInstance& c = Cuber();
  c.set_autoswap_presets(true);
  Wear(c, "Farm Sword", EQUIP_SLOT_PRIMARY_WEAPON, StatPreset::kFirst);
  panel_focus_ = kEquipPanel;
  EquippedPanel panel(c, account_, panel_focus_);
  ftxui::Component component = panel.MakeComponent([]() {});
  RenderComponent(component);
  component->OnEvent(ftxui::Event::ArrowUp);
  component->OnEvent(ftxui::Event::ArrowRight);
  component->OnEvent(ftxui::Event::Return);

  EXPECT_EQ(c.SlotInUse(PresetKind::kEquip), StatPreset::kFirst);
}

// A preset can only unequip its own items, so Unequip is dimmed on an inherited
// row instead of removing the Farm preset's item.
TEST_F(GearPresetTest, UnequipIsDimmedOnAnInheritedRow) {
  CharacterInstance& c = Cuber();
  Wear(c, "Sword", EQUIP_SLOT_PRIMARY_WEAPON, StatPreset::kFirst);
  panel_focus_ = kEquipPanel;
  EquippedPanel panel(c, account_, panel_focus_);
  ftxui::Component component = panel.MakeComponent([]() {});
  RenderComponent(component);
  component->OnEvent(ftxui::Event::ArrowUp);
  component->OnEvent(ftxui::Event::ArrowRight);
  component->OnEvent(ftxui::Event::ArrowDown);
  panel.OpenMenu();

  std::vector<int> reachable = ReachableMenuEntries(panel.menu());
  EXPECT_EQ(std::find(reachable.begin(), reachable.end(), kGearMenuUnequip),
            reachable.end());
}

// --- The Symbols tab ---

class SymbolTabTest : public EquippedPanelTest {
 protected:
  // A character in Arcane River, which adds the second tab to the bar.
  CharacterInstance Traveller() {
    Character proto;
    proto.set_level(200);
    proto.set_job(JOB_HERO);
    proto.set_job_stage(4);
    return CharacterInstance(rng_, std::move(proto));
  }

  void WearSymbol(CharacterInstance& c, const std::string& name, EquipSlot slot,
                  int level, int exp) {
    EquipPrototype proto;
    proto.set_name(name);
    proto.set_equip_slot(slot);
    proto.mutable_arcane_symbol()->set_meso_cost_base(8);
    Equip state;
    state.set_symbol_level(level);
    state.set_symbol_exp(exp);
    c.PickUp(std::make_unique<EquipInstance>(proto, state));
    ASSERT_TRUE(c.Equip(0));
  }

  // Up to the tab bar and one step right, onto the Symbols tab. The way up
  // passes through the Gear tab's preset row, which a traveller has.
  void OpenSymbolTab(const ftxui::Component& component) {
    component->OnEvent(ftxui::Event::ArrowUp);
    component->OnEvent(ftxui::Event::ArrowUp);
    component->OnEvent(ftxui::Event::ArrowRight);
  }
};

// Below Arcane River a symbol tab would have nothing to hold, so there is no
// Symbols tab.
TEST_F(SymbolTabTest, NoSymbolsTabBeforeArcaneRiver) {
  EquippedPanel panel(c_, account_, panel_focus_);
  ftxui::Component component = panel.MakeComponent([]() {});
  EXPECT_EQ(RenderComponent(component).find("Symbols"), std::string::npos);
}

TEST_F(SymbolTabTest, TheTabArrivesWithArcaneRiver) {
  CharacterInstance c = Traveller();
  EquippedPanel panel(c, account_, panel_focus_);
  ftxui::Component component = panel.MakeComponent([]() {});
  std::string rendered = RenderComponent(component);
  EXPECT_NE(rendered.find("Gear"), std::string::npos);
  EXPECT_NE(rendered.find("Symbols"), std::string::npos);
}

// Expand is in the tab bar, which is drawn from the level the panel arrives, so
// it is there with Gear as the only chip, long before Symbols. Its label is the
// state Enter would leave the panel in.
TEST_F(SymbolTabTest, TheExpandTabIsThereWithGearAlone) {
  EquippedPanel bare(c_, account_, panel_focus_);
  EXPECT_NE(RenderComponent(bare.MakeComponent([]() {})).find("Expand"),
            std::string::npos);

  CharacterInstance c = Traveller();
  EquippedPanel panel(c, account_, panel_focus_);
  ftxui::Component component = panel.MakeComponent([]() {});
  EXPECT_NE(RenderComponent(component).find("Expand"), std::string::npos);
  panel.SetExpanded(true);
  std::string rendered = RenderComponent(component);
  EXPECT_NE(rendered.find("Close"), std::string::npos);
  EXPECT_EQ(rendered.find("Expand"), std::string::npos);
}

// The far right of the bar, past the Symbols tab. It is a door rather than a
// page: selecting it shows a line saying so instead of a list, and the bar
// wraps through it.
TEST_F(SymbolTabTest, TheExpandTabClosesTheRing) {
  CharacterInstance c = Traveller();
  EquipPrototype sword;
  sword.set_name("Sword");
  sword.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  c.PickUp(std::make_unique<EquipInstance>(sword));
  ASSERT_TRUE(c.Equip(0));
  panel_focus_ = kEquipPanel;
  EquippedPanel panel(c, account_, panel_focus_);
  int expands = 0;
  ftxui::Component component =
      panel.MakeComponent([]() {}, [&expands]() { ++expands; });
  RenderComponent(component);
  component->OnEvent(ftxui::Event::ArrowUp);     // the list to the presets
  component->OnEvent(ftxui::Event::ArrowUp);     // the presets to the bar
  component->OnEvent(ftxui::Event::ArrowRight);  // Gear to Symbols
  component->OnEvent(ftxui::Event::ArrowRight);  // Symbols to Expand
  EXPECT_NE(
      RenderComponent(component).find("Hit Enter to fullscreen Equipment"),
      std::string::npos);
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(120),
                                               ftxui::Dimension::Fixed(20));
  ftxui::Render(screen, component->Render());
  // The chip under the cursor is drawn white, so only one chip can be.
  EXPECT_EQ(PixelOf(screen, "Expand").background_color, ftxui::Color::White);
  EXPECT_NE(PixelOf(screen, "Symbols").background_color, ftxui::Color::White)
      << "the highlight is in one place, not two";

  component->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(expands, 1);
  EXPECT_EQ(panel.active_tab(), EquippedPanel::kGearTab)
      << "the wide panel opens on the first tab, not on the door";
  component->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(expands, 1) << "Enter on a page does nothing; it is not a door";

  component->OnEvent(ftxui::Event::ArrowLeft);  // Gear to Expand, wrapping
  EXPECT_NE(
      RenderComponent(component).find("Hit Enter to fullscreen Equipment"),
      std::string::npos);
  component->OnEvent(ftxui::Event::ArrowRight);  // Expand to Gear again
  EXPECT_EQ(
      RenderComponent(component).find("Hit Enter to fullscreen Equipment"),
      std::string::npos)
      << "the list is back";
  EXPECT_EQ(panel.active_tab(), EquippedPanel::kGearTab);
}

// A worn symbol shows its level, its progress to the next, and its value, in
// the order the player levels it by.
TEST_F(SymbolTabTest, ASymbolRowIsItsLevelExpAndForce) {
  CharacterInstance c = Traveller();
  WearSymbol(c, "Arcane Symbol: Vanishing Journey",
             EQUIP_SLOT_SYMBOL_VANISHING_JOURNEY, /*level=*/8, /*exp=*/12);
  EquippedPanel panel(c, account_, panel_focus_);
  ftxui::Component component = panel.MakeComponent([]() {});
  // Down from the bar, then Right onto Symbols.
  OpenSymbolTab(component);
  std::string rendered = RenderComponent(component);
  EXPECT_NE(rendered.find("Vanishing Journey"), std::string::npos);
  EXPECT_NE(rendered.find("12/75"), std::string::npos) << rendered;
  EXPECT_NE(rendered.find("+100"), std::string::npos) << rendered;
}

// Worn symbols have their own tab, so neither list shows the other's items.
TEST_F(SymbolTabTest, TheTwoListsDoNotShareItems) {
  CharacterInstance c = Traveller();
  EquipPrototype sword;
  sword.set_name("Sword");
  sword.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  c.PickUp(std::make_unique<EquipInstance>(sword));
  ASSERT_TRUE(c.Equip(0));
  WearSymbol(c, "Arcane Symbol: Vanishing Journey",
             EQUIP_SLOT_SYMBOL_VANISHING_JOURNEY, /*level=*/1, /*exp=*/0);

  EquippedPanel panel(c, account_, panel_focus_);
  ftxui::Component component = panel.MakeComponent([]() {});
  std::string gear = RenderComponent(component);
  EXPECT_NE(gear.find("Sword"), std::string::npos);
  EXPECT_EQ(gear.find("Vanishing Journey"), std::string::npos);

  OpenSymbolTab(component);
  std::string symbols = RenderComponent(component);
  EXPECT_NE(symbols.find("Vanishing Journey"), std::string::npos);
  EXPECT_EQ(symbols.find("Sword"), std::string::npos);
}

// The tab appears when Arcane River opens and stays empty until the player
// equips their first symbol.
TEST_F(SymbolTabTest, TheTabIsEmptyUntilASymbolIsWorn) {
  CharacterInstance c = Traveller();
  EquippedPanel panel(c, account_, panel_focus_);
  ftxui::Component component = panel.MakeComponent([]() {});
  OpenSymbolTab(component);
  EXPECT_NE(RenderComponent(component).find("(empty)"), std::string::npos);
}

// The symbol menu offers only what a symbol can take: no scrolls and no stars.
TEST_F(SymbolTabTest, TheSymbolMenuLeavesTheUpgradesOff) {
  CharacterInstance c = Traveller();
  WearSymbol(c, "Arcane Symbol: Vanishing Journey",
             EQUIP_SLOT_SYMBOL_VANISHING_JOURNEY, /*level=*/1, /*exp=*/0);
  EquippedPanel panel(c, account_, panel_focus_);
  ftxui::Component component = panel.MakeComponent([]() {});
  OpenSymbolTab(component);
  panel.OpenMenu();
  std::string rendered = RenderElement(panel.menu().Render(0, 0));
  EXPECT_NE(rendered.find("Unequip"), std::string::npos);
  EXPECT_NE(rendered.find("Inspect"), std::string::npos);
  EXPECT_EQ(rendered.find("Scroll"), std::string::npos);
  EXPECT_EQ(rendered.find("Star Force"), std::string::npos);
}

// Level Up is grey until the duplicates are combined, which is how the player
// learns that combining comes first. It becomes selectable afterwards.
TEST_F(SymbolTabTest, LevelUpWaitsForTheDuplicates) {
  CharacterInstance waiting = Traveller();
  WearSymbol(waiting, "Arcane Symbol: Vanishing Journey",
             EQUIP_SLOT_SYMBOL_VANISHING_JOURNEY, /*level=*/1, /*exp=*/11);
  EquippedPanel panel(waiting, account_, panel_focus_);
  ftxui::Component component = panel.MakeComponent([]() {});
  OpenSymbolTab(component);
  panel.OpenMenu();
  std::vector<int> reachable = ReachableMenuEntries(panel.menu());
  EXPECT_EQ(std::find(reachable.begin(), reachable.end(), kSymbolMenuLevelUp),
            reachable.end());

  CharacterInstance ready = Traveller();
  WearSymbol(ready, "Arcane Symbol: Vanishing Journey",
             EQUIP_SLOT_SYMBOL_VANISHING_JOURNEY, /*level=*/1, /*exp=*/12);
  EquippedPanel open(ready, account_, panel_focus_);
  ftxui::Component ready_component = open.MakeComponent([]() {});
  OpenSymbolTab(ready_component);
  open.OpenMenu();
  reachable = ReachableMenuEntries(open.menu());
  EXPECT_NE(std::find(reachable.begin(), reachable.end(), kSymbolMenuLevelUp),
            reachable.end());
}

// Pressing it opens the dialog that asks for the meso.
TEST_F(SymbolTabTest, LevelUpOpensTheDialog) {
  CharacterInstance c = Traveller();
  WearSymbol(c, "Arcane Symbol: Vanishing Journey",
             EQUIP_SLOT_SYMBOL_VANISHING_JOURNEY, /*level=*/1, /*exp=*/12);
  EquippedPanel panel(c, account_, panel_focus_);
  ftxui::Component component = panel.MakeComponent([]() {});
  OpenSymbolTab(component);
  panel.OpenMenu();
  panel.menu().Down();
  panel.menu().Down();
  ASSERT_EQ(panel.menu().selected(), kSymbolMenuLevelUp);
  ScrollPanel scrolls(c, no_scrolls_);
  EXPECT_EQ(panel.OnMenuEvent(ftxui::Event::Return, scrolls), kSymbolLevel);
}

// --- read-only, the panel the Inspect screen lists a party member with ---

// The trail is about the reader's own upgrades, so it isn't drawn on someone
// else's weapon.
TEST_F(EquippedPanelTest, ReadOnlyDrawsNoTrailToTheWeapon) {
  LevelTo(UnlockLevel(Feature::kScrolling));
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  c_.Equip(0);
  EquippedPanel panel(c_, account_, panel_focus_);
  panel.SetReadOnly(true);
  ftxui::Component comp = panel.MakeComponent([]() {});
  EXPECT_NE(LabelColor(comp->Render(), "Sword"), kYellow);
}

// Enter still calls whatever the caller passed (the item's card, on that
// screen), and the rows can still be moved through. It won't equip a preset,
// since a party member's gear isn't the reader's to switch.
TEST_F(GearPresetTest, ReadOnlyReadsThePresetsWithoutWearingOne) {
  CharacterInstance& c = Cuber();
  Wear(c, "Farm Sword", EQUIP_SLOT_PRIMARY_WEAPON, StatPreset::kFirst);
  Wear(c, "Boss Sword", EQUIP_SLOT_PRIMARY_WEAPON, StatPreset::kSecond);
  ASSERT_EQ(c.SlotInUse(PresetKind::kEquip), StatPreset::kFirst);
  panel_focus_ = kEquipPanel;
  EquippedPanel panel(c, account_, panel_focus_);
  panel.SetReadOnly(true);
  int opened = 0;
  ftxui::Component comp = panel.MakeComponent([&opened]() { ++opened; });
  RenderComponent(comp);

  comp->OnEvent(ftxui::Event::ArrowUp);     // the list to the preset row
  comp->OnEvent(ftxui::Event::ArrowRight);  // to their second preset
  comp->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(panel.gear_preset(), StatPreset::kSecond)
      << "the row is still read";
  EXPECT_NE(RenderComponent(comp).find("Boss Sword"), std::string::npos);
  EXPECT_EQ(c.SlotInUse(PresetKind::kEquip), StatPreset::kFirst)
      << "a reader put a preset on somebody else";

  comp->OnEvent(ftxui::Event::ArrowDown);  // to the list
  comp->OnEvent(ftxui::Event::Character(' '));
  EXPECT_EQ(opened, 1) << "Enter on a row raised nothing";
}

// Every row, not just the two the gutter test above checks, with an item in
// every slot so each column is at its widest.
TEST_F(EquippedPanelTest, NoRowWeldsItselfToTheRightBorder) {
  LevelTo(200);
  UnlockEverything();
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  c_.Equip(0);
  EquippedPanel panel(c_, account_, panel_focus_);
  panel.SetWidth(kRightColumnMin);
  ftxui::Component comp = panel.MakeComponent([]() {});
  EXPECT_TRUE(RowsTouchingTheRightBorder(comp->Render()).empty());
}
}  // namespace
}  // namespace ms
