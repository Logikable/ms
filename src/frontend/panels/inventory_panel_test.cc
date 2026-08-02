#include "src/frontend/panels/inventory_panel.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "src/frontend/types.h"
#include "src/frontend/widgets/panel_test_base.h"
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
};

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

  InventoryPanel panel(c_, panel_focus_);
  panel.OpenMenu();
  // Equip/Scroll/StarForce are disabled; only Inspect is selectable.
  EXPECT_EQ(panel.menu().selected(), kMenuInspect);
}

// Neither action can do anything to a throwing star, so the menu should not
// offer them. Scroll is the one that used to slip through: the picker always
// includes Clean Slate scrolls, so it opened on a list of scrolls that would
// have been refused.
TEST_F(InventoryPanelTest, ThrowingStarsOfferNoScrollOrStarForce) {
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
TEST_F(InventoryPanelTest, ASpentWeaponKeepsScrollAndStarForce) {
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

// The Shop tab lists nothing the player owns, so where the other tabs show a
// list it shows the way in.
TEST_F(InventoryPanelTest, ShopTabSaysHowToOpenTheShop) {
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

TEST_F(InventoryPanelTest, UseTabCursorStartsOnFirstStack) {
  c_.AddStackable(MakeStackable("Red Potion", ITEM_CATEGORY_USE), 5);
  panel_focus_ = kInventoryPanel;
  InventoryPanel panel(c_, panel_focus_);
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

TEST_F(InventoryPanelTest, SellMenuSellReturnsSellScreen) {
  c_.AddStackable(MakeStackable("Red Potion", ITEM_CATEGORY_USE, 7), 5);
  InventoryPanel panel(c_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  comp->OnEvent(ftxui::Event::ArrowRight);  // Equip -> Use
  panel.OpenMenu();
  ScrollPanel sp({});
  EXPECT_EQ(panel.OnMenuEvent(ftxui::Event::Return, panel_focus_, sp), kSell);
}

TEST_F(InventoryPanelTest, SellMenuCloseReturnsMain) {
  c_.AddStackable(MakeStackable("Red Potion", ITEM_CATEGORY_USE, 7), 5);
  InventoryPanel panel(c_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  comp->OnEvent(ftxui::Event::ArrowRight);  // Equip -> Use
  panel.OpenMenu();
  ScrollPanel sp({});
  panel.OnMenuEvent(ftxui::Event::ArrowDown, panel_focus_,
                    sp);  // Sell -> Close
  EXPECT_EQ(panel.OnMenuEvent(ftxui::Event::Return, panel_focus_, sp), kMain);
}

TEST_F(InventoryPanelTest, UnsellableStackDisablesSellOption) {
  c_.AddStackable(MakeStackable("Junk", ITEM_CATEGORY_ETC, 0), 5);
  InventoryPanel panel(c_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  comp->OnEvent(ftxui::Event::ArrowRight);  // -> Use
  comp->OnEvent(ftxui::Event::ArrowRight);  // -> Etc
  panel.OpenMenu();
  // Sell is disabled, so the cursor lands on Close.
  EXPECT_EQ(panel.menu().selected(), kSellClose);
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

}  // namespace
}  // namespace ms
