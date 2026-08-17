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
#include "src/frontend/types.h"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/panel_test_base.h"
#include "src/item/equip_instance.h"
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
      // The walk ends where it started, the list being a ring. Watching for a
      // cursor that stopped moving instead would never end -- except on a menu
      // with one reachable entry, where the two are the same thing.
      if (menu.selected() == seen.front()) {
        return seen;
      }
      seen.push_back(menu.selected());
    }
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

    EquippedPanel panel(character, panel_focus_);
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

    EquippedPanel panel(mage, panel_focus_);
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

  EquippedPanel panel(warrior, panel_focus_);
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
  stars.set_equip_slot(EQUIP_SLOT_PROJECTILE);
  stars.add_unsupported_upgrades(UPGRADE_SCROLL);
  stars.add_unsupported_upgrades(UPGRADE_STAR_FORCE);
  // Past both gates, or neither entry would be offered on any item and the
  // assertions below would say nothing about throwing stars in particular.
  // ScrollAndStarForceArriveOnTime is the control.
  LevelTo(UnlockLevel(Feature::kStarForce));
  c_.PickUp(std::make_unique<EquipInstance>(stars));
  c_.Equip(0);

  EquippedPanel panel(c_, panel_focus_);
  // The slot list is built during render, which is the order the app runs in:
  // the menu opens on a row the player is already looking at.
  RenderComponent(panel.MakeComponent([]() {}));
  panel.OpenMenu();
  ASSERT_EQ(panel.selected_slot(), EQUIP_SLOT_PROJECTILE);
  std::vector<int> reachable = ReachableMenuEntries(panel.menu());
  EXPECT_EQ(std::count(reachable.begin(), reachable.end(), kMenuScroll), 0);
  EXPECT_EQ(std::count(reachable.begin(), reachable.end(), kMenuStarForce), 0);
  EXPECT_NE(std::count(reachable.begin(), reachable.end(), kMenuInspect), 0);
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
  EquippedPanel panel(c_, panel_focus_);
  EXPECT_NE(RenderComponent(panel.MakeComponent([]() {})).find("> Sword"),
            std::string::npos);
}

// The Scrolls column is a ledger of slots, so an item with none says so
// instead of reading three zeroes -- the same row the bag draws for it.
TEST_F(EquippedPanelTest, TheScrollColumnReadsADashWithoutSlots) {
  EquipPrototype stars;
  stars.set_name("Subi Throwing-Stars");
  stars.set_equip_slot(EQUIP_SLOT_PROJECTILE);
  stars.add_unsupported_upgrades(UPGRADE_SCROLL);
  sword_.set_upgrade_slots(7);
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  c_.PickUp(std::make_unique<EquipInstance>(stars));
  c_.Equip(0);
  c_.Equip(0);

  EquippedPanel panel(c_, panel_focus_);
  std::string rendered = RenderComponent(panel.MakeComponent([]() {}));
  EXPECT_NE(LineWith(rendered, "Sword").find("0/7/0"), std::string::npos);
  EXPECT_EQ(LineWith(rendered, "Subi").find("/"), std::string::npos);
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
    panel_ = std::make_unique<EquippedPanel>(c_, panel_focus_);
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
  EquippedPanel panel(c_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  ASSERT_TRUE(c_.equipped().empty());
  EXPECT_TRUE(comp->Focusable());
}

// Arrows on an empty list leave the cursor alone. The ftxui::Menu underneath
// would move its index anyway, putting selected() at -1, which selected_slot()
// would then read past the front of an empty slot list.
TEST_F(EquippedPanelTest, ArrowsDoNothingWithNothingEquipped) {
  EquippedPanel panel(c_, panel_focus_);
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
  EquippedPanel panel(c_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([&opened]() { opened = true; });
  comp->OnEvent(ftxui::Event::Character(' '));
  EXPECT_FALSE(opened);
}

TEST_F(EquippedPanelTest, SpaceOpensTheMenuOnAWornItem) {
  bool opened = false;
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  c_.Equip(0);
  EquippedPanel panel(c_, panel_focus_);
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
  EquippedPanel panel(rogue, panel_focus_);
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
  EquippedPanel panel(rogue, panel_focus_);
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
  EquippedPanel panel(rogue, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  RenderComponent(comp);
  int first = panel.cursor_row();
  comp->OnEvent(ftxui::Event::ArrowDown);
  RenderComponent(comp);
  EXPECT_EQ(panel.cursor_row(), first + 1);
}

// --- level-gated menu entries ---

// Taking something off needs somewhere to put it, and the bag is not open
// yet. A greyed Unequip would be an invitation to a screen that is not there.
TEST_F(EquippedPanelTest, UnequipWaitsForTheBag) {
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  c_.Equip(0);
  EquippedPanel panel(c_, panel_focus_);
  RenderComponent(panel.MakeComponent([]() {}));

  LevelTo(UnlockLevel(Feature::kBag) - 1);
  panel.OpenMenu();
  std::vector<int> before = ReachableMenuEntries(panel.menu());
  EXPECT_EQ(std::count(before.begin(), before.end(), kMenuAction), 0);
  EXPECT_EQ(RenderElement(panel.menu().Render(0, 0)).find("Unequip"),
            std::string::npos);

  LevelTo(UnlockLevel(Feature::kBag));
  panel.OpenMenu();
  std::vector<int> after = ReachableMenuEntries(panel.menu());
  EXPECT_NE(std::count(after.begin(), after.end(), kMenuAction), 0);
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
  EquippedPanel panel(c_, panel_focus_);
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
  EquippedPanel panel(c_, panel_focus_);
  EXPECT_NE(LabelColor(panel.MakeComponent([]() {})->Render(), "Sword"),
            kYellow);
}

// Opening the menu is the step: the player looked, so the signpost comes down
// whether or not they went on to press anything.
TEST_F(EquippedPanelTest, OpeningTheMenuPutsTheWeaponsGoldOut) {
  LevelTo(UnlockLevel(Feature::kScrolling));
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  c_.Equip(0);
  EquippedPanel panel(c_, panel_focus_);
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
  EquippedPanel panel(c_, panel_focus_);
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
  EquippedPanel panel(c_, panel_focus_);
  ScrollPanel sp(c_, {});
  RenderComponent(panel.MakeComponent([]() {}));
  panel.OpenMenu();
  while (panel.menu().selected() != kMenuScroll) {
    panel.menu().Down();
  }
  panel.OnMenuEvent(ftxui::Event::Return, sp);

  panel.OpenMenu();
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
  EquippedPanel panel(c_, panel_focus_);
  RenderComponent(panel.MakeComponent([]() {}));

  LevelTo(UnlockLevel(Feature::kScrolling));
  panel.OpenMenu();
  std::vector<int> scrolling = ReachableMenuEntries(panel.menu());
  EXPECT_NE(std::count(scrolling.begin(), scrolling.end(), kMenuScroll), 0);
  EXPECT_EQ(std::count(scrolling.begin(), scrolling.end(), kMenuStarForce), 0);

  LevelTo(UnlockLevel(Feature::kStarForce));
  panel.OpenMenu();
  std::vector<int> star_force = ReachableMenuEntries(panel.menu());
  EXPECT_NE(std::count(star_force.begin(), star_force.end(), kMenuStarForce),
            0);
}

// --- highlighting ---

// This panel arrives at level 3, and a card across the screen does
// not say where to look. The gold border is what points at it.
TEST_F(EquippedPanelTest, LightsItsBorderGoldWhenHighlighted) {
  EquippedPanel panel(c_, panel_focus_);
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
  EquippedPanel panel(c_, panel_focus_);
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
  EquippedPanel panel(c_, panel_focus_);
  ftxui::Component component = panel.MakeComponent([]() {});
  ASSERT_NE(RenderComponent(component).find("(empty)"), std::string::npos);
  panel.SetHighlighted(true);
  EXPECT_EQ(BorderColor(component->Render()), kYellow);
}

}  // namespace
}  // namespace ms
