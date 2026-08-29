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
#include "src/frontend/types.h"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/panel_test_base.h"
#include "src/item/equip_instance.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"

namespace ms {
namespace {

// No context menu in the game has anywhere near this many entries, so a walk
// that takes this many steps is walking in circles.
constexpr int kMenuWalkLimit = 32;

class EquippedPanelTest : public PanelTest {
 protected:
  // Walks the menu to `entry`, or gives up once it has been all the way round.
  // Bounded on purpose: an entry that is not reachable is a test failure, not
  // a reason to spin.
  static bool StepTo(ItemMenu& menu, int entry) {
    for (int step = 0; step < kMenuWalkLimit; ++step) {
      if (menu.selected() == entry) {
        return true;
      }
      menu.Down();
    }
    return false;
  }

  // Every entry the player can actually land on, in the order Down walks them.
  // Disabled entries are skipped rather than merely dimmed, so what this does
  // not contain is what the menu does not offer.
  std::vector<int> ReachableMenuEntries(ItemMenu& menu) {
    std::vector<int> seen{menu.selected()};
    for (int step = 0; step < kMenuWalkLimit; ++step) {
      menu.Down();
      // The walk ends where it started, the list being a ring. Watching for a
      // cursor that stopped moving instead would never end -- except on a menu
      // with one reachable entry, where the two are the same thing.
      if (menu.selected() == seen.front()) {
        return seen;
      }
      seen.push_back(menu.selected());
    }
    return seen;
  }

  // The Equipped panel of a `job` wearing a 45-attack weapon and a 25-attack
  // projectile, rendered. Nothing is asserted here about which of the two
  // counts -- that is what the caller reads off the rows.
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

  // The panel holds a reference, so each character has to outlive its render.
  std::vector<std::unique_ptr<CharacterInstance>> characters_;
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

// On the narrowest panel the list fills its width exactly, so the columns
// have to be measured to leave a blank one inside the right border. Nothing
// is kept inside the left one: that column is the cursor's.
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

  // The header row and the row under the rule. The rule itself spans the
  // panel, as every rule in the game does.
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

TEST_F(EquippedPanelTest, ShowsEquippedItemName) {
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  c_.Equip(0);
  EquippedPanel panel(c_, account_, panel_focus_);
  EXPECT_NE(RenderComponent(panel.MakeComponent([]() {})).find("Sword"),
            std::string::npos);
}

// A staff carries weapon and magic attack both; which one the row shows is the
// one the wielder actually swings with. Every magician branch is asked, because
// a list of jobs written out by hand went stale once and left the 3rd-job
// mages reading ATT.
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

  EquippedPanel panel(rogue, account_, panel_focus_);
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

// Stars worn without a claw keep their number on screen but their whole row is
// drawn dim, because the character's totals are not counting them.
// The same refusal the bag menu honours, on the panel the stars are worn in.
TEST_F(EquippedPanelTest, WornThrowingStarsOfferNoScrollOrStarForce) {
  EquipPrototype stars;
  stars.set_name("Subi Throwing-Stars");
  stars.set_equip_type(EQUIP_TYPE_THROWING_STAR);
  stars.set_equip_slot(EQUIP_SLOT_PROJECTILE);
  stars.add_unsupported_upgrades(UPGRADE_SCROLL);
  stars.add_unsupported_upgrades(UPGRADE_STAR_FORCE);
  // Past both gates, or neither entry would be offered on any item and the
  // assertions below would say nothing about throwing stars in particular.
  // ScrollAndStarForceArriveOnTime is the control.
  LevelTo(UnlockLevel(Feature::kStarForce));
  c_.PickUp(std::make_unique<EquipInstance>(stars));
  c_.Equip(0);

  EquippedPanel panel(c_, account_, panel_focus_);
  // The slot list is built during render, which is the order the app runs in:
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
  // Gone from the menu rather than greyed on it: ReachableMenuEntries cannot
  // tell those two apart, so the rendered menu is asked as well.
  std::string rendered = RenderElement(panel.menu().Render(0, 0));
  EXPECT_EQ(rendered.find("Scroll"), std::string::npos);
  EXPECT_EQ(rendered.find("Star Force"), std::string::npos);
}

TEST_F(EquippedPanelTest, DimsAnItemThatIsNotCounting) {
  // A rogue holding a dagger, and an archer holding a bow: neither draws the
  // ammunition worn beside it, so neither projectile counts.
  std::string rogue =
      RenderWorn(JOB_ROGUE, "Reef Claw", EQUIP_TYPE_DAGGER,
                 "Steely Throwing-Knives", EQUIP_TYPE_THROWING_STAR);
  std::string archer =
      RenderWorn(JOB_ARCHER, "War Bow", EQUIP_TYPE_BOW, "Bronze Arrow",
                 EQUIP_TYPE_ARROW_FOR_CROSSBOW);
  // Color codes sit between the dim marker and the text, so the row is checked
  // as a whole rather than for an exact prefix.
  std::string stars_row = LineWith(rogue, "Steely");
  EXPECT_NE(stars_row.find("+25 ATT"), std::string::npos);  // still shown
  EXPECT_NE(stars_row.find("\033[2m"), std::string::npos);
  EXPECT_NE(LineWith(archer, "Bronze").find("\033[2m"), std::string::npos);
  // Each weapon is counting, so its own row is drawn plainly.
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

// An upgrade the item refuses says so with a dash instead of a zero, which
// would read as an upgrade standing ready -- the same row the bag draws.
TEST_F(EquippedPanelTest, TheUpgradeColumnsReadADashWhenRefused) {
  EquipPrototype stars;
  stars.set_name("Subi Throwing-Stars");
  stars.set_equip_slot(EQUIP_SLOT_PROJECTILE);
  stars.add_unsupported_upgrades(UPGRADE_SCROLL);
  stars.add_unsupported_upgrades(UPGRADE_STAR_FORCE);
  sword_.set_upgrade_slots(7);
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  c_.PickUp(std::make_unique<EquipInstance>(stars));
  c_.Equip(0);
  c_.Equip(0);

  EquippedPanel panel(c_, account_, panel_focus_);
  std::string rendered = RenderComponent(panel.MakeComponent([]() {}));
  EXPECT_NE(LineWith(rendered, "Sword").find("+0"), std::string::npos);
  EXPECT_NE(LineWith(rendered, "Sword").find("0\u2605"), std::string::npos);
  EXPECT_EQ(LineWith(rendered, "Subi").find("+"), std::string::npos);
  EXPECT_EQ(LineWith(rendered, "Subi").find("\u2605"), std::string::npos);
}

TEST_F(EquippedPanelTest, ShowsColumnHeader) {
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  c_.Equip(0);
  EquippedPanel panel(c_, account_, panel_focus_);
  std::string rendered = RenderComponent(panel.MakeComponent([]() {}));
  EXPECT_NE(rendered.find("Name"), std::string::npos);
  EXPECT_NE(rendered.find("Equip Slot"), std::string::npos);
  EXPECT_NE(rendered.find("Scroll"), std::string::npos);
  EXPECT_NE(rendered.find("Star Force"), std::string::npos);
}

TEST_F(EquippedPanelTest, ShowsEquipSlotName) {
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  c_.Equip(0);
  EquippedPanel panel(c_, account_, panel_focus_);
  EXPECT_NE(RenderComponent(panel.MakeComponent([]() {})).find("Weapon"),
            std::string::npos);
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

// Throwing stars, so a test can wear two things at once and have a list worth
// walking. The other slot the game has today; equipped() is keyed by slot, so
// this lands below the weapon.
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
    // Fills the entry list the menu walks and the wrap measures itself
    // against; nothing has drawn this panel yet.
    RenderComponent(comp_);
  }

  std::unique_ptr<EquippedPanel> panel_;
  ftxui::Component comp_;
};

// Nothing stands above this list -- it has no tab bar over it -- so Up off the
// top row has nowhere to go but the bottom one.
TEST_F(EquippedPanelRingTest, ArrowUpFromTheTopRowWrapsToTheBottom) {
  ASSERT_EQ(c_.equipped().size(), 2u);
  ASSERT_EQ(panel_->selected(), 0);
  comp_->OnEvent(ftxui::Event::ArrowUp);
  EXPECT_EQ(panel_->selected(), 1);
}

TEST_F(EquippedPanelRingTest, ArrowDownFromTheBottomRowWrapsToTheTop) {
  comp_->OnEvent(ftxui::Event::ArrowDown);
  ASSERT_EQ(panel_->selected(), 1) << "the bottom row";
  comp_->OnEvent(ftxui::Event::ArrowDown);
  EXPECT_EQ(panel_->selected(), 0);
}

// The steps that are not at an edge still belong to the menu underneath.
TEST_F(EquippedPanelRingTest, WalksTheListNormallyInTheMiddle) {
  comp_->OnEvent(ftxui::Event::ArrowDown);
  EXPECT_EQ(panel_->selected(), 1);
  comp_->OnEvent(ftxui::Event::ArrowUp);
  EXPECT_EQ(panel_->selected(), 0);
}

// --- an empty list ---

// Container::Tab asks its active panel whether it is focusable and drops every
// key when the answer is no, and the ftxui::Menu behind this panel says no as
// soon as the list is empty. Nothing here reads a key today, but a panel that
// silently stops being dispatched to is a trap for whatever does next.
TEST_F(EquippedPanelTest, StaysFocusableWithNothingEquipped) {
  EquippedPanel panel(c_, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  ASSERT_TRUE(c_.equipped().empty());
  EXPECT_TRUE(comp->Focusable());
}

// Arrows on an empty list leave the cursor alone. The ftxui::Menu underneath
// would move its index anyway, putting selected() at -1, which selected_slot()
// would then read past the front of an empty slot list.
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

// The menu opens on whatever selected_slot() names, and on an empty list that
// is EQUIP_SLOT_UNSPECIFIED -- a slot equipped() has no entry for, which the
// Scroll action would look up with std::map::at and throw on.
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

// What the item menu anchors to. Read from the render rather than counted up
// from the header rows above the list, which is what the caller used to do.
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

// The list is a ring, and WrappingList turns the corner by writing selected_
// itself -- a move the ftxui::Menu never sees, so the Menu's own idea of the
// current row stays where the player left it. A caret drawn from that idea
// then points at one row while Enter acts on another, which is what the
// player sees: they wrap to the top and the caret stays at the bottom.
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
  comp->OnEvent(ftxui::Event::ArrowDown);  // round to the first
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80),
                                               ftxui::Dimension::Fixed(20));
  ftxui::Render(screen, comp->Render());
  ASSERT_EQ(panel.selected(), 0) << "the ring did not wrap";
  EXPECT_EQ(RowWithCursor(screen), top) << "the caret stayed behind the wrap";
}

// It follows the cursor rather than sitting at the top of the list.
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

// A character wearing nine items, named Gear1 to Gear9 for the rows they make
// -- the slots are listed in the order the window lists them, so a test can
// say which row it means.
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

// The panel rendered into a screen `rows` tall, which is what the half cap
// hands it on a real terminal: fewer rows than it has gear.
std::string RenderShort(ftxui::Component component, int rows) {
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80),
                                               ftxui::Dimension::Fixed(rows));
  ftxui::Render(screen, component->Render());
  return screen.ToString();
}

// Five list rows for nine items: the ones past the bottom wait their turn
// rather than pushing the panel past the room it was given.
TEST_F(EquippedPanelTest, AShortPanelShowsWhatFits) {
  CharacterInstance geared = MakeFullyGeared(rng_);
  panel_focus_ = kEquipPanel;
  EquippedPanel panel(geared, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  std::string rendered = RenderShort(comp, 10);
  EXPECT_NE(rendered.find("Gear1"), std::string::npos) << "the first row";
  EXPECT_EQ(rendered.find("Gear9"), std::string::npos) << "the last one";
}

// And the list follows the cursor down to them, which is the whole point of
// capping the panel rather than letting it run off the screen.
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

// The bar is only there while there is something to scroll to.
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

// Taking something off needs somewhere to put it, and the bag is not open
// yet. A greyed Unequip would be an invitation to a screen that is not there.
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

// The first step of the trail: something has opened, and the worn weapon is
// where the player has to go to use it. The name alone goes gold -- the
// columns after it say what they always said.
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
  // The weapon alone. Everything else worn is where it always was, and gilding
  // the lot would point at nothing in particular.
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

// Opening the menu is the step: the player looked, so the signpost comes down
// whether or not they went on to press anything.
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

// The same trail the bag menu carries, on the panel the player is led to
// first: the entry stays gold until they press it.
TEST_F(EquippedPanelTest, ANewUpgradeIsGoldOnTheMenu) {
  LevelTo(UnlockLevel(Feature::kScrolling));
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  c_.Equip(0);
  EquippedPanel panel(c_, account_, panel_focus_);
  RenderComponent(panel.MakeComponent([]() {}));
  panel.OpenMenu();
  EXPECT_EQ(LabelColor(panel.menu().Render(0, 0), "Scroll"), kYellow);
}

// Pressed anywhere, spent everywhere: the player has learned what the entry
// is, and the bag's copy of it has nothing left to teach them.
TEST_F(EquippedPanelTest, PressingTheUpgradePutsItsGoldOut) {
  LevelTo(UnlockLevel(Feature::kScrolling));
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  c_.Equip(0);
  EquippedPanel panel(c_, account_, panel_focus_);
  ScrollPanel sp(c_, {});
  RenderComponent(panel.MakeComponent([]() {}));
  panel.OpenMenu();
  ASSERT_TRUE(StepTo(panel.menu(), kGearMenuScroll));
  panel.OnMenuEvent(ftxui::Event::Return, sp);

  panel.OpenMenu();
  EXPECT_NE(LabelColor(panel.menu().Render(0, 0), "Scroll"), kYellow);
}

// Star force lights its entry and nothing else. By 120 the player has been
// opening this menu since level 40, so the weapon needs no signpost -- and a
// second gold thing on screen would only take the eye off the row that is
// actually new.
TEST_F(EquippedPanelTest, StarForceIsGoldOnTheMenuAlone) {
  sword_.set_upgrade_slots(1);
  Equip spent;
  spent.set_equip_name(sword_.name());
  spent.set_remaining_upgrade_slots(0);
  c_.PickUp(std::make_unique<EquipInstance>(sword_, spent));
  c_.Equip(0);
  EquippedPanel panel(c_, account_, panel_focus_);
  ScrollPanel sp(c_, {});
  ftxui::Component comp = panel.MakeComponent([]() {});

  // Scrolling's own trail walked first, or its gold would still be lit and
  // the assertions below could not tell the two upgrades apart.
  LevelTo(UnlockLevel(Feature::kScrolling));
  RenderComponent(comp);
  panel.OpenMenu();
  ASSERT_TRUE(StepTo(panel.menu(), kGearMenuScroll));
  panel.OnMenuEvent(ftxui::Event::Return, sp);

  LevelTo(UnlockLevel(Feature::kStarForce));
  // The weapon asked first: opening the menu is what puts a weapon's gold out,
  // so asking after would answer for the wrong reason.
  EXPECT_NE(LabelColor(comp->Render(), "Sword"), kYellow)
      << "star force lit the weapon as well";
  panel.OpenMenu();
  EXPECT_EQ(LabelColor(panel.menu().Render(0, 0), "Star Force"), kYellow);
  EXPECT_NE(LabelColor(panel.menu().Render(0, 0), "Scroll"), kYellow);
}

TEST_F(EquippedPanelTest, ScrollAndStarForceArriveOnTime) {
  // A spent weapon: star force refuses an item with upgrade slots still on
  // it, and this test is about the level gate rather than that refusal.
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

// The hammer's own gate, and the piece it has nothing to do to. It sits
// between the other two upgrades: scrolls fill the shelf, a hammer widens it.
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

// A piece a hammer can do nothing to keeps no row: the entry is hidden the way
// Scroll is on an item that refuses scrolls outright.
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

// Every item that takes stars at all carries the entry, greyed until its
// slots are spent. Hidden, it would have made the order a secret: a player
// scrolling a weapon would never see what scrolling it is for.
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
  // And the gold waits with it: an entry nobody can press is not the far end
  // of a trail.
  EXPECT_NE(LabelColor(panel.menu().Render(0, 0), "Star Force"), kYellow);
}

// --- highlighting ---

// This panel arrives at level 3, and a card across the screen does
// not say where to look. The gold border is what points at it.
TEST_F(EquippedPanelTest, LightsItsBorderGoldWhenHighlighted) {
  EquippedPanel panel(c_, account_, panel_focus_);
  ftxui::Component component = panel.MakeComponent([]() {});
  ASSERT_EQ(BorderColor(component->Render()), kTheme);
  panel.SetHighlighted(true);
  EXPECT_EQ(BorderColor(component->Render()), kYellow);
  panel.SetHighlighted(false);
  EXPECT_EQ(BorderColor(component->Render()), kTheme);
}

// The rule under the column headers is the only one this panel has, and it is
// there only once something is worn.
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

// An empty panel takes a different path through Render, and level 3 is exactly
// when this one is most likely to be empty.
TEST_F(EquippedPanelTest, LightsUpEvenWithNothingEquipped) {
  EquippedPanel panel(c_, account_, panel_focus_);
  ftxui::Component component = panel.MakeComponent([]() {});
  ASSERT_NE(RenderComponent(component).find("(empty)"), std::string::npos);
  panel.SetHighlighted(true);
  EXPECT_EQ(BorderColor(component->Render()), kYellow);
}

// --- The Symbols tab ---

class SymbolTabTest : public EquippedPanelTest {
 protected:
  // A character in Arcane River, which is what puts the second tab on the bar.
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
};

// Below Arcane River there is nothing a symbol tab could hold, so the bar is
// not drawn at all and the panel reads exactly as it always did.
TEST_F(SymbolTabTest, NoBarBeforeArcaneRiver) {
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

// A worn symbol reads its level, how far along the next one it is, and what it
// is worth -- in that order, which is the order the player levels it by.
TEST_F(SymbolTabTest, ASymbolRowIsItsLevelExpAndForce) {
  CharacterInstance c = Traveller();
  WearSymbol(c, "Arcane Symbol: Vanishing Journey",
             EQUIP_SLOT_SYMBOL_VANISHING_JOURNEY, /*level=*/8, /*exp=*/12);
  EquippedPanel panel(c, account_, panel_focus_);
  ftxui::Component component = panel.MakeComponent([]() {});
  // Down off the bar, then Right onto Symbols.
  component->OnEvent(ftxui::Event::ArrowUp);
  component->OnEvent(ftxui::Event::ArrowRight);
  std::string rendered = RenderComponent(component);
  EXPECT_NE(rendered.find("Vanishing Journey"), std::string::npos);
  EXPECT_NE(rendered.find("12/75"), std::string::npos) << rendered;
  EXPECT_NE(rendered.find("+100"), std::string::npos) << rendered;
}

// Worn symbols are on their own tab, so neither list shows the other's items.
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

  component->OnEvent(ftxui::Event::ArrowUp);
  component->OnEvent(ftxui::Event::ArrowRight);
  std::string symbols = RenderComponent(component);
  EXPECT_NE(symbols.find("Vanishing Journey"), std::string::npos);
  EXPECT_EQ(symbols.find("Sword"), std::string::npos);
}

// The tab is there from the moment Arcane River opens, and empty until the
// player puts their first symbol on.
TEST_F(SymbolTabTest, TheTabIsEmptyUntilASymbolIsWorn) {
  CharacterInstance c = Traveller();
  EquippedPanel panel(c, account_, panel_focus_);
  ftxui::Component component = panel.MakeComponent([]() {});
  component->OnEvent(ftxui::Event::ArrowUp);
  component->OnEvent(ftxui::Event::ArrowRight);
  EXPECT_NE(RenderComponent(component).find("(empty)"), std::string::npos);
}

// The symbol menu offers only what a symbol can be put through: it takes no
// scrolls and no stars.
TEST_F(SymbolTabTest, TheSymbolMenuLeavesTheUpgradesOff) {
  CharacterInstance c = Traveller();
  WearSymbol(c, "Arcane Symbol: Vanishing Journey",
             EQUIP_SLOT_SYMBOL_VANISHING_JOURNEY, /*level=*/1, /*exp=*/0);
  EquippedPanel panel(c, account_, panel_focus_);
  ftxui::Component component = panel.MakeComponent([]() {});
  component->OnEvent(ftxui::Event::ArrowUp);
  component->OnEvent(ftxui::Event::ArrowRight);
  panel.OpenMenu();
  std::string rendered = RenderElement(panel.menu().Render(0, 0));
  EXPECT_NE(rendered.find("Unequip"), std::string::npos);
  EXPECT_NE(rendered.find("Inspect"), std::string::npos);
  EXPECT_EQ(rendered.find("Scroll"), std::string::npos);
  EXPECT_EQ(rendered.find("Star Force"), std::string::npos);
}

// Level Up is greyed until the duplicates are in, which is how the player
// learns that combining comes first. Reachable once they are.
TEST_F(SymbolTabTest, LevelUpWaitsForTheDuplicates) {
  CharacterInstance waiting = Traveller();
  WearSymbol(waiting, "Arcane Symbol: Vanishing Journey",
             EQUIP_SLOT_SYMBOL_VANISHING_JOURNEY, /*level=*/1, /*exp=*/11);
  EquippedPanel panel(waiting, account_, panel_focus_);
  ftxui::Component component = panel.MakeComponent([]() {});
  component->OnEvent(ftxui::Event::ArrowUp);
  component->OnEvent(ftxui::Event::ArrowRight);
  panel.OpenMenu();
  std::vector<int> reachable = ReachableMenuEntries(panel.menu());
  EXPECT_EQ(std::find(reachable.begin(), reachable.end(), kSymbolMenuLevelUp),
            reachable.end());

  CharacterInstance ready = Traveller();
  WearSymbol(ready, "Arcane Symbol: Vanishing Journey",
             EQUIP_SLOT_SYMBOL_VANISHING_JOURNEY, /*level=*/1, /*exp=*/12);
  EquippedPanel open(ready, account_, panel_focus_);
  ftxui::Component ready_component = open.MakeComponent([]() {});
  ready_component->OnEvent(ftxui::Event::ArrowUp);
  ready_component->OnEvent(ftxui::Event::ArrowRight);
  open.OpenMenu();
  reachable = ReachableMenuEntries(open.menu());
  EXPECT_NE(std::find(reachable.begin(), reachable.end(), kSymbolMenuLevelUp),
            reachable.end());
}

// Pressing it leads to the dialog that asks for the meso.
TEST_F(SymbolTabTest, LevelUpOpensTheDialog) {
  CharacterInstance c = Traveller();
  WearSymbol(c, "Arcane Symbol: Vanishing Journey",
             EQUIP_SLOT_SYMBOL_VANISHING_JOURNEY, /*level=*/1, /*exp=*/12);
  EquippedPanel panel(c, account_, panel_focus_);
  ftxui::Component component = panel.MakeComponent([]() {});
  component->OnEvent(ftxui::Event::ArrowUp);
  component->OnEvent(ftxui::Event::ArrowRight);
  panel.OpenMenu();
  panel.menu().Down();
  panel.menu().Down();
  ASSERT_EQ(panel.menu().selected(), kSymbolMenuLevelUp);
  ScrollPanel scrolls(c, {});
  EXPECT_EQ(panel.OnMenuEvent(ftxui::Event::Return, scrolls), kSymbolLevel);
}

}  // namespace
}  // namespace ms
