#include "src/frontend/panels/inventory_panel.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/character/progression.h"
#include "src/frontend/types.h"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/panel_test_base.h"
#include "src/frontend/widgets/panel_util.h"
#include "src/item/equip_instance.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"

namespace ms {
namespace {

class InventoryPanelTest : public PanelTest {
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

  EquipPrototype MakeThrowingStars() {
    EquipPrototype stars;
    stars.set_name("Subi Throwing-Stars");
    stars.set_equip_slot(EQUIP_SLOT_STARS);
    stars.set_equip_type(EQUIP_TYPE_THROWING_STAR);
    stars.add_equip_job_categories(EQUIP_JOB_CATEGORY_UNIVERSAL);
    stars.add_unsupported_upgrades(UPGRADE_SCROLL);
    stars.add_unsupported_upgrades(UPGRADE_STAR_FORCE);
    return stars;
  }

  ItemPrototype MakeStackable(const std::string& name, ItemCategory category,
                              int sell_price = 0) {
    ItemPrototype proto;
    proto.set_name(name);
    proto.set_category(category);
    proto.set_sell_price(sell_price);
    return proto;
  }

  // The same bounded screen RenderComponent uses, kept so a test can read
  // pixels rather than the joined string. The bound is the point: the list
  // really does overflow and scroll at this size.
  ftxui::Screen RenderToScreen(ftxui::Component component) {
    ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80),
                                                 ftxui::Dimension::Fixed(20));
    ftxui::Render(screen, component->Render());
    return screen;
  }

  // The screen row the list cursor was drawn on, or -1. Found by cell rather
  // than by searching the joined row, whose border glyphs are multibyte.
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

  // Fills the bag past what the test screen can show.
  void FillBag(int count) {
    for (int i = 0; i < count; ++i) {
      c_.PickUp(std::make_unique<EquipInstance>(sword_));
    }
  }

  // The panel wired the way the main screen wires it: as one tab of a
  // Container::Tab, which is what routes keys to it in the running game.
  // Every other test here calls OnEvent on the panel component directly, so
  // none of them can see a key that never gets dispatched.
  ftxui::Component InTabContainer(ftxui::Component panel) {
    return ftxui::Container::Tab({std::move(panel)}, &tab_selector_);
  }

  int tab_selector_ = 0;
};

// Container::Tab drops keys aimed at a child that reports itself unfocusable,
// and the equip Menu says exactly that when the bag is empty -- which used to
// take the tab bar down with it, leaving a new character unable to reach the
// Use, Etc or Shop tabs at all.
TEST_F(InventoryPanelTest, TheTabBarStillSwitchesTabsOnAnEmptyBag) {
  InventoryPanel panel(c_, panel_focus_);
  panel_focus_ = kInventoryPanel;
  ftxui::Component root = InTabContainer(panel.MakeComponent([]() {}));
  ASSERT_EQ(c_.inventory().size(), 0);
  root->OnEvent(ftxui::Event::ArrowRight);
  EXPECT_TRUE(panel.on_stackable_tab()) << "Equip -> Use";
  root->OnEvent(ftxui::Event::ArrowLeft);
  EXPECT_FALSE(panel.on_stackable_tab()) << "Use -> Equip";
}

// Equipping the last item in the bag takes the row the cursor was standing on
// with it. The panel used to hand focus to the equipped panel at that point;
// now it keeps it, so the cursor has to go somewhere it can be seen.
TEST_F(InventoryPanelTest, TheCursorLeavesAListThatEmptiedUnderIt) {
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  InventoryPanel panel(c_, panel_focus_);
  panel_focus_ = kInventoryPanel;
  ftxui::Component comp = panel.MakeComponent([]() {});
  comp->OnEvent(ftxui::Event::ArrowDown);  // tab bar -> the one item
  RenderComponent(comp);
  c_.Equip(0);
  RenderComponent(comp);
  comp->OnEvent(ftxui::Event::ArrowRight);
  EXPECT_TRUE(panel.on_stackable_tab())
      << "Right switched tabs, so the cursor is back on the tab bar";
}

TEST_F(InventoryPanelTest, ShowsEmptyWhenBagIsEmpty) {
  InventoryPanel panel(c_, panel_focus_);
  EXPECT_NE(RenderComponent(panel.MakeComponent([]() {})).find("(empty)"),
            std::string::npos);
}

TEST_F(InventoryPanelTest, ShowsItemName) {
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  InventoryPanel panel(c_, panel_focus_);
  EXPECT_NE(RenderComponent(panel.MakeComponent([]() {})).find("Sword"),
            std::string::npos);
}

TEST_F(InventoryPanelTest, ShowsSelectionCursorInListZone) {
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  InventoryPanel panel(c_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  comp->OnEvent(ftxui::Event::ArrowDown);  // tab bar -> item list
  EXPECT_NE(RenderComponent(comp).find("> Sword"), std::string::npos);
}

TEST_F(InventoryPanelTest, NoSelectionCursorOnTheTabRow) {
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  InventoryPanel panel(c_, panel_focus_);
  // The panel opens on the tab row, so the list cursor is hidden until Down.
  EXPECT_EQ(RenderComponent(panel.MakeComponent([]() {})).find("> Sword"),
            std::string::npos);
}

TEST_F(InventoryPanelTest, ShowsColumnHeader) {
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  InventoryPanel panel(c_, panel_focus_);
  std::string rendered = RenderComponent(panel.MakeComponent([]() {}));
  EXPECT_NE(rendered.find("Name"), std::string::npos);
  EXPECT_NE(rendered.find("Equip Slot"), std::string::npos);
  EXPECT_NE(rendered.find("Scrolls"), std::string::npos);
}

TEST_F(InventoryPanelTest, ShowsEquipSlotName) {
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  InventoryPanel panel(c_, panel_focus_);
  EXPECT_NE(RenderComponent(panel.MakeComponent([]() {})).find("Weapon"),
            std::string::npos);
}

TEST_F(InventoryPanelTest, ShowsSlotsRemaining) {
  sword_.set_upgrade_slots(7);
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  InventoryPanel panel(c_, panel_focus_);
  // Fresh item: 0 pass, 7 left, 0 restores.
  EXPECT_NE(RenderComponent(panel.MakeComponent([]() {})).find("0/7/0"),
            std::string::npos);
}

TEST_F(InventoryPanelTest, ShowsItemLevel) {
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  InventoryPanel panel(c_, panel_focus_);
  EXPECT_NE(RenderComponent(panel.MakeComponent([]() {})).find("Lv10"),
            std::string::npos);
}

TEST_F(InventoryPanelTest, ShowsWarriorJobCategory) {
  c_.PickUp(std::make_unique<EquipInstance>(
      sword_));  // sword_ has EQUIP_JOB_CATEGORY_WARRIOR
  InventoryPanel panel(c_, panel_focus_);
  EXPECT_NE(RenderComponent(panel.MakeComponent([]() {})).find("Warrior"),
            std::string::npos);
}

TEST_F(InventoryPanelTest, ShowsAllForUniversalItem) {
  EquipPrototype axe;
  axe.set_name("Axe");
  axe.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  axe.add_equip_job_categories(EQUIP_JOB_CATEGORY_UNIVERSAL);
  c_.PickUp(std::make_unique<EquipInstance>(axe));
  InventoryPanel panel(c_, panel_focus_);
  EXPECT_NE(RenderComponent(panel.MakeComponent([]() {})).find("All"),
            std::string::npos);
}

TEST_F(InventoryPanelTest, TraceMenuDisablesAllExceptInspect) {
  // Trigger a star force destroy to place a trace in inventory.
  EquipPrototype proto;
  proto.set_name("Sword");
  proto.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  proto.set_required_level(138);
  Equip state;
  state.set_stars(19);
  c_.PickUp(std::make_unique<EquipInstance>(proto, state));
  bool saw_destroy = false;
  for (int i = 0; i < 100 && !saw_destroy; ++i) {
    if (c_.StarForceInventory(0) == kStarForceDestroy) {
      saw_destroy = true;
    }
  }
  ASSERT_TRUE(saw_destroy);

  // High enough that every upgrade entry would be on the menu for an ordinary
  // item. What disables them here has to be the trace, not the level.
  LevelTo(UnlockLevel(Feature::kRecovery));
  InventoryPanel panel(c_, panel_focus_);
  panel.OpenMenu();
  // Recover is offered on a trace -- it is the one thing a trace is for -- so
  // Inspect and Recover are what remain. Equip, Scroll and Star Force all need
  // an item that still exists.
  // Read before walking the menu: ReachableMenuEntries moves the selection,
  // so where the menu opens has to be captured first.
  EXPECT_EQ(panel.menu().selected(), kMenuInspect);
  std::vector<int> reachable = ReachableMenuEntries(panel.menu());
  EXPECT_EQ(std::count(reachable.begin(), reachable.end(), kMenuAction), 0);
  EXPECT_EQ(std::count(reachable.begin(), reachable.end(), kMenuScroll), 0);
  EXPECT_EQ(std::count(reachable.begin(), reachable.end(), kMenuStarForce), 0);
  EXPECT_NE(std::count(reachable.begin(), reachable.end(), kMenuInspect), 0);
}

// Neither action can do anything to a throwing star, so the menu should not
// offer them. Scroll is the one that used to slip through: the picker always
// includes Clean Slate scrolls, so it opened on a list of scrolls that would
// have been refused.
TEST_F(InventoryPanelTest, ThrowingStarsOfferNoScrollOrStarForce) {
  // Levelled past both gates first. At level 1 neither entry is offered on
  // anything at all, so the assertions below would hold for an ordinary sword
  // and this would be a test of the gates, not of the throwing stars.
  // ASpentWeaponKeepsScrollAndStarForce is the control at the same level.
  LevelTo(UnlockLevel(Feature::kStarForce));
  c_.PickUp(std::make_unique<EquipInstance>(MakeThrowingStars()));
  InventoryPanel panel(c_, panel_focus_);
  panel.OpenMenu();
  std::vector<int> reachable = ReachableMenuEntries(panel.menu());
  EXPECT_EQ(std::count(reachable.begin(), reachable.end(), kMenuScroll), 0);
  EXPECT_EQ(std::count(reachable.begin(), reachable.end(), kMenuStarForce), 0);
  // Still a usable menu, or the assertions above would pass on a dead one.
  EXPECT_NE(std::count(reachable.begin(), reachable.end(), kMenuInspect), 0);
}

// An ordinary weapon keeps both, including one with no slots left: a spent
// weapon is what star force is for, and a Clean Slate still applies to it.
// --- level-gated menu entries ---

// A gated entry is not drawn at all, rather than drawn grey: greying it would
// advertise an upgrade the player cannot ask about yet.
TEST_F(InventoryPanelTest, ANewCharacterIsOfferedNoUpgrades) {
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  InventoryPanel panel(c_, panel_focus_);
  panel.OpenMenu();
  std::vector<int> reachable = ReachableMenuEntries(panel.menu());
  EXPECT_EQ(std::count(reachable.begin(), reachable.end(), kMenuScroll), 0);
  EXPECT_EQ(std::count(reachable.begin(), reachable.end(), kMenuStarForce), 0);
  EXPECT_EQ(std::count(reachable.begin(), reachable.end(), kMenuRecover), 0);
  EXPECT_EQ(RenderElement(panel.menu().Render(0, 0)).find("Scroll"),
            std::string::npos);
}

TEST_F(InventoryPanelTest, ScrollingArrivesAtItsLevel) {
  sword_.set_upgrade_slots(7);
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  InventoryPanel panel(c_, panel_focus_);
  LevelTo(UnlockLevel(Feature::kScrolling) - 1);
  panel.OpenMenu();
  std::vector<int> before = ReachableMenuEntries(panel.menu());
  EXPECT_EQ(std::count(before.begin(), before.end(), kMenuScroll), 0);

  LevelTo(UnlockLevel(Feature::kScrolling));
  panel.OpenMenu();
  std::vector<int> after = ReachableMenuEntries(panel.menu());
  EXPECT_NE(std::count(after.begin(), after.end(), kMenuScroll), 0);
}

// Star force and recovery are the two late ones, and they arrive separately.
TEST_F(InventoryPanelTest, StarForceAndRecoveryArriveAtTheirOwnLevels) {
  Equip destroyed;
  destroyed.set_equip_name(sword_.name());
  c_.PickUp(std::make_unique<EquipTrace>(sword_, destroyed));
  InventoryPanel panel(c_, panel_focus_);

  LevelTo(UnlockLevel(Feature::kStarForce));
  panel.OpenMenu();
  std::vector<int> at_60 = ReachableMenuEntries(panel.menu());
  EXPECT_EQ(std::count(at_60.begin(), at_60.end(), kMenuRecover), 0)
      << "recovery waits for 140";

  LevelTo(UnlockLevel(Feature::kRecovery));
  panel.OpenMenu();
  std::vector<int> at_140 = ReachableMenuEntries(panel.menu());
  EXPECT_NE(std::count(at_140.begin(), at_140.end(), kMenuRecover), 0);
}

TEST_F(InventoryPanelTest, ASpentWeaponKeepsScrollAndStarForce) {
  LevelTo(UnlockLevel(Feature::kStarForce));
  EquipPrototype proto = sword_;
  proto.set_upgrade_slots(1);
  Equip state;
  state.set_equip_name(proto.name());
  state.set_remaining_upgrade_slots(0);
  c_.PickUp(std::make_unique<EquipInstance>(proto, state));
  InventoryPanel panel(c_, panel_focus_);
  panel.OpenMenu();
  std::vector<int> reachable = ReachableMenuEntries(panel.menu());
  EXPECT_NE(std::count(reachable.begin(), reachable.end(), kMenuScroll), 0);
  EXPECT_NE(std::count(reachable.begin(), reachable.end(), kMenuStarForce), 0);
}

// --- the level-gated Shop tab ---

// The bar simply ends at Etc rather than showing a greyed fourth chip: a shop
// a character cannot walk into is not a tab they should be able to land on.
TEST_F(InventoryPanelTest, TheShopTabIsAbsentBeforeItsLevel) {
  InventoryPanel panel(c_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  panel_focus_ = kInventoryPanel;
  EXPECT_EQ(RenderComponent(comp).find("Shop"), std::string::npos);
  for (int i = 0; i < 5; ++i) {
    comp->OnEvent(ftxui::Event::ArrowRight);
  }
  EXPECT_FALSE(panel.on_shop_tab()) << "Right cannot walk past Etc";
}

TEST_F(InventoryPanelTest, TheShopTabArrivesAtItsLevel) {
  LevelTo(UnlockLevel(Feature::kShop) - 1);
  InventoryPanel panel(c_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  panel_focus_ = kInventoryPanel;
  ASSERT_EQ(RenderComponent(comp).find("Shop"), std::string::npos);

  LevelTo(UnlockLevel(Feature::kShop));
  EXPECT_NE(RenderComponent(comp).find("Shop"), std::string::npos);
  for (int i = 0; i < 5; ++i) {
    comp->OnEvent(ftxui::Event::ArrowRight);
  }
  EXPECT_TRUE(panel.on_shop_tab());
}

// The Shop tab lists nothing the player owns, so where the other tabs show a
// list it shows the way in.
TEST_F(InventoryPanelTest, ShopTabSaysHowToOpenTheShop) {
  LevelTo(UnlockLevel(Feature::kShop));
  InventoryPanel panel(c_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  panel_focus_ = kInventoryPanel;
  for (int i = 0; i < 3; ++i) {
    comp->OnEvent(ftxui::Event::ArrowRight);
  }
  EXPECT_TRUE(panel.on_shop_tab());
  EXPECT_NE(RenderComponent(comp).find("Hit Enter to open Shop"),
            std::string::npos);
}

// It is the last tab, so Right must stop there rather than walking off the bar.
TEST_F(InventoryPanelTest, ShopIsTheRightmostTab) {
  LevelTo(UnlockLevel(Feature::kShop));
  InventoryPanel panel(c_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  for (int i = 0; i < 6; ++i) {
    comp->OnEvent(ftxui::Event::ArrowRight);
  }
  EXPECT_TRUE(panel.on_shop_tab());
  EXPECT_NE(RenderComponent(comp).find("Shop"), std::string::npos);
}

// Down would leave the cursor nowhere: there is no list under this tab. The
// Etc stack matters -- without one, a Shop tab that fell through to the Etc
// emptiness check would look inert for the wrong reason.
TEST_F(InventoryPanelTest, DownDoesNotDescendIntoTheShopTab) {
  LevelTo(UnlockLevel(Feature::kShop));
  c_.AddStackable(MakeStackable("Shell", ITEM_CATEGORY_ETC, 7), 5);
  InventoryPanel panel(c_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  panel_focus_ = kInventoryPanel;
  for (int i = 0; i < 3; ++i) {
    comp->OnEvent(ftxui::Event::ArrowRight);
  }
  comp->OnEvent(ftxui::Event::ArrowDown);
  // Still on the tab bar, so Left still switches tabs rather than moving a row.
  comp->OnEvent(ftxui::Event::ArrowLeft);
  EXPECT_FALSE(panel.on_shop_tab());
}

// The shop is not a stackable tab, or the sell menu would open over it.
TEST_F(InventoryPanelTest, TheShopTabIsNotAStackableTab) {
  LevelTo(UnlockLevel(Feature::kShop));
  InventoryPanel panel(c_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  for (int i = 0; i < 3; ++i) {
    comp->OnEvent(ftxui::Event::ArrowRight);
  }
  EXPECT_FALSE(panel.on_stackable_tab());
}

TEST_F(InventoryPanelTest, ShowsTabBar) {
  InventoryPanel panel(c_, panel_focus_);
  std::string rendered = RenderComponent(panel.MakeComponent([]() {}));
  EXPECT_NE(rendered.find("Equip"), std::string::npos);
  EXPECT_NE(rendered.find("Use"), std::string::npos);
  EXPECT_NE(rendered.find("Etc"), std::string::npos);
}

TEST_F(InventoryPanelTest, ShowsMesoCounterWithCommas) {
  c_.AddMeso(1234567);
  InventoryPanel panel(c_, panel_focus_);
  EXPECT_NE(RenderComponent(panel.MakeComponent([]() {})).find("1,234,567"),
            std::string::npos);
}

TEST_F(InventoryPanelTest, UseTabListsUseStacksWithQuantity) {
  c_.AddStackable(MakeStackable("Red Potion", ITEM_CATEGORY_USE), 5);
  InventoryPanel panel(c_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  comp->OnEvent(ftxui::Event::ArrowRight);  // Equip -> Use
  std::string rendered = RenderComponent(comp);
  EXPECT_NE(rendered.find("Quantity"), std::string::npos);
  EXPECT_NE(rendered.find("Red Potion"), std::string::npos);
}

TEST_F(InventoryPanelTest, EtcTabShowsOnlyEtcStacks) {
  c_.AddStackable(MakeStackable("Red Potion", ITEM_CATEGORY_USE), 5);
  c_.AddStackable(MakeStackable("Snail Shell", ITEM_CATEGORY_ETC), 3);
  InventoryPanel panel(c_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  comp->OnEvent(ftxui::Event::ArrowRight);  // Equip -> Use
  comp->OnEvent(ftxui::Event::ArrowRight);  // Use -> Etc
  std::string rendered = RenderComponent(comp);
  EXPECT_NE(rendered.find("Snail Shell"), std::string::npos);
  EXPECT_EQ(rendered.find("Red Potion"), std::string::npos);
}

TEST_F(InventoryPanelTest, EmptyUseTabShowsPlaceholder) {
  InventoryPanel panel(c_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  comp->OnEvent(ftxui::Event::ArrowRight);  // Equip -> Use
  EXPECT_NE(RenderComponent(comp).find("(empty)"), std::string::npos);
}

// The Equip tab has never drawn its column names over an empty bag, and the
// stack tabs now match it: names label rows, so with no rows there is nothing
// for them to label.
TEST_F(InventoryPanelTest, EmptyUseTabShowsNoColumnHeader) {
  InventoryPanel panel(c_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  comp->OnEvent(ftxui::Event::ArrowRight);  // Equip -> Use
  EXPECT_EQ(RenderComponent(comp).find("Quantity"), std::string::npos);
}

TEST_F(InventoryPanelTest, EmptyEtcTabShowsNoColumnHeader) {
  c_.AddStackable(MakeStackable("Red Potion", ITEM_CATEGORY_USE), 5);
  InventoryPanel panel(c_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  comp->OnEvent(ftxui::Event::ArrowRight);  // Equip -> Use
  comp->OnEvent(ftxui::Event::ArrowRight);  // Use -> Etc
  std::string rendered = RenderComponent(comp);
  EXPECT_NE(rendered.find("(empty)"), std::string::npos);
  EXPECT_EQ(rendered.find("Quantity"), std::string::npos)
      << "the Use tab's stack must not leak its header onto the Etc tab";
}

TEST_F(InventoryPanelTest, UseTabCursorStartsOnFirstStack) {
  c_.AddStackable(MakeStackable("Red Potion", ITEM_CATEGORY_USE), 5);
  panel_focus_ = kInventoryPanel;
  InventoryPanel panel(c_, panel_focus_);
  // The stack list draws its cursor only while the panel holds focus, and this
  // test compares cursor_row() against where that cursor landed.
  panel_focus_ = kInventoryPanel;
  ftxui::Component comp = panel.MakeComponent([]() {});
  comp->OnEvent(ftxui::Event::ArrowRight);  // Equip -> Use
  comp->OnEvent(ftxui::Event::ArrowDown);   // tab bar -> stack list
  EXPECT_NE(RenderComponent(comp).find("> Red Potion"), std::string::npos);
}

TEST_F(InventoryPanelTest, UseTabCursorHiddenWhenPanelNotFocused) {
  c_.AddStackable(MakeStackable("Red Potion", ITEM_CATEGORY_USE), 5);
  panel_focus_ = kEquipPanel;
  InventoryPanel panel(c_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  comp->OnEvent(ftxui::Event::ArrowRight);  // Equip -> Use
  std::string rendered = RenderComponent(comp);
  EXPECT_NE(rendered.find("  Red Potion"), std::string::npos);
  EXPECT_EQ(rendered.find("> Red Potion"), std::string::npos);
}

TEST_F(InventoryPanelTest, UseTabCursorMovesWithArrowDown) {
  c_.AddStackable(MakeStackable("Red Potion", ITEM_CATEGORY_USE), 5);
  c_.AddStackable(MakeStackable("Blue Potion", ITEM_CATEGORY_USE), 3);
  panel_focus_ = kInventoryPanel;
  InventoryPanel panel(c_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  comp->OnEvent(ftxui::Event::ArrowRight);  // Equip -> Use
  comp->OnEvent(ftxui::Event::ArrowDown);   // tab bar -> first stack
  comp->OnEvent(ftxui::Event::ArrowDown);   // cursor -> second stack
  std::string rendered = RenderComponent(comp);
  EXPECT_NE(rendered.find("> Blue Potion"), std::string::npos);
  EXPECT_NE(rendered.find("  Red Potion"), std::string::npos);
}

TEST_F(InventoryPanelTest, SwitchingTabsResetsStackCursor) {
  c_.AddStackable(MakeStackable("Red Potion", ITEM_CATEGORY_USE), 5);
  c_.AddStackable(MakeStackable("Blue Potion", ITEM_CATEGORY_USE), 3);
  panel_focus_ = kInventoryPanel;
  InventoryPanel panel(c_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  comp->OnEvent(ftxui::Event::ArrowRight);  // Equip -> Use
  comp->OnEvent(ftxui::Event::ArrowDown);   // tab bar -> first stack
  comp->OnEvent(ftxui::Event::ArrowDown);   // cursor -> second stack
  comp->OnEvent(ftxui::Event::ArrowUp);     // -> first stack
  comp->OnEvent(ftxui::Event::ArrowUp);     // -> tab bar
  comp->OnEvent(ftxui::Event::ArrowRight);  // Use -> Etc
  comp->OnEvent(ftxui::Event::ArrowLeft);   // Etc -> Use, cursor reset
  comp->OnEvent(ftxui::Event::ArrowDown);   // tab bar -> first stack
  EXPECT_NE(RenderComponent(comp).find("> Red Potion"), std::string::npos);
}

TEST_F(InventoryPanelTest, UseTabEnterOpensMenuOnNonEmptyStack) {
  c_.AddStackable(MakeStackable("Red Potion", ITEM_CATEGORY_USE, 7), 5);
  InventoryPanel panel(c_, panel_focus_);
  bool opened = false;
  ftxui::Component comp = panel.MakeComponent([&opened]() { opened = true; });
  comp->OnEvent(ftxui::Event::ArrowRight);  // Equip -> Use
  comp->OnEvent(ftxui::Event::ArrowDown);   // tab bar -> stack list
  comp->OnEvent(ftxui::Event::Return);
  EXPECT_TRUE(opened);
}

TEST_F(InventoryPanelTest, EmptyTabEnterDoesNotOpenMenu) {
  InventoryPanel panel(c_, panel_focus_);
  bool opened = false;
  ftxui::Component comp = panel.MakeComponent([&opened]() { opened = true; });
  comp->OnEvent(ftxui::Event::ArrowRight);  // Equip -> Use (empty)
  comp->OnEvent(ftxui::Event::Return);
  EXPECT_FALSE(opened);
}

// Inspect leads, because looking at a thing is what you do before deciding
// what to do with it.
TEST_F(InventoryPanelTest, StackMenuOpensOnInspect) {
  c_.AddStackable(MakeStackable("Red Potion", ITEM_CATEGORY_USE, 7), 5);
  InventoryPanel panel(c_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  comp->OnEvent(ftxui::Event::ArrowRight);  // Equip -> Use
  panel.OpenMenu();
  ScrollPanel sp({});
  EXPECT_EQ(panel.menu().selected(), kStackInspect);
  EXPECT_EQ(panel.OnMenuEvent(ftxui::Event::Return, sp), kItemInspect);
}

// --- Use ---

// Etc is where drops and quest pieces sit; nothing there is drunk, so the
// entry is absent rather than permanently grey.
TEST_F(InventoryPanelTest, UseIsNotOfferedOnTheEtcTab) {
  c_.AddStackable(MakeStackable("Shell", ITEM_CATEGORY_ETC, 2), 5);
  InventoryPanel panel(c_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  comp->OnEvent(ftxui::Event::ArrowRight);  // Equip -> Use
  comp->OnEvent(ftxui::Event::ArrowRight);  // Use -> Etc
  panel.OpenMenu();
  std::vector<int> reachable = ReachableMenuEntries(panel.menu());
  EXPECT_EQ(std::count(reachable.begin(), reachable.end(), kStackUse), 0);
  EXPECT_EQ(RenderElement(panel.menu().Render(0, 0)).find("Use"),
            std::string::npos);
}

// On the Use tab it is there but grey, which is the answer to "can I drink
// this?" -- unlike Etc, where the question does not arise.
TEST_F(InventoryPanelTest, UseIsGreyedForAUseItemThatDoesNothing) {
  c_.AddStackable(MakeStackable("Odd Pebble", ITEM_CATEGORY_USE, 2), 5);
  InventoryPanel panel(c_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  comp->OnEvent(ftxui::Event::ArrowRight);  // Equip -> Use
  panel.OpenMenu();
  std::vector<int> reachable = ReachableMenuEntries(panel.menu());
  EXPECT_EQ(std::count(reachable.begin(), reachable.end(), kStackUse), 0);
  EXPECT_NE(RenderElement(panel.menu().Render(0, 0)).find("Use"),
            std::string::npos);
}

TEST_F(InventoryPanelTest, UsingAnItemAppliesItsEffectAndSpendsOne) {
  ItemPrototype potion = MakeStackable("Level-Up", ITEM_CATEGORY_USE);
  potion.set_effect(ITEM_EFFECT_LEVEL_UP);
  c_.AddStackable(potion, 3);
  InventoryPanel panel(c_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  comp->OnEvent(ftxui::Event::ArrowRight);  // Equip -> Use
  panel.OpenMenu();
  ScrollPanel sp({});
  panel.OnMenuEvent(ftxui::Event::ArrowDown, sp);  // Inspect -> Use
  EXPECT_EQ(panel.menu().selected(), kStackUse);
  EXPECT_EQ(panel.OnMenuEvent(ftxui::Event::Return, sp), kMain);

  EXPECT_EQ(c_.proto().level(), 2);
  EXPECT_EQ(c_.stackables(ITEM_CATEGORY_USE)[0].count(), 2);
}

TEST_F(InventoryPanelTest, StackMenuSellReturnsSellScreen) {
  c_.AddStackable(MakeStackable("Red Potion", ITEM_CATEGORY_USE, 7), 5);
  InventoryPanel panel(c_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  comp->OnEvent(ftxui::Event::ArrowRight);  // Equip -> Use
  panel.OpenMenu();
  ScrollPanel sp({});
  panel.OnMenuEvent(ftxui::Event::ArrowDown, sp);  // Inspect -> Sell
  EXPECT_EQ(panel.OnMenuEvent(ftxui::Event::Return, sp), kSell);
}

TEST_F(InventoryPanelTest, StackMenuCloseReturnsMain) {
  c_.AddStackable(MakeStackable("Red Potion", ITEM_CATEGORY_USE, 7), 5);
  InventoryPanel panel(c_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  comp->OnEvent(ftxui::Event::ArrowRight);  // Equip -> Use
  panel.OpenMenu();
  ScrollPanel sp({});
  panel.OnMenuEvent(ftxui::Event::ArrowDown, sp);  // Inspect -> Sell
  panel.OnMenuEvent(ftxui::Event::ArrowDown, sp);  // Sell -> Close
  EXPECT_EQ(panel.OnMenuEvent(ftxui::Event::Return, sp), kMain);
}

TEST_F(InventoryPanelTest, UnsellableStackDisablesSellOption) {
  c_.AddStackable(MakeStackable("Junk", ITEM_CATEGORY_ETC, 0), 5);
  InventoryPanel panel(c_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  comp->OnEvent(ftxui::Event::ArrowRight);  // -> Use
  comp->OnEvent(ftxui::Event::ArrowRight);  // -> Etc
  panel.OpenMenu();
  std::vector<int> reachable = ReachableMenuEntries(panel.menu());
  EXPECT_EQ(std::count(reachable.begin(), reachable.end(), kStackSell), 0);
  // Disabled rather than hidden: an item with no sale value is a fact about
  // the item, and the row saying so is the answer to "can I sell this?".
  EXPECT_NE(RenderElement(panel.menu().Render(0, 0)).find("Sell"),
            std::string::npos);
}

// The test screen is 20 rows, so a bag of 40 cannot fit and the list has to
// scroll rather than run off the bottom of the window.
TEST_F(InventoryPanelTest, KeepsTheCursorInViewWhenTheBagOverflows) {
  for (int i = 0; i < 40; ++i) {
    c_.PickUp(std::make_unique<EquipInstance>(sword_));
  }
  InventoryPanel panel(c_, panel_focus_);
  panel_focus_ = kInventoryPanel;
  ftxui::Component comp = panel.MakeComponent([]() {});
  comp->OnEvent(ftxui::Event::ArrowDown);  // tab bar -> item list
  RenderComponent(comp);
  for (int i = 0; i < 39; ++i) {
    comp->OnEvent(ftxui::Event::ArrowDown);
    // Rendered every step, because the frame scrolls at render time: walking
    // the whole way and only then looking would not say whether the view kept
    // up or merely caught up at the end.
    EXPECT_NE(RenderComponent(comp).find("> "), std::string::npos)
        << "the cursor left the window after " << i + 1 << " steps down";
  }
}

TEST_F(InventoryPanelTest, ShowsAScrollIndicatorOnlyWhenTheBagOverflows) {
  InventoryPanel small(c_, panel_focus_);
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  EXPECT_EQ(RenderComponent(small.MakeComponent([]() {})).find("┃"),
            std::string::npos)
      << "one item fits, so there is nothing to indicate";

  for (int i = 0; i < 40; ++i) {
    c_.PickUp(std::make_unique<EquipInstance>(sword_));
  }
  InventoryPanel big(c_, panel_focus_);
  EXPECT_NE(RenderComponent(big.MakeComponent([]() {})).find("┃"),
            std::string::npos)
      << "41 items do not fit, so how far down the list is should show";
}

// Use/Etc rows are plain text rather than an ftxui::Menu, so nothing marks the
// cursor for the frame unless the panel does it itself.
TEST_F(InventoryPanelTest, KeepsTheCursorInViewOnAStackableTab) {
  for (int i = 0; i < 40; ++i) {
    c_.AddStackable(
        MakeStackable("Etc " + std::to_string(i), ITEM_CATEGORY_ETC, 1), 1);
  }
  InventoryPanel panel(c_, panel_focus_);
  panel_focus_ = kInventoryPanel;
  ftxui::Component comp = panel.MakeComponent([]() {});
  comp->OnEvent(ftxui::Event::ArrowRight);  // -> Use
  comp->OnEvent(ftxui::Event::ArrowRight);  // -> Etc
  comp->OnEvent(ftxui::Event::ArrowDown);   // tab bar -> stack list
  for (int i = 0; i < 39; ++i) {
    comp->OnEvent(ftxui::Event::ArrowDown);
    EXPECT_NE(RenderComponent(comp).find("> "), std::string::npos)
        << "the cursor left the window after " << i + 1 << " steps down";
  }
}

// --- cursor_row ---

// What the item menu anchors to. It has to be where the cursor was actually
// drawn, not where the selected index says it should be.
TEST_F(InventoryPanelTest, CursorRowIsTheRowTheCursorWasDrawnOn) {
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  InventoryPanel panel(c_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  comp->OnEvent(ftxui::Event::ArrowDown);  // tab bar -> item list
  comp->OnEvent(ftxui::Event::ArrowDown);  // -> second item
  ftxui::Screen screen = RenderToScreen(comp);
  EXPECT_EQ(panel.cursor_row(), RowWithCursor(screen));
}

// The bug this replaced: past the point where the list scrolls, the selected
// index and the row on screen stop agreeing, and the old arithmetic followed
// the index. Walked one item at a time because the frame scrolls at render
// time -- stepping to the end and looking once cannot tell a cursor that kept
// up from one that merely caught up.
TEST_F(InventoryPanelTest, CursorRowFollowsAListThatHasScrolled) {
  FillBag(40);
  InventoryPanel panel(c_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  comp->OnEvent(ftxui::Event::ArrowDown);  // tab bar -> item list
  for (int i = 0; i < 39; ++i) {
    comp->OnEvent(ftxui::Event::ArrowDown);
    ftxui::Screen screen = RenderToScreen(comp);
    ASSERT_EQ(panel.cursor_row(), RowWithCursor(screen))
        << "cursor row wrong on item " << i + 1;
  }
  // And the two really did come apart, which is the whole point: the index is
  // well past the bottom of a twenty-row screen while the row it is drawn on
  // is still inside the window.
  EXPECT_GT(panel.selected(), panel.cursor_row());
  EXPECT_LT(panel.cursor_row(), 20);
}

// The equip tab draws a row two ways -- plain, or split into coloured cells
// when the character cannot equip it -- and the mark has to ride along with
// whichever is built. sword_ is level 10 and Warrior-only, so every test above
// takes the coloured path; this one is something a level-1 Beginner can wear.
TEST_F(InventoryPanelTest, CursorRowIsFoundOnAnItemTheCharacterCanEquip) {
  EquipPrototype plain;
  plain.set_name("Plain Sword");
  plain.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  plain.add_equip_job_categories(EQUIP_JOB_CATEGORY_UNIVERSAL);
  c_.PickUp(std::make_unique<EquipInstance>(plain));
  c_.PickUp(std::make_unique<EquipInstance>(plain));
  ASSERT_TRUE(c_.MeetsLevel(plain));
  ASSERT_TRUE(c_.MeetsJob(plain));
  InventoryPanel panel(c_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  comp->OnEvent(ftxui::Event::ArrowDown);  // tab bar -> item list
  comp->OnEvent(ftxui::Event::ArrowDown);  // -> second item
  ftxui::Screen screen = RenderToScreen(comp);
  EXPECT_EQ(panel.cursor_row(), RowWithCursor(screen));
}

// In the game the bag is not at the top of the screen -- the equipped panel
// sits above it. The row reported has to be the row on the SCREEN, so it has
// to carry that offset.
TEST_F(InventoryPanelTest, CursorRowIsAScreenRowNotARowWithinThePanel) {
  FillBag(40);
  InventoryPanel panel(c_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  comp->OnEvent(ftxui::Event::ArrowDown);  // tab bar -> item list
  for (int i = 0; i < 39; ++i) {
    comp->OnEvent(ftxui::Event::ArrowDown);
    ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80),
                                                 ftxui::Dimension::Fixed(20));
    // Three rows of something else above it, as the equipped panel is.
    ftxui::Render(screen, ftxui::vbox({
                              ftxui::text("above"),
                              ftxui::text("above"),
                              ftxui::text("above"),
                              comp->Render(),
                          }));
    ASSERT_EQ(panel.cursor_row(), RowWithCursor(screen))
        << "cursor row wrong on item " << i + 1;
  }
}

// The Use and Etc tabs hand-roll their rows rather than using ftxui::Menu, so
// they need marking of their own.
TEST_F(InventoryPanelTest, CursorRowFollowsTheStackListToo) {
  for (int i = 0; i < 30; ++i) {
    c_.AddStackable(
        MakeStackable("Potion " + std::to_string(i), ITEM_CATEGORY_USE), 1);
  }
  InventoryPanel panel(c_, panel_focus_);
  // The stack list draws its cursor only while the panel holds focus, and this
  // test compares cursor_row() against where that cursor landed.
  panel_focus_ = kInventoryPanel;
  ftxui::Component comp = panel.MakeComponent([]() {});
  comp->OnEvent(ftxui::Event::ArrowRight);  // Equip -> Use
  comp->OnEvent(ftxui::Event::ArrowDown);   // tab bar -> stack list
  for (int i = 0; i < 29; ++i) {
    comp->OnEvent(ftxui::Event::ArrowDown);
    ftxui::Screen screen = RenderToScreen(comp);
    ASSERT_EQ(panel.cursor_row(), RowWithCursor(screen))
        << "cursor row wrong on stack " << i + 1;
  }
  EXPECT_GT(panel.selected_stack(), panel.cursor_row());
}

// --- highlighting ---

// The bag arrives at level 4, and the gold border is what sends the player to
// it rather than leaving them to find the new panel themselves.
TEST_F(InventoryPanelTest, LightsItsBorderGoldWhenHighlighted) {
  InventoryPanel panel(c_, panel_focus_);
  ftxui::Component component = panel.MakeComponent([]() {});
  ASSERT_EQ(BorderColor(component->Render()), kTheme);
  panel.SetHighlighted(true);
  EXPECT_EQ(BorderColor(component->Render()), kYellow);
  panel.SetHighlighted(false);
  EXPECT_EQ(BorderColor(component->Render()), kTheme);
}

// Two rules here, from two different renderers: the one under the tab bar and
// the one under the stack list's column headers. Both have to come up gold, so
// this asks about every rule the panel drew rather than just the first.
TEST_F(InventoryPanelTest, LightsEveryInnerRuleGoldToo) {
  c_.AddStackable(MakeStackable("Red Potion", ITEM_CATEGORY_USE), 5);
  InventoryPanel panel(c_, panel_focus_);
  ftxui::Component component = panel.MakeComponent([]() {});
  panel_focus_ = kInventoryPanel;
  component->OnEvent(ftxui::Event::ArrowRight);  // onto the Use tab

  std::vector<ftxui::Color> rules = InnerRuleColors(component->Render());
  ASSERT_EQ(rules.size(), 2u) << "expected the tab rule and the list rule";
  EXPECT_EQ(rules[0], kTheme);
  EXPECT_EQ(rules[1], kTheme);

  panel.SetHighlighted(true);
  rules = InnerRuleColors(component->Render());
  ASSERT_EQ(rules.size(), 2u);
  EXPECT_EQ(rules[0], kYellow);
  EXPECT_EQ(rules[1], kYellow);
}

// --- a newly unlocked tab announces itself ---

// The gold outlives the four-second card: a player who was away when the shop
// opened still finds the tab saying it is new.
TEST_F(InventoryPanelTest, ANewShopTabIsWrittenInGold) {
  LevelTo(UnlockLevel(Feature::kShop));
  InventoryPanel panel(c_, panel_focus_);
  ftxui::Component component = panel.MakeComponent([]() {});
  EXPECT_EQ(LabelColor(component->Render(), "Shop"), kYellow);
  EXPECT_EQ(LabelColor(component->Render(), "Use"), kTheme)
      << "the tabs that were always there say nothing";
}

// Walking onto it is what puts it out -- the tab is "seen" when opened, not
// when it appears.
TEST_F(InventoryPanelTest, OpeningTheShopTabStopsItAnnouncingItself) {
  LevelTo(UnlockLevel(Feature::kShop));
  InventoryPanel panel(c_, panel_focus_);
  ftxui::Component component = panel.MakeComponent([]() {});
  panel_focus_ = kInventoryPanel;
  ASSERT_EQ(LabelColor(component->Render(), "Shop"), kYellow);

  // Equip -> Use -> Etc -> Shop.
  for (int i = 0; i < 3; ++i) {
    component->OnEvent(ftxui::Event::ArrowRight);
  }
  ASSERT_TRUE(panel.on_shop_tab()) << "the walk has to actually arrive";
  // Read unfocused: a focused active chip is black on white whatever its
  // history, which would hide the thing under test.
  panel_focus_ = kCharPanel;
  EXPECT_EQ(LabelColor(component->Render(), "Shop"), kTheme);
}

// And it stays put: the record is on the character, so it survives the panel
// being rebuilt -- which is what a relaunch amounts to.
TEST_F(InventoryPanelTest, AnOpenedShopTabStaysQuietForANewPanel) {
  LevelTo(UnlockLevel(Feature::kShop));
  c_.MarkTabSeen(kShopTabKey);
  InventoryPanel panel(c_, panel_focus_);
  ftxui::Component component = panel.MakeComponent([]() {});
  EXPECT_EQ(LabelColor(component->Render(), "Shop"), kTheme);
}

}  // namespace
}  // namespace ms
