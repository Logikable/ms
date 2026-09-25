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
#include "src/frontend/testing/panel_test_base.h"
#include "src/frontend/testing/screen_text.h"
#include "src/frontend/types.h"
#include "src/frontend/widgets/chrome.h"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/text_columns.h"
#include "src/item/equip_instance.h"
#include "src/item/item.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"
#include "src/testing/prototypes.h"

namespace ms {
namespace {

class InventoryPanelTest : public PanelTest {
 protected:
  // A ScrollPanel holds its catalog by reference, so the catalog must outlive
  // it.
  const std::map<std::string, Scroll> no_scrolls_;

  // Every entry the player can land on, in the order Down visits them. Disabled
  // entries are skipped rather than just dimmed, so anything missing here is
  // something the menu doesn't offer.
  std::vector<int> ReachableMenuEntries(ItemMenu& menu) {
    std::vector<int> seen{menu.selected()};
    // The limit is deliberate: no menu in the game has anywhere near this many
    // entries, so a walk this long is going in circles, and without a limit a
    // wrong constant would hang the test.
    for (int step = 0; step < 32; ++step) {
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

  // Moves through the menu to `entry`, giving up after one full round, for the
  // same reason as ReachableMenuEntries.
  static bool StepTo(ItemMenu& menu, int entry) {
    for (int step = 0; step < 32; ++step) {
      if (menu.selected() == entry) {
        return true;
      }
      menu.Down();
    }
    return false;
  }

  EquipPrototype MakeThrowingStars() {
    EquipPrototype stars;
    stars.set_name("Subi Throwing-Stars");
    stars.set_equip_slot(EQUIP_SLOT_PROJECTILE);
    stars.set_equip_type(EQUIP_TYPE_THROWING_STAR);
    stars.add_equip_job_categories(EQUIP_JOB_CATEGORY_UNIVERSAL);
    stars.add_unsupported_upgrades(UPGRADE_SCROLL);
    stars.add_unsupported_upgrades(UPGRADE_STAR_FORCE);
    return stars;
  }

  ItemPrototype MakeStackable(const std::string& name, int sell_price = 0) {
    ItemPrototype proto;
    proto.set_name(name);
    proto.set_sell_price(sell_price);
    return proto;
  }

  ItemPrototype MakeToken(const std::string& name, const std::string& mark) {
    ItemPrototype proto = MakeStackable(name);
    proto.set_kind(ITEM_KIND_TOKEN);
    proto.set_currency_mark(mark);
    return proto;
  }

  // Named the way the catalog names a shard: in full, with the short form the
  // Token tab's column uses.
  ItemPrototype MakeShard(const std::string& boss) {
    ItemPrototype proto = MakeStackable(boss + "'s Soul Shard");
    proto.set_short_name(boss);
    proto.set_kind(ITEM_KIND_SOUL_SHARD);
    return proto;
  }

  // The same bounded screen RenderComponent uses, kept so a test can read
  // pixels rather than the joined string. The bound matters: the list really
  // does overflow and scroll at this size.
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
    EquipPrototype proto = sword_;
    for (int i = 0; i < count; ++i) {
      // Numbered, so a test can say which row the cursor is on, not just that
      // some row has it.
      proto.set_name("Item" + std::to_string(i));
      c_.PickUp(std::make_unique<EquipInstance>(proto));
    }
  }

  // The pixel where the first cell of `needle` lands, so a test can check both
  // its colour and whether it was dimmed.
  ftxui::Pixel PixelOfRendered(ftxui::Component component,
                               const std::string& needle) {
    return ms::PixelOf(RenderToScreen(std::move(component)), needle);
  }

  // The panel as one tab of a Container::Tab, as the game wires it. Other tests
  // call OnEvent directly and would miss a key that never gets dispatched.
  ftxui::Component InTabContainer(ftxui::Component panel) {
    return ftxui::Container::Tab({std::move(panel)}, &tab_selector_);
  }

  // Moves right along the bar until `tab` is open. Tests name the tab they want
  // instead of counting presses, so adding a tab doesn't break every test that
  // steps past it.
  void OpenTab(const ftxui::Component& comp, const InventoryPanel& panel,
               int tab) {
    for (int step = 0; step < kNumInventoryTabs && panel.active_tab() != tab;
         ++step) {
      comp->OnEvent(ftxui::Event::ArrowRight);
    }
  }

  int tab_selector_ = 0;
};

// The Equip tab turns gold to say something arrived in it, so it may only do
// that at an advancement that gives gear: the 1st and the 2nd.
TEST_F(InventoryPanelTest, TheEquipTabOnlyLightsForAnAdvancementThatGives) {
  InventoryPanel panel(c_, account_, panel_focus_);
  ftxui::Component root = panel.MakeComponent([]() {});
  c_.AdvanceJob(JOB_SWORDMAN);
  EXPECT_EQ(LabelColor(root->Render(), "Equip"), kYellow);
  account_.MarkSeen(EquipGiftTabKey(c_.proto().job_stage()));
  EXPECT_EQ(LabelColor(root->Render(), "Equip"), kTheme);

  c_.AdvanceJob(JOB_FIGHTER);
  EXPECT_EQ(LabelColor(root->Render(), "Equip"), kYellow);
  account_.MarkSeen(EquipGiftTabKey(c_.proto().job_stage()));

  // The 3rd and 4th open no slot and unlock no tier.
  c_.AdvanceJob(JOB_CRUSADER);
  EXPECT_EQ(LabelColor(root->Render(), "Equip"), kTheme);
  c_.AdvanceJob(JOB_HERO);
  EXPECT_EQ(LabelColor(root->Render(), "Equip"), kTheme);
}

// Container::Tab drops keys sent to a child that reports itself unfocusable,
// and the equip Menu does that when the bag is empty. That must not disable the
// tab bar, or a new character couldn't reach the Etc or Shop tabs.
TEST_F(InventoryPanelTest, TheTabBarStillSwitchesTabsOnAnEmptyBag) {
  InventoryPanel panel(c_, account_, panel_focus_);
  panel_focus_ = kInventoryPanel;
  ftxui::Component root = InTabContainer(panel.MakeComponent([]() {}));
  ASSERT_EQ(c_.inventory().size(), 0);
  root->OnEvent(ftxui::Event::ArrowRight);
  EXPECT_EQ(panel.active_tab(), kTokenTab) << "Equip -> Token";
  root->OnEvent(ftxui::Event::ArrowLeft);
  EXPECT_EQ(panel.active_tab(), kEquipTab) << "Token -> Equip";
}

// Equipping the last item in the bag removes the row the cursor was on. The
// panel keeps focus, so the cursor has to move somewhere visible.
TEST_F(InventoryPanelTest, TheCursorLeavesAListThatEmptiedUnderIt) {
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  InventoryPanel panel(c_, account_, panel_focus_);
  panel_focus_ = kInventoryPanel;
  ftxui::Component comp = panel.MakeComponent([]() {});
  comp->OnEvent(ftxui::Event::ArrowDown);  // tab bar -> the one item
  RenderComponent(comp);
  c_.Equip(0);
  RenderComponent(comp);
  comp->OnEvent(ftxui::Event::ArrowRight);
  EXPECT_EQ(panel.active_tab(), kTokenTab)
      << "Right switched tabs, so the cursor is back on the tab bar";
}

// --- the tab bar and the list are one ring ---

// The bar and the rows form one ring, so Up from the bar lands on the last row.
TEST_F(InventoryPanelTest, ArrowUpFromTheTabBarLandsOnTheLastRow) {
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  panel_focus_ = kInventoryPanel;
  InventoryPanel panel(c_, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  RenderComponent(comp);
  comp->OnEvent(ftxui::Event::ArrowUp);
  EXPECT_EQ(panel.selected(), 1) << "the second and last row";
}

// The caret is drawn from selected_, not ftxui's focused entry. The tab-bar
// jumps are the moves the Menu never sees; on a short list both indices sit at
// 0 and agree by chance.
TEST_F(InventoryPanelTest, TheCaretShowsOnArrivalFromTheTabBar) {
  FillBag(25);
  panel_focus_ = kInventoryPanel;
  InventoryPanel panel(c_, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  RenderComponent(comp);

  comp->OnEvent(ftxui::Event::ArrowUp);  // the bar -> the last row
  ASSERT_EQ(panel.selected(), 24);
  EXPECT_NE(RenderComponentText(comp).find("> Item24"), std::string::npos)
      << "no caret after wrapping up onto the last row";
}

TEST_F(InventoryPanelTest, TheCaretShowsOnReturnToTheFirstRow) {
  FillBag(25);
  panel_focus_ = kInventoryPanel;
  InventoryPanel panel(c_, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  RenderComponent(comp);

  // Moved down rather than wrapped, so the Menu handles every step and its own
  // focused row follows the cursor to the bottom.
  comp->OnEvent(ftxui::Event::ArrowDown);
  for (int i = 0; i < 24; ++i) {
    comp->OnEvent(ftxui::Event::ArrowDown);
    RenderComponent(comp);
  }
  ASSERT_EQ(panel.selected(), 24);
  comp->OnEvent(ftxui::Event::ArrowDown);  // off the bottom -> the bar
  RenderComponent(comp);
  comp->OnEvent(ftxui::Event::ArrowDown);  // the bar -> the first row

  ASSERT_EQ(panel.selected(), 0);
  EXPECT_NE(RenderComponentText(comp).find("> Item0"), std::string::npos)
      << "no caret after coming back round to the first row";
}

// --- the band under the cursor ---

// The caret shows where the cursor is, and the band shows how far the row
// reaches, so a stat eight columns away can be traced back to its name. The
// band has to cross the whole panel, borders excluded.
TEST_F(InventoryPanelTest, TheSelectedRowWearsABandAcrossThePanel) {
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  panel_focus_ = kInventoryPanel;
  InventoryPanel panel(c_, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  comp->OnEvent(ftxui::Event::ArrowDown);  // tab bar -> the first item
  ftxui::Screen screen = RenderToScreen(comp);

  std::vector<BandSpan> bands = BandSpans(screen, kSelectedRow);
  ASSERT_EQ(bands.size(), 1u) << "one row is selected, so one row is banded";
  EXPECT_EQ(bands[0].y, RowWithCursor(screen)) << "the band is under the caret";
  EXPECT_EQ(bands[0].first, 1) << "starts in the column inside the left border";
  // Two columns short of the right border, not one: the list reserves the
  // innermost column for its scroll bar, which isn't part of the row.
  EXPECT_EQ(bands[0].last, screen.dimx() - 3) << "and runs to the scroll bar";
}

// Drawn under the same condition as the caret, so the two always agree. A band
// on a list the arrows can't reach would show a selection that can't move.
TEST_F(InventoryPanelTest, TheBandGoesWhereverTheCaretGoes) {
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  panel_focus_ = kInventoryPanel;
  InventoryPanel panel(c_, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  comp->OnEvent(ftxui::Event::ArrowDown);  // tab bar -> the one item
  ASSERT_EQ(BandSpans(RenderToScreen(comp), kSelectedRow).size(), 1u);

  comp->OnEvent(ftxui::Event::ArrowDown);  // off the bottom -> the tab bar
  EXPECT_TRUE(BandSpans(RenderToScreen(comp), kSelectedRow).empty())
      << "the cursor is up on the bar";

  comp->OnEvent(ftxui::Event::ArrowDown);  // back onto the row
  panel_focus_ = kEquipPanel;
  EXPECT_TRUE(BandSpans(RenderToScreen(comp), kSelectedRow).empty())
      << "another panel holds focus";
}

// The stack tabs draw their own rows instead of using an ftxui::Menu, so their
// band is separate code and needs its own test.
TEST_F(InventoryPanelTest, AStackRowWearsTheBandToo) {
  c_.AddItem(MakeStackable("Mixed Block"), 5);
  panel_focus_ = kInventoryPanel;
  InventoryPanel panel(c_, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  OpenTab(comp, panel, kEtcTab);
  comp->OnEvent(ftxui::Event::ArrowDown);  // tab bar -> the one stack

  ftxui::Screen screen = RenderToScreen(comp);
  std::vector<BandSpan> bands = BandSpans(screen, kSelectedRow);
  ASSERT_EQ(bands.size(), 1u);
  EXPECT_EQ(bands[0].y, RowWithCursor(screen));
  EXPECT_EQ(bands[0].first, 1);
  EXPECT_EQ(bands[0].last, screen.dimx() - 3);
}

TEST_F(InventoryPanelTest, DownFromTheLastItemReturnsToTheBar) {
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  panel_focus_ = kInventoryPanel;
  InventoryPanel panel(c_, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  comp->OnEvent(ftxui::Event::ArrowDown);  // tab bar -> the one item
  ASSERT_NE(RenderComponentText(comp).find("> Sword"), std::string::npos);

  comp->OnEvent(ftxui::Event::ArrowDown);  // off the bottom -> the tab bar
  // The cursor is drawn only in the list zone, so its absence shows where the
  // cursor went. Left still switching tabs confirms it.
  EXPECT_EQ(RenderComponentText(comp).find("> Sword"), std::string::npos);
  comp->OnEvent(ftxui::Event::ArrowRight);
  EXPECT_EQ(panel.active_tab(), kTokenTab);
}

TEST_F(InventoryPanelTest, ArrowUpFromTheTabBarLandsOnTheLastStack) {
  c_.AddItem(MakeStackable("Red Shell"), 5);
  c_.AddItem(MakeStackable("Blue Shell"), 3);
  panel_focus_ = kInventoryPanel;
  InventoryPanel panel(c_, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  OpenTab(comp, panel, kEtcTab);
  comp->OnEvent(ftxui::Event::ArrowUp);  // the bar -> the last stack
  EXPECT_NE(RenderComponentText(comp).find("> Blue Shell"), std::string::npos);
}

TEST_F(InventoryPanelTest, DownFromTheLastStackReturnsToTheBar) {
  c_.AddItem(MakeStackable("Red Shell"), 5);
  panel_focus_ = kInventoryPanel;
  InventoryPanel panel(c_, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  OpenTab(comp, panel, kEtcTab);
  comp->OnEvent(ftxui::Event::ArrowDown);  // tab bar -> the one stack
  ASSERT_NE(RenderComponentText(comp).find("> Red Shell"), std::string::npos);

  comp->OnEvent(ftxui::Event::ArrowDown);
  EXPECT_EQ(RenderComponentText(comp).find("> Red Shell"), std::string::npos);
}

// A tab with nothing under it forms a ring of the bar and the buttons, so
// neither key lands on a row that isn't drawn.
TEST_F(InventoryPanelTest, TheEmptyTabRingIsTheBarAndTheButtons) {
  panel_focus_ = kInventoryPanel;
  InventoryPanel panel(c_, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  ASSERT_EQ(c_.inventory().size(), 0);
  comp->OnEvent(ftxui::Event::ArrowUp);    // the bar -> the buttons
  comp->OnEvent(ftxui::Event::ArrowDown);  // and back
  // Back on the bar, so Right switches tabs instead of moving a row.
  comp->OnEvent(ftxui::Event::ArrowRight);
  EXPECT_EQ(panel.active_tab(), kTokenTab);
}

// --- the Token tab ---

// The tab sits between Equip and Etc from the start, whether or not the bag
// holds a currency yet. If tabs came and went with what the player carried, the
// others would shift under their hand.
TEST_F(InventoryPanelTest, TheTokenTabStandsInTheBarFromTheStart) {
  panel_focus_ = kInventoryPanel;
  InventoryPanel panel(c_, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  ASSERT_TRUE(c_.stackables().empty());
  EXPECT_NE(RenderComponentText(comp).find("Token"), std::string::npos);
  comp->OnEvent(ftxui::Event::ArrowRight);
  EXPECT_EQ(panel.active_tab(), kTokenTab) << "Equip -> Token";
  // Empty, and it says so instead of heading two columns with no rows.
  std::string text = RenderComponentText(comp);
  EXPECT_NE(text.find("empty"), std::string::npos);
  EXPECT_EQ(text.find("Soul Shard"), std::string::npos);

  comp->OnEvent(ftxui::Event::ArrowRight);
  EXPECT_TRUE(panel.on_stackable_tab()) << "Token -> Etc";
  comp->OnEvent(ftxui::Event::ArrowLeft);
  EXPECT_EQ(panel.active_tab(), kTokenTab) << "Etc -> Token";
  comp->OnEvent(ftxui::Event::ArrowLeft);
  EXPECT_EQ(panel.active_tab(), kEquipTab) << "Token -> Equip";
}

// A balance sheet rather than a list: neither key moves into it, so the cursor
// stays on the bar and the arrows keep switching tabs.
TEST_F(InventoryPanelTest, TheTokenTabTakesNoCursor) {
  c_.AddItem(MakeToken("Frozen Weapon Token", "●"), 3);
  panel_focus_ = kInventoryPanel;
  InventoryPanel panel(c_, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  comp->OnEvent(ftxui::Event::ArrowRight);
  ASSERT_EQ(panel.active_tab(), kTokenTab);
  comp->OnEvent(ftxui::Event::ArrowDown);
  EXPECT_TRUE(panel.on_tab_bar()) << "Down scrolls the sheet, not the cursor";
  EXPECT_EQ(RenderComponentText(comp).find("> Frozen"), std::string::npos);
  comp->OnEvent(ftxui::Event::ArrowUp);
  EXPECT_TRUE(panel.on_tab_bar());
  comp->OnEvent(ftxui::Event::ArrowRight);
  EXPECT_TRUE(panel.on_stackable_tab()) << "the bar still has the arrows";
}

// Two columns side by side, each with its counts. A shard goes by its short
// name, since its column heading says the rest.
TEST_F(InventoryPanelTest, TheTokenTabDrawsBothColumns) {
  c_.AddItem(MakeToken("Frozen Weapon Token", "●"), 3);
  c_.AddItem(MakeShard("Zakum"), 47);
  panel_focus_ = kInventoryPanel;
  InventoryPanel panel(c_, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  comp->OnEvent(ftxui::Event::ArrowRight);
  std::string text = RenderComponentText(comp);
  EXPECT_NE(text.find("Soul Shard"), std::string::npos) << "the heading";
  EXPECT_NE(text.find("Frozen Weapon Token"), std::string::npos);
  EXPECT_NE(text.find("Zakum"), std::string::npos);
  EXPECT_EQ(text.find("Zakum's Soul Shard"), std::string::npos)
      << "the column already says Soul Shard";
  EXPECT_NE(text.find("47"), std::string::npos);
}

// The currencies moved out of Etc, which keeps the ordinary drops. The spell
// trace is on neither tab: it is a balance in the bar.
TEST_F(InventoryPanelTest, EtcKeepsOnlyTheOrdinaryDrops) {
  c_.AddItem(MakeToken("Frozen Weapon Token", "●"), 3);
  c_.AddItem(MakeShard("Zakum"), 47);
  ItemPrototype trace = MakeStackable(kSpellTraceName);
  trace.set_kind(ITEM_KIND_SPELL_TRACE);
  c_.AddItem(trace, 900);
  c_.AddItem(MakeStackable("Red Shell"), 5);
  panel_focus_ = kInventoryPanel;
  InventoryPanel panel(c_, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  comp->OnEvent(ftxui::Event::ArrowRight);
  comp->OnEvent(ftxui::Event::ArrowRight);
  ASSERT_TRUE(panel.on_stackable_tab());
  std::string text = RenderComponentText(comp);
  EXPECT_NE(text.find("Red Shell"), std::string::npos);
  EXPECT_EQ(text.find("Frozen Weapon Token"), std::string::npos);
  EXPECT_EQ(text.find("Zakum"), std::string::npos);
  EXPECT_EQ(text.find(kSpellTraceName), std::string::npos);

  // The cursor on its one row names that stack, not the fourth item the bag
  // holds.
  comp->OnEvent(ftxui::Event::ArrowDown);
  EXPECT_EQ(c_.stackables()[panel.selected_stack()].name(), "Red Shell");
}

// With no cursor to move, Up and Down scroll the sheet one row per press, no
// further than the last row. Sixteen shards on a panel that shows a handful is
// what the keys are for.
TEST_F(InventoryPanelTest, UpAndDownScrollTheTokenSheet) {
  for (const std::string& boss :
       {"Arkarium", "Crimson Queen", "Cygnus", "Damien", "Hilla", "Horntail",
        "Lotus", "Magnus", "Papulatus", "Pierre", "Pink Bean", "Princess No",
        "Vellum", "Von Bon", "Zakum"}) {
    c_.AddItem(MakeShard(boss), 20);
  }
  panel_focus_ = kInventoryPanel;
  InventoryPanel panel(c_, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  comp->OnEvent(ftxui::Event::ArrowRight);
  ASSERT_EQ(panel.active_tab(), kTokenTab);
  ASSERT_NE(RenderComponentText(comp).find("Arkarium"), std::string::npos);

  // One press, one row: the first row scrolls off and the next one leads.
  comp->OnEvent(ftxui::Event::ArrowDown);
  std::string text = RenderComponentText(comp);
  EXPECT_EQ(text.find("Arkarium"), std::string::npos);
  EXPECT_NE(text.find("Crimson Queen"), std::string::npos);

  // Down past the end stops at the last row instead of scrolling past it, so
  // there are no wasted presses on the way back up.
  for (int i = 0; i < 40; ++i) {
    comp->OnEvent(ftxui::Event::ArrowDown);
  }
  ASSERT_NE(RenderComponentText(comp).find("Zakum"), std::string::npos);
  comp->OnEvent(ftxui::Event::ArrowUp);
  EXPECT_NE(RenderComponentText(comp).find("Von Bon"), std::string::npos);

  // Leaving the tab and coming back opens it at the top again.
  comp->OnEvent(ftxui::Event::ArrowRight);
  comp->OnEvent(ftxui::Event::ArrowLeft);
  EXPECT_NE(RenderComponentText(comp).find("Arkarium"), std::string::npos);
}

// The Token sheet is sorted as items are added, so both columns read from most
// to fewest without pressing Sort. The tab still offers Sort, which leaves a
// sorted sheet unchanged.
TEST_F(InventoryPanelTest, TheTokenSheetIsAlwaysFiled) {
  c_.AddItem(MakeToken("AbsoLab Coin", "◆"), 2);
  c_.AddItem(MakeToken("Frozen Weapon Token", "●"), 9);
  c_.AddItem(MakeShard("Hilla"), 1);
  c_.AddItem(MakeShard("Zakum"), 8);
  panel_focus_ = kInventoryPanel;
  InventoryPanel panel(c_, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  comp->OnEvent(ftxui::Event::ArrowRight);
  ASSERT_EQ(panel.active_tab(), kTokenTab);
  panel.OpenTabMenu();
  panel.OnTabMenuEvent(ftxui::Event::Return);

  std::string text = RenderComponentText(comp);
  EXPECT_LT(text.find("Frozen Weapon Token"), text.find("AbsoLab Coin"));
  EXPECT_LT(text.find("Zakum"), text.find("Hilla"));
}

// --- the Expand tab ---

// Its label is the state Enter would leave the bag in, and it stays at the far
// right of the bar whether or not the shop has added a fourth tab.
TEST_F(InventoryPanelTest, TheExpandTabReadsTheStateItWouldLeave) {
  InventoryPanel panel(c_, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  EXPECT_NE(RenderComponent(comp).find("Expand"), std::string::npos);
  panel.SetExpanded(true);
  std::string screen = RenderComponent(comp);
  EXPECT_NE(screen.find("Close"), std::string::npos);
  EXPECT_EQ(screen.find("Expand"), std::string::npos);
}

// A door rather than a page: selecting it shows a line saying so instead of a
// list, and Enter goes through instead of opening a menu.
TEST_F(InventoryPanelTest, TheExpandTabSaysWhatEnterWouldDo) {
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  panel_focus_ = kInventoryPanel;
  InventoryPanel panel(c_, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  RenderComponent(comp);
  OpenTab(comp, panel, kEtcTab);
  comp->OnEvent(ftxui::Event::ArrowRight);  // Etc -> Expand
  std::string screen = RenderComponent(comp);
  EXPECT_NE(screen.find("Hit Enter to fullscreen Inventory"),
            std::string::npos);
  EXPECT_EQ(screen.find("Sword"), std::string::npos)
      << "the door stands in front of the list, as the Shop tab does";
}

TEST_F(InventoryPanelTest, EnterOnTheExpandTabCallsBack) {
  panel_focus_ = kInventoryPanel;
  InventoryPanel panel(c_, account_, panel_focus_);
  int expands = 0;
  ftxui::Component comp =
      panel.MakeComponent([]() {}, [&expands]() { ++expands; });
  OpenTab(comp, panel, kEtcTab);
  comp->OnEvent(ftxui::Event::ArrowRight);  // Etc -> Expand
  comp->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(expands, 1);
  EXPECT_EQ(panel.active_tab(), kEquipTab)
      << "the wide bag opens on the first tab, not on the door";
  comp->OnEvent(ftxui::Event::Return);
  EXPECT_EQ(expands, 1) << "Enter on a page raises a menu, not the fullscreen";
}

// The bar wraps through Expand: Right from it goes to Equip, and Left from
// Equip goes back to it.
TEST_F(InventoryPanelTest, TheExpandTabClosesTheRing) {
  c_.AddItem(MakeStackable("Ore"), 1);
  panel_focus_ = kInventoryPanel;
  InventoryPanel panel(c_, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  OpenTab(comp, panel, kEtcTab);
  comp->OnEvent(ftxui::Event::ArrowRight);  // Etc -> Expand
  // The chip under the cursor is drawn white, so only one chip can be.
  EXPECT_EQ(PixelOfRendered(comp, "Expand").background_color,
            ftxui::Color::White);
  EXPECT_NE(PixelOfRendered(comp, "Etc").background_color, ftxui::Color::White)
      << "the highlight is in one place, not two";
  comp->OnEvent(ftxui::Event::ArrowRight);  // Expand -> Equip, coming round
  EXPECT_EQ(panel.active_tab(), kEquipTab);
  EXPECT_EQ(RenderComponent(comp).find("Hit Enter to fullscreen Inventory"),
            std::string::npos)
      << "the list is back";
  comp->OnEvent(ftxui::Event::ArrowLeft);  // Equip -> Expand again
  EXPECT_EQ(PixelOfRendered(comp, "Expand").background_color,
            ftxui::Color::White);
}

// --- the tab menu ---

// Sort acts on the tab the player is viewing, and opening the menu doesn't move
// them off it.
TEST_F(InventoryPanelTest, SortFilesTheEquipTab) {
  EquipPrototype wearable = sword_;
  wearable.set_name("Zzz Club");
  wearable.set_required_level(1);
  wearable.clear_equip_job_categories();
  wearable.add_equip_job_categories(EQUIP_JOB_CATEGORY_UNIVERSAL);
  sword_.set_name("Aaa Gated Sword");
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  c_.PickUp(std::make_unique<EquipInstance>(wearable));
  panel_focus_ = kInventoryPanel;
  InventoryPanel panel(c_, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  ASSERT_EQ(c_.inventory()[0].name(), "Aaa Gated Sword");

  ScrollPanel sp(c_, no_scrolls_);
  panel.OpenTabMenu();
  panel.OnTabMenuEvent(ftxui::Event::Return);  // Sort, the first entry
  EXPECT_EQ(c_.inventory()[0].name(), "Zzz Club")
      << "what can be worn files above what cannot";
  EXPECT_EQ(panel.active_tab(), kEquipTab);
}

TEST_F(InventoryPanelTest, SortFilesAStackTab) {
  c_.AddItem(MakeStackable("Zzz Shell"), 2);
  c_.AddItem(MakeStackable("Aaa Shell"), 40);
  panel_focus_ = kInventoryPanel;
  InventoryPanel panel(c_, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  OpenTab(comp, panel, kEtcTab);
  panel.OpenTabMenu();
  panel.OnTabMenuEvent(ftxui::Event::Return);
  EXPECT_EQ(c_.stackables()[0].name(), "Aaa Shell")
      << "the larger stack files first";
}

// The shop is a door rather than a list, so Enter goes through it instead of
// opening a menu about a page with nothing to sort.
TEST_F(InventoryPanelTest, TheShopTabIsEnteredNotAskedAbout) {
  LevelTo(UnlockLevel(Feature::kShop));
  panel_focus_ = kInventoryPanel;
  InventoryPanel panel(c_, account_, panel_focus_);
  bool opened = false;
  ftxui::Component comp = panel.MakeComponent([&opened]() { opened = true; });
  for (int i = 0; i < 2; ++i) {
    OpenTab(comp, panel, kShopTab);
  }
  comp->OnEvent(ftxui::Event::Return);
  EXPECT_TRUE(opened);
  EXPECT_TRUE(panel.on_shop_tab())
      << "the controller reads this to send Enter to the shop";
}

TEST_F(InventoryPanelTest, ShowsEmptyWhenBagIsEmpty) {
  InventoryPanel panel(c_, account_, panel_focus_);
  EXPECT_NE(RenderComponent(panel.MakeComponent([]() {})).find("(empty)"),
            std::string::npos);
}

// sword_ is a level 10 warrior weapon, so one row fills every column.
TEST_F(InventoryPanelTest, ARowNamesTheItemAndItsColumns) {
  sword_.set_upgrade_slots(7);
  UnlockEverything();
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  InventoryPanel panel(c_, account_, panel_focus_);
  // Wide enough for every column at once. At the right column's minimum width,
  // the job column is one of the two the list drops.
  panel.SetWidth(kTestScreenWidth - 2);
  std::string drawn = RenderComponent(panel.MakeComponent([]() {}));
  EXPECT_NE(drawn.find("Sword"), std::string::npos);
  EXPECT_NE(drawn.find("Weapon"), std::string::npos);
  EXPECT_NE(drawn.find("Lv10"), std::string::npos);
  EXPECT_NE(drawn.find("Warrior"), std::string::npos);
  // A fresh item: no scrolls passed, no stars.
  EXPECT_NE(drawn.find("0/7"), std::string::npos);
  EXPECT_NE(drawn.find("0\u2605"), std::string::npos);
}

// A row whose item can't be worn is dimmed as a whole, as the skills tab dims a
// skill that can't be learned, while the cells that explain why stay bright
// red. Dimming them too would hide the one thing on the row worth reading.
TEST_F(InventoryPanelTest, AnUnwearableRowDimsAndItsReasonStaysRed) {
  // sword_ is level 10 and Warrior only, and c_ is a level 1 Beginner, so both
  // cells are red.
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  panel_focus_ = kInventoryPanel;
  InventoryPanel panel(c_, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});

  EXPECT_TRUE(PixelOfRendered(comp, "Sword").dim) << "the name";

  ftxui::Pixel level = PixelOfRendered(comp, "Lv10");
  EXPECT_EQ(level.foreground_color, kRed);
  EXPECT_FALSE(level.dim) << "the reason must not be muted";

  ftxui::Pixel job = PixelOfRendered(comp, "Warrior");
  EXPECT_EQ(job.foreground_color, kRed);
  EXPECT_FALSE(job.dim);
}

// A row that can be worn is not dimmed, so dimming keeps its meaning.
TEST_F(InventoryPanelTest, AWearableRowIsNotDimmed) {
  EquipPrototype wearable;
  wearable.set_name("Plain Cape");
  wearable.set_equip_slot(EQUIP_SLOT_CAPE);
  wearable.set_required_level(1);
  wearable.add_equip_job_categories(EQUIP_JOB_CATEGORY_UNIVERSAL);
  c_.PickUp(std::make_unique<EquipInstance>(wearable));
  panel_focus_ = kInventoryPanel;
  InventoryPanel panel(c_, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});

  ftxui::Pixel name = PixelOfRendered(comp, "Plain Cape");
  EXPECT_FALSE(name.dim);
  EXPECT_NE(name.foreground_color, kRed);
}

TEST_F(InventoryPanelTest, ShowsSelectionCursorInListZone) {
  panel_focus_ = kInventoryPanel;
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  InventoryPanel panel(c_, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  comp->OnEvent(ftxui::Event::ArrowDown);  // tab bar -> item list
  EXPECT_NE(RenderComponentText(comp).find("> Sword"), std::string::npos);
}

TEST_F(InventoryPanelTest, NoSelectionCursorOnTheTabRow) {
  panel_focus_ = kInventoryPanel;
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  InventoryPanel panel(c_, account_, panel_focus_);
  // The panel opens on the tab row, so the list cursor is hidden until Down.
  EXPECT_EQ(RenderComponent(panel.MakeComponent([]() {})).find("> Sword"),
            std::string::npos);
}

// The same rule the Etc tab already follows: a caret on an unfocused panel
// would suggest the keys go there.
TEST_F(InventoryPanelTest, EquipTabCursorHiddenWhenPanelNotFocused) {
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  panel_focus_ = kInventoryPanel;
  InventoryPanel panel(c_, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  comp->OnEvent(ftxui::Event::ArrowDown);  // tab bar -> item list
  ASSERT_NE(RenderComponentText(comp).find("> Sword"), std::string::npos);

  panel_focus_ = kEquipPanel;
  EXPECT_EQ(RenderComponentText(comp).find("> Sword"), std::string::npos);
}

// The upgrade columns appear with their mechanics, so a player who has never
// scrolled doesn't see an empty Scroll column.
TEST_F(InventoryPanelTest, ShowsColumnHeader) {
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  InventoryPanel locked(c_, account_, panel_focus_);
  std::string rendered = RenderComponent(locked.MakeComponent([]() {}));
  EXPECT_NE(rendered.find("Name"), std::string::npos);
  EXPECT_NE(rendered.find("Equip Slot"), std::string::npos);
  EXPECT_NE(rendered.find("Level"), std::string::npos);
  EXPECT_NE(rendered.find("Job"), std::string::npos);
  EXPECT_EQ(rendered.find("Scroll"), std::string::npos);
  EXPECT_EQ(rendered.find("Stars"), std::string::npos);
  EXPECT_EQ(rendered.find("Potential"), std::string::npos);

  UnlockEverything();
  InventoryPanel open(c_, account_, panel_focus_);
  rendered = RenderComponent(open.MakeComponent([]() {}));
  EXPECT_NE(rendered.find("Scroll"), std::string::npos);
  EXPECT_NE(rendered.find("Stars"), std::string::npos);
  EXPECT_NE(rendered.find("Potential"), std::string::npos);
}

TEST_F(InventoryPanelTest, ShowsAllForUniversalItem) {
  EquipPrototype axe;
  axe.set_name("Axe");
  axe.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  axe.add_equip_job_categories(EQUIP_JOB_CATEGORY_UNIVERSAL);
  c_.PickUp(std::make_unique<EquipInstance>(axe));
  InventoryPanel panel(c_, account_, panel_focus_);
  EXPECT_NE(RenderComponent(panel.MakeComponent([]() {})).find("All"),
            std::string::npos);
}

TEST_F(InventoryPanelTest, TraceMenuDisablesAllExceptInspect) {
  // Destroy an item with star force to put a trace in the bag.
  EquipPrototype proto;
  proto.set_name("Sword");
  proto.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  proto.set_required_level(138);
  Equip state;
  state.set_stars(19);
  c_.PickUp(std::make_unique<EquipInstance>(proto, state));
  c_.AddMeso(1'000'000'000'000);  // a hundred attempts at nine figures each
  bool saw_destroy = false;
  for (int i = 0; i < 100 && !saw_destroy; ++i) {
    if (c_.StarForceInventory(0) == kStarForceDestroy) {
      saw_destroy = true;
    }
  }
  ASSERT_TRUE(saw_destroy);

  // High enough that every upgrade entry would be on the menu for an ordinary
  // item, so anything disabled here is because of the trace, not the level.
  LevelTo(UnlockLevel(Feature::kStarForce));
  InventoryPanel panel(c_, account_, panel_focus_);
  panel.OpenMenu();
  // Recover is offered on a trace, so Inspect, Recover and Sell remain. Read
  // the selection before walking the menu: ReachableMenuEntries moves it.
  EXPECT_EQ(panel.menu().selected(), kMenuInspect);
  std::vector<int> reachable = ReachableMenuEntries(panel.menu());
  EXPECT_EQ(std::count(reachable.begin(), reachable.end(), kMenuAction), 0);
  EXPECT_EQ(std::count(reachable.begin(), reachable.end(), kMenuScroll), 0);
  EXPECT_EQ(std::count(reachable.begin(), reachable.end(), kMenuStarForce), 0);
  EXPECT_NE(std::count(reachable.begin(), reachable.end(), kMenuInspect), 0);
  EXPECT_NE(std::count(reachable.begin(), reachable.end(), kMenuSell), 0);
  // The two upgrades are removed from the menu, not greyed. Equip stays,
  // greyed: the player can wear this kind of item, just not a destroyed one.
  std::string rendered = RenderElement(panel.menu().Render(0, 0));
  EXPECT_EQ(rendered.find("Scroll"), std::string::npos);
  EXPECT_EQ(rendered.find("Star Force"), std::string::npos);
  EXPECT_NE(rendered.find("Equip"), std::string::npos);
}

// Recovery rebuilds a destroyed item, so it means nothing on an item that was
// never destroyed.
TEST_F(InventoryPanelTest, ALiveItemIsOfferedNoRecovery) {
  LevelTo(UnlockLevel(Feature::kStarForce));
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  InventoryPanel panel(c_, account_, panel_focus_);
  panel.OpenMenu();
  EXPECT_EQ(RenderElement(panel.menu().Render(0, 0)).find("Recover"),
            std::string::npos);
}

// Neither action can do anything to a throwing star, so the menu shouldn't
// offer them. Scroll is the easy one to miss, because the picker always
// includes Clean Slate scrolls and would open on scrolls that all get refused.
TEST_F(InventoryPanelTest, ThrowingStarsOfferNoScrollOrStarForce) {
  // Levelled past both gates, or neither entry is offered on anything and this
  // would test the gates. ASpentWeaponKeepsScrollAndStarForce is the control.
  LevelTo(UnlockLevel(Feature::kStarForce));
  c_.PickUp(std::make_unique<EquipInstance>(MakeThrowingStars()));
  InventoryPanel panel(c_, account_, panel_focus_);
  panel.OpenMenu();
  std::vector<int> reachable = ReachableMenuEntries(panel.menu());
  EXPECT_EQ(std::count(reachable.begin(), reachable.end(), kMenuScroll), 0);
  EXPECT_EQ(std::count(reachable.begin(), reachable.end(), kMenuStarForce), 0);
  // Still a usable menu, or the checks above could pass on a broken one.
  EXPECT_NE(std::count(reachable.begin(), reachable.end(), kMenuInspect), 0);
  // Removed from the menu, not greyed. ReachableMenuEntries can't tell the two
  // apart, so the rendered menu is checked too.
  std::string rendered = RenderElement(panel.menu().Render(0, 0));
  EXPECT_EQ(rendered.find("Scroll"), std::string::npos);
  EXPECT_EQ(rendered.find("Star Force"), std::string::npos);
}

// Selling arrives with the shop and not before, since there is nowhere to sell
// until then.
TEST_F(InventoryPanelTest, SellArrivesWithTheShop) {
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  InventoryPanel panel(c_, account_, panel_focus_);
  LevelTo(UnlockLevel(Feature::kShop) - 1);
  panel.OpenMenu();
  std::vector<int> before = ReachableMenuEntries(panel.menu());
  EXPECT_EQ(std::count(before.begin(), before.end(), kMenuSell), 0);
  EXPECT_EQ(RenderElement(panel.menu().Render(0, 0)).find("Sell"),
            std::string::npos);

  LevelTo(UnlockLevel(Feature::kShop));
  panel.OpenMenu();
  std::vector<int> after = ReachableMenuEntries(panel.menu());
  EXPECT_NE(std::count(after.begin(), after.end(), kMenuSell), 0);
}

// Multi-Sell sits under Sell on both menus and waits for the same shop, since a
// mistaken sale is undone at the shop's buyback.
TEST_F(InventoryPanelTest, MultiSellSitsUnderSellOnBothMenus) {
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  c_.AddItem(MakeStackable("Red Shell", 7), 5);
  InventoryPanel panel(c_, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  LevelTo(UnlockLevel(Feature::kShop) - 1);
  panel.OpenMenu();
  std::vector<int> before = ReachableMenuEntries(panel.menu());
  EXPECT_EQ(std::count(before.begin(), before.end(), kMenuMultiSell), 0);
  OpenTab(comp, panel, kEtcTab);
  panel.OpenMenu();
  before = ReachableMenuEntries(panel.menu());
  EXPECT_EQ(std::count(before.begin(), before.end(), kStackMultiSell), 0);

  LevelTo(UnlockLevel(Feature::kShop));
  panel.OpenMenu();
  std::vector<int> after = ReachableMenuEntries(panel.menu());
  EXPECT_NE(std::count(after.begin(), after.end(), kStackMultiSell), 0);
  // Both appear on a worthless item: a stack worth nothing is still one the
  // player wants out of the bag.
  c_.AddItem(MakeStackable("Junk", 0), 5);
  OpenTab(comp, panel, kEtcTab);
  panel.OpenMenu();
  after = ReachableMenuEntries(panel.menu());
  EXPECT_NE(std::count(after.begin(), after.end(), kStackSell), 0);
  EXPECT_NE(std::count(after.begin(), after.end(), kStackMultiSell), 0);
}

TEST_F(InventoryPanelTest, MultiSellLeadsToItsScreen) {
  LevelTo(UnlockLevel(Feature::kShop));
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  c_.AddItem(MakeStackable("Red Shell", 7), 5);
  InventoryPanel panel(c_, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  ScrollPanel sp(c_, no_scrolls_);
  panel.OpenMenu();
  panel.menu().Up();  // Close
  panel.menu().Up();  // Multi-Sell
  EXPECT_EQ(panel.OnMenuEvent(ftxui::Event::Return, sp), kMultiSell);

  OpenTab(comp, panel, kEtcTab);
  panel.OpenMenu();
  panel.menu().Up();
  panel.menu().Up();
  EXPECT_EQ(panel.OnMenuEvent(ftxui::Event::Return, sp), kMultiSell);
}

// Once Sell is unlocked, every item and every item state offers it. Selling is
// the only way anything leaves the bag, so an item it skipped could never be
// got rid of.
TEST_F(InventoryPanelTest, SellIsOfferedOnEverything) {
  LevelTo(UnlockLevel(Feature::kShop));
  InventoryPanel panel(c_, account_, panel_focus_);
  // One at a time in the first row, where the menu opens. With three items in
  // the bag at once, only the top one would ever be checked.
  std::vector<std::unique_ptr<EquipTabItem>> items;
  items.push_back(std::make_unique<EquipInstance>(sword_));
  items.push_back(std::make_unique<EquipInstance>(MakeThrowingStars()));
  items.push_back(std::make_unique<EquipTrace>(sword_, Equip()));
  for (std::unique_ptr<EquipTabItem>& item : items) {
    std::string name = item->name();
    c_.PickUp(std::move(item));
    panel.OpenMenu();
    std::vector<int> reachable = ReachableMenuEntries(panel.menu());
    EXPECT_NE(std::count(reachable.begin(), reachable.end(), kMenuSell), 0)
        << name << " cannot be sold";
    EXPECT_NE(RenderElement(panel.menu().Render(0, 0)).find("Sell"),
              std::string::npos)
        << name << " has no Sell entry";
    c_.SellEquip(0);
  }
}

// --- level-gated menu entries ---

// A gated entry isn't drawn at all, rather than drawn grey. Greying it would
// advertise an upgrade the player can't use yet.
TEST_F(InventoryPanelTest, ANewCharacterIsOfferedNoUpgrades) {
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  InventoryPanel panel(c_, account_, panel_focus_);
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
  InventoryPanel panel(c_, account_, panel_focus_);
  LevelTo(UnlockLevel(Feature::kScrolling) - 1);
  panel.OpenMenu();
  std::vector<int> before = ReachableMenuEntries(panel.menu());
  EXPECT_EQ(std::count(before.begin(), before.end(), kMenuScroll), 0);

  LevelTo(UnlockLevel(Feature::kScrolling));
  panel.OpenMenu();
  std::vector<int> after = ReachableMenuEntries(panel.menu());
  EXPECT_NE(std::count(after.begin(), after.end(), kMenuScroll), 0);
}

TEST_F(InventoryPanelTest, StarForceArrivesAtItsLevel) {
  // A weapon with no slots left. An item with slots left greys the entry, and
  // this test is about the level gate, not that refusal.
  EquipPrototype proto = sword_;
  proto.set_upgrade_slots(1);
  Equip spent;
  spent.set_equip_name(proto.name());
  spent.set_remaining_upgrade_slots(0);
  c_.PickUp(std::make_unique<EquipInstance>(proto, spent));
  InventoryPanel panel(c_, account_, panel_focus_);

  LevelTo(UnlockLevel(Feature::kStarForce) - 1);
  panel.OpenMenu();
  std::vector<int> before = ReachableMenuEntries(panel.menu());
  EXPECT_EQ(std::count(before.begin(), before.end(), kMenuStarForce), 0);

  LevelTo(UnlockLevel(Feature::kStarForce));
  panel.OpenMenu();
  std::vector<int> after = ReachableMenuEntries(panel.menu());
  EXPECT_NE(std::count(after.begin(), after.end(), kMenuStarForce), 0);
}

// Recovery has no level gate of its own. A trace exists only because an item
// was destroyed at the 16th star, so the trace is the gate, and a character
// holding one at level 1 is offered it.
TEST_F(InventoryPanelTest, RecoveryFollowsTheTraceAndNotTheLevel) {
  Equip destroyed;
  destroyed.set_equip_name(sword_.name());
  c_.PickUp(std::make_unique<EquipTrace>(sword_, destroyed));
  InventoryPanel panel(c_, account_, panel_focus_);
  panel.OpenMenu();
  std::vector<int> reachable = ReachableMenuEntries(panel.menu());
  EXPECT_NE(std::count(reachable.begin(), reachable.end(), kMenuRecover), 0);
}

// An ordinary weapon keeps both, even with no slots left: a weapon with its
// slots used is what star force is for, and a Clean Slate still applies to it.
TEST_F(InventoryPanelTest, ASpentWeaponKeepsScrollAndStarForce) {
  LevelTo(UnlockLevel(Feature::kStarForce));
  EquipPrototype proto = sword_;
  proto.set_upgrade_slots(1);
  Equip state;
  state.set_equip_name(proto.name());
  state.set_remaining_upgrade_slots(0);
  c_.PickUp(std::make_unique<EquipInstance>(proto, state));
  InventoryPanel panel(c_, account_, panel_focus_);
  panel.OpenMenu();
  std::vector<int> reachable = ReachableMenuEntries(panel.menu());
  EXPECT_NE(std::count(reachable.begin(), reachable.end(), kMenuScroll), 0);
  EXPECT_NE(std::count(reachable.begin(), reachable.end(), kMenuStarForce), 0);
}

// An item with slots left keeps the entry too, greyed. Hiding it would keep the
// order secret: the player would never see what their scrolling leads to.
TEST_F(InventoryPanelTest, StarForceGreysWhileSlotsRemain) {
  LevelTo(UnlockLevel(Feature::kStarForce));
  EquipPrototype proto = sword_;
  proto.set_upgrade_slots(1);
  c_.PickUp(std::make_unique<EquipInstance>(proto));
  InventoryPanel panel(c_, account_, panel_focus_);
  panel.OpenMenu();
  std::vector<int> reachable = ReachableMenuEntries(panel.menu());
  EXPECT_EQ(std::count(reachable.begin(), reachable.end(), kMenuStarForce), 0)
      << "an unspent item let the player onto the entry";
  EXPECT_NE(RenderElement(panel.menu().Render(0, 0)).find("Star Force"),
            std::string::npos)
      << "greyed, not gone";
}

// The same rule as the equipped panel: the hammer entry sits between the other
// two upgrades, and appears only on an item that has slots.
TEST_F(InventoryPanelTest, TheHammerSitsBetweenTheOtherTwoUpgrades) {
  LevelTo(UnlockLevel(Feature::kHammer));
  EquipPrototype proto = sword_;
  proto.set_upgrade_slots(1);
  c_.PickUp(std::make_unique<EquipInstance>(proto));
  InventoryPanel panel(c_, account_, panel_focus_);
  panel.OpenMenu();
  std::vector<int> reachable = ReachableMenuEntries(panel.menu());
  EXPECT_NE(std::count(reachable.begin(), reachable.end(), kMenuHammer), 0);
  std::string rendered = RenderElement(panel.menu().Render(0, 0));
  EXPECT_LT(rendered.find("Scroll"), rendered.find("Hammer"));
  EXPECT_LT(rendered.find("Hammer"), rendered.find("Star Force"));
}

// The same cubing rule as the equipped panel: last of the upgrades, gold until
// pressed, and absent from a trace, which is no longer an item.
TEST_F(InventoryPanelTest, CubingArrivesLastAndNotOnATrace) {
  LevelTo(UnlockLevel(Feature::kPotential));
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  InventoryPanel panel(c_, account_, panel_focus_);
  panel.OpenMenu();
  std::vector<int> reachable = ReachableMenuEntries(panel.menu());
  EXPECT_NE(std::count(reachable.begin(), reachable.end(), kMenuCube), 0);
  std::string rendered = RenderElement(panel.menu().Render(0, 0));
  EXPECT_LT(rendered.find("Star Force"), rendered.find("Cube"));
  EXPECT_EQ(LabelColor(panel.menu().Render(0, 0), "Cube"), kYellow);

  // A trace is a remnant rather than an item, so nothing can be applied to it.
  CharacterInstance wrecked = MakeCharacter(UnlockLevel(Feature::kPotential));
  Equip lost;
  lost.set_equip_name(sword_.name());
  lost.set_stars(15);
  wrecked.PickUp(std::make_unique<EquipTrace>(sword_, lost));
  InventoryPanel trace_panel(wrecked, account_, panel_focus_);
  trace_panel.OpenMenu();
  EXPECT_EQ(RenderElement(trace_panel.menu().Render(0, 0)).find("Cube"),
            std::string::npos);
}

// An item a hammer can't improve gets no entry, just as Scroll is hidden on an
// item that refuses scrolls.
TEST_F(InventoryPanelTest, NoHammerEntryWithoutASlotToWiden) {
  LevelTo(UnlockLevel(Feature::kHammer));
  EquipPrototype slotless = sword_;
  slotless.set_upgrade_slots(0);
  c_.PickUp(std::make_unique<EquipInstance>(slotless));
  InventoryPanel panel(c_, account_, panel_focus_);
  panel.OpenMenu();
  EXPECT_EQ(RenderElement(panel.menu().Render(0, 0)).find("Hammer"),
            std::string::npos);
}

// Both hammers used, and the entry stays dim. If it vanished, it would look
// like the feature going away.
TEST_F(InventoryPanelTest, TheHammerGreysOnAFullyHammeredPiece) {
  LevelTo(UnlockLevel(Feature::kHammer));
  sword_.set_upgrade_slots(1);
  Equip state;
  state.set_equip_name(sword_.name());
  state.set_hammers(kMaxHammers);
  c_.PickUp(std::make_unique<EquipInstance>(sword_, state));
  InventoryPanel panel(c_, account_, panel_focus_);
  panel.OpenMenu();

  std::vector<int> reachable = ReachableMenuEntries(panel.menu());
  EXPECT_EQ(std::count(reachable.begin(), reachable.end(), kMenuHammer), 0)
      << "a finished piece let the player onto the entry";
  EXPECT_NE(RenderElement(panel.menu().Render(0, 0)).find("Hammer"),
            std::string::npos)
      << "greyed, not gone";
}

// --- the gold trail to a new upgrade ---

// The end of the trail that starts on the level-up card: the newly unlocked
// entry is gold until the player presses it.
TEST_F(InventoryPanelTest, ANewUpgradeIsGoldOnTheMenu) {
  LevelTo(UnlockLevel(Feature::kScrolling));
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  InventoryPanel panel(c_, account_, panel_focus_);
  panel.OpenMenu();
  EXPECT_EQ(LabelColor(panel.menu().Render(0, 0), "Scroll"), kYellow);
}

TEST_F(InventoryPanelTest, PressingTheUpgradePutsItsGoldOut) {
  LevelTo(UnlockLevel(Feature::kScrolling));
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  InventoryPanel panel(c_, account_, panel_focus_);
  ScrollPanel sp(c_, no_scrolls_);
  panel.OpenMenu();
  while (panel.menu().selected() != kMenuScroll) {
    panel.menu().Down();
  }
  panel.OnMenuEvent(ftxui::Event::Return, sp);

  panel.OpenMenu();
  EXPECT_NE(LabelColor(panel.menu().Render(0, 0), "Scroll"), kYellow);
}

// Each upgrade has its own gold. Star force isn't unlocked at the scrolling
// level, so none of its gold may be lit or used up yet.
TEST_F(InventoryPanelTest, OnlyTheUpgradeThatOpenedIsGold) {
  LevelTo(UnlockLevel(Feature::kStarForce));
  EquipPrototype proto = sword_;
  proto.set_upgrade_slots(1);
  Equip state;
  state.set_equip_name(proto.name());
  state.set_remaining_upgrade_slots(0);
  c_.PickUp(std::make_unique<EquipInstance>(proto, state));
  InventoryPanel panel(c_, account_, panel_focus_);
  ScrollPanel sp(c_, no_scrolls_);
  panel.OpenMenu();
  while (panel.menu().selected() != kMenuScroll) {
    panel.menu().Down();
  }
  panel.OnMenuEvent(ftxui::Event::Return, sp);

  panel.OpenMenu();
  EXPECT_NE(LabelColor(panel.menu().Render(0, 0), "Scroll"), kYellow);
  EXPECT_EQ(LabelColor(panel.menu().Render(0, 0), "Star Force"), kYellow);
}

// --- the level-gated Shop tab ---

// The bar just ends at Etc instead of showing a grey fourth chip. A shop the
// character can't enter shouldn't be a tab they can select.
TEST_F(InventoryPanelTest, TheShopTabIsAbsentBeforeItsLevel) {
  InventoryPanel panel(c_, account_, panel_focus_);
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
  InventoryPanel panel(c_, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  panel_focus_ = kInventoryPanel;
  ASSERT_EQ(RenderComponent(comp).find("Shop"), std::string::npos);

  LevelTo(UnlockLevel(Feature::kShop));
  EXPECT_NE(RenderComponent(comp).find("Shop"), std::string::npos);
  OpenTab(comp, panel, kShopTab);
  EXPECT_TRUE(panel.on_shop_tab());
}

// The Shop tab lists nothing the player owns, so instead of a list it shows how
// to get in.
TEST_F(InventoryPanelTest, ShopTabSaysHowToOpenTheShop) {
  LevelTo(UnlockLevel(Feature::kShop));
  InventoryPanel panel(c_, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  panel_focus_ = kInventoryPanel;
  OpenTab(comp, panel, kShopTab);
  EXPECT_TRUE(panel.on_shop_tab());
  EXPECT_NE(RenderComponent(comp).find("Hit Enter to open Shop"),
            std::string::npos);
}

// The Bank tab is the other door out of the panel, and it opens at 210, long
// after the shop. Below that the bar ends at Shop.
TEST_F(InventoryPanelTest, BankTabArrivesLastAndSaysHowToOpenIt) {
  LevelTo(UnlockLevel(Feature::kShop));
  {
    InventoryPanel early(c_, account_, panel_focus_);
    ftxui::Component comp = early.MakeComponent([]() {});
    OpenTab(comp, early, kBankTab);
    EXPECT_FALSE(early.on_bank_tab()) << "the tab is absent, not greyed";
  }

  LevelTo(UnlockLevel(Feature::kBank));
  InventoryPanel panel(c_, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  panel_focus_ = kInventoryPanel;
  OpenTab(comp, panel, kBankTab);
  EXPECT_TRUE(panel.on_bank_tab());
  EXPECT_FALSE(panel.on_shop_tab());
  EXPECT_NE(RenderComponent(comp).find("Hit Enter to open Bank"),
            std::string::npos);

  // Nothing to move into, so the cursor stays on the bar and Right moves from
  // Bank onto the Expand door after it.
  comp->OnEvent(ftxui::Event::ArrowDown);
  EXPECT_TRUE(panel.on_tab_bar());
  comp->OnEvent(ftxui::Event::ArrowRight);
  EXPECT_NE(RenderComponent(comp).find("Hit Enter to fullscreen Inventory"),
            std::string::npos)
      << "the door is the only thing past the bank";
}

// Before the bank unlocks, Shop is the last page of the bar, with only the
// Expand door after it.
TEST_F(InventoryPanelTest, ShopIsTheLastTabBeforeTheDoor) {
  LevelTo(UnlockLevel(Feature::kShop));
  InventoryPanel panel(c_, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  OpenTab(comp, panel, kShopTab);
  EXPECT_TRUE(panel.on_shop_tab());
  EXPECT_NE(RenderComponent(comp).find("Shop"), std::string::npos);
  comp->OnEvent(ftxui::Event::ArrowRight);
  EXPECT_NE(RenderComponent(comp).find("Hit Enter to fullscreen Inventory"),
            std::string::npos)
      << "and then the door";
}

// Down would leave the cursor nowhere, since this tab has no list. The Etc
// stack matters: without one, a Shop tab that wrongly used the Etc emptiness
// check would look inert for the wrong reason.
TEST_F(InventoryPanelTest, DownDoesNotDescendIntoTheShopTab) {
  LevelTo(UnlockLevel(Feature::kShop));
  c_.AddItem(MakeStackable("Shell", 7), 5);
  InventoryPanel panel(c_, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  panel_focus_ = kInventoryPanel;
  OpenTab(comp, panel, kShopTab);
  comp->OnEvent(ftxui::Event::ArrowDown);  // the bar -> the buttons
  comp->OnEvent(ftxui::Event::ArrowDown);  // and back, no row in between
  // Back on the tab bar, so Left switches tabs instead of moving a row.
  comp->OnEvent(ftxui::Event::ArrowLeft);
  EXPECT_FALSE(panel.on_shop_tab());
}

// The shop isn't a stack tab, or the sell menu would open over it.
TEST_F(InventoryPanelTest, TheShopTabIsNotAStackableTab) {
  LevelTo(UnlockLevel(Feature::kShop));
  InventoryPanel panel(c_, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  OpenTab(comp, panel, kShopTab);
  EXPECT_FALSE(panel.on_stackable_tab());
}

TEST_F(InventoryPanelTest, ShowsTabBar) {
  InventoryPanel panel(c_, account_, panel_focus_);
  std::string rendered = RenderComponent(panel.MakeComponent([]() {}));
  EXPECT_NE(rendered.find("Equip"), std::string::npos);
  EXPECT_NE(rendered.find("Etc"), std::string::npos);
}

TEST_F(InventoryPanelTest, ShowsMesoCounterWithCommas) {
  c_.AddMeso(1234567);
  InventoryPanel panel(c_, account_, panel_focus_);
  EXPECT_NE(RenderComponent(panel.MakeComponent([]() {})).find("1,234,567"),
            std::string::npos);
}

// The balances are centred in the bar, but never closer than a gutter to the
// last chip. At the narrowest panel, centring alone would put them against the
// tabs, and a count touching a tab reads as part of it.
TEST_F(InventoryPanelTest, TheBalancesKeepClearOfTheTabs) {
  LevelTo(UnlockLevel(Feature::kShop));
  c_.AddMeso(1234567);
  InventoryPanel panel(c_, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  std::string bar;
  for (const std::string& row : ScreenRows(RenderToScreen(comp))) {
    if (row.find("Shop") != std::string::npos) {
      bar = row;
      break;
    }
  }
  ASSERT_FALSE(bar.empty()) << "the whole Shop chip has to survive the row";
  size_t after_tabs = bar.find("Shop") + 4;
  size_t coin = bar.find("🪙");
  ASSERT_NE(coin, std::string::npos);
  ASSERT_GT(coin, after_tabs);
  EXPECT_GE(TextColumns(bar.substr(after_tabs, coin - after_tabs)), 8)
      << "the balances are up against the tab bar: [" << bar << "]";
}

// The trace balance appears beside the meso from the level traces can first be
// bought, not before. Below that there is no way to hold one, and a counter
// that can only show zero says nothing.
TEST_F(InventoryPanelTest, TheTraceBalanceArrivesWithTheShop) {
  ItemPrototype trace = MakeStackable(kSpellTraceName);
  trace.set_kind(ITEM_KIND_SPELL_TRACE);
  c_.AddItem(trace, 8400);
  InventoryPanel panel(c_, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  LevelTo(UnlockLevel(Feature::kShop) - 1);
  EXPECT_EQ(RenderComponentText(comp).find("8,400"), std::string::npos);
  LevelTo(UnlockLevel(Feature::kShop));
  EXPECT_NE(RenderComponentText(comp).find("8,400"), std::string::npos);
}

TEST_F(InventoryPanelTest, TheEtcTabListsItsStacksWithTheirQuantity) {
  c_.AddItem(MakeStackable("Snail Shell"), 5);
  InventoryPanel panel(c_, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  OpenTab(comp, panel, kEtcTab);
  std::string rendered = RenderComponent(comp);
  EXPECT_NE(rendered.find("Quantity"), std::string::npos);
  EXPECT_NE(rendered.find("Snail Shell"), std::string::npos);
}

// Like the Equip tab, an empty stack tab shows only the placeholder, with no
// column names. Names label rows, and there are no rows.
TEST_F(InventoryPanelTest, AnEmptyEtcTabShowsAPlaceholderAndNoColumnHeader) {
  InventoryPanel panel(c_, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  OpenTab(comp, panel, kEtcTab);
  std::string rendered = RenderComponent(comp);
  EXPECT_NE(rendered.find("(empty)"), std::string::npos);
  EXPECT_EQ(rendered.find("Quantity"), std::string::npos);
}

TEST_F(InventoryPanelTest, TheStackCursorStartsOnTheFirstRowAndWalksDown) {
  c_.AddItem(MakeStackable("Red Shell"), 5);
  c_.AddItem(MakeStackable("Blue Shell"), 3);
  panel_focus_ = kInventoryPanel;
  InventoryPanel panel(c_, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  OpenTab(comp, panel, kEtcTab);
  comp->OnEvent(ftxui::Event::ArrowDown);  // tab bar -> first stack
  EXPECT_NE(RenderComponentText(comp).find("> Red Shell"), std::string::npos);
  comp->OnEvent(ftxui::Event::ArrowDown);  // cursor -> second stack
  std::string rendered = RenderComponent(comp);
  EXPECT_NE(rendered.find("> Blue Shell"), std::string::npos);
  EXPECT_NE(rendered.find("  Red Shell"), std::string::npos);
}

TEST_F(InventoryPanelTest, TheStackCursorIsHiddenWhenThePanelIsNotFocused) {
  c_.AddItem(MakeStackable("Red Shell"), 5);
  panel_focus_ = kEquipPanel;
  InventoryPanel panel(c_, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  OpenTab(comp, panel, kEtcTab);
  std::string rendered = RenderComponent(comp);
  EXPECT_NE(rendered.find("  Red Shell"), std::string::npos);
  EXPECT_EQ(rendered.find("> Red Shell"), std::string::npos);
}

TEST_F(InventoryPanelTest, SwitchingTabsResetsStackCursor) {
  c_.AddItem(MakeStackable("Red Shell"), 5);
  c_.AddItem(MakeStackable("Blue Shell"), 3);
  panel_focus_ = kInventoryPanel;
  InventoryPanel panel(c_, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  OpenTab(comp, panel, kEtcTab);
  comp->OnEvent(ftxui::Event::ArrowDown);  // tab bar -> first stack
  comp->OnEvent(ftxui::Event::ArrowDown);  // cursor -> second stack
  comp->OnEvent(ftxui::Event::ArrowUp);    // -> first stack
  comp->OnEvent(ftxui::Event::ArrowUp);    // -> tab bar
  comp->OnEvent(ftxui::Event::ArrowLeft);  // Etc -> Equip
  OpenTab(comp, panel, kEtcTab);  // the stack cursor resets with the tab
  comp->OnEvent(ftxui::Event::ArrowDown);  // tab bar -> first stack
  EXPECT_NE(RenderComponentText(comp).find("> Red Shell"), std::string::npos);
}

TEST_F(InventoryPanelTest, EnterOnAStackOpensItsMenu) {
  c_.AddItem(MakeStackable("Red Shell", 7), 5);
  InventoryPanel panel(c_, account_, panel_focus_);
  bool opened = false;
  ftxui::Component comp = panel.MakeComponent([&opened]() { opened = true; });
  OpenTab(comp, panel, kEtcTab);
  comp->OnEvent(ftxui::Event::ArrowDown);  // tab bar -> stack list
  comp->OnEvent(ftxui::Event::Return);
  EXPECT_TRUE(opened);
}

// Nothing to move into, so the cursor stays on the bar, where Enter opens the
// tab menu instead of an item's.
TEST_F(InventoryPanelTest, EnterOnAnEmptyTabAsksAboutTheTab) {
  InventoryPanel panel(c_, account_, panel_focus_);
  bool opened = false;
  ftxui::Component comp = panel.MakeComponent([&opened]() { opened = true; });
  OpenTab(comp, panel, kEtcTab);           // empty
  comp->OnEvent(ftxui::Event::ArrowDown);  // nowhere below to go
  comp->OnEvent(ftxui::Event::Return);
  EXPECT_TRUE(opened);
  EXPECT_TRUE(panel.on_tab_bar());
}

// Inspect comes first, because looking at an item comes before deciding what to
// do with it.
TEST_F(InventoryPanelTest, StackMenuOpensOnInspect) {
  c_.AddItem(MakeStackable("Red Shell", 7), 5);
  InventoryPanel panel(c_, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  OpenTab(comp, panel, kEtcTab);
  panel.OpenMenu();
  ScrollPanel sp(c_, no_scrolls_);
  EXPECT_EQ(panel.menu().selected(), kStackInspect);
  EXPECT_EQ(panel.OnMenuEvent(ftxui::Event::Return, sp), kItemInspect);
}

TEST_F(InventoryPanelTest, StackMenuSellReturnsSellScreen) {
  c_.AddItem(MakeStackable("Red Shell", 7), 5);
  InventoryPanel panel(c_, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  OpenTab(comp, panel, kEtcTab);
  panel.OpenMenu();
  ScrollPanel sp(c_, no_scrolls_);
  panel.OnMenuEvent(ftxui::Event::ArrowDown, sp);  // Inspect -> Sell
  EXPECT_EQ(panel.OnMenuEvent(ftxui::Event::Return, sp), kSell);
}

TEST_F(InventoryPanelTest, StackMenuCloseReturnsMain) {
  c_.AddItem(MakeStackable("Red Shell", 7), 5);
  InventoryPanel panel(c_, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  OpenTab(comp, panel, kEtcTab);
  panel.OpenMenu();
  ScrollPanel sp(c_, no_scrolls_);
  panel.OnMenuEvent(ftxui::Event::ArrowDown, sp);  // Inspect -> Sell
  panel.OnMenuEvent(ftxui::Event::ArrowDown, sp);  // Sell -> Close
  EXPECT_EQ(panel.OnMenuEvent(ftxui::Event::Return, sp), kMain);
}

// A stack worth nothing is still offered for sale. Selling is the only way
// anything leaves the bag, so a row that refused it could never be discarded.
TEST_F(InventoryPanelTest, AWorthlessStackIsStillOfferedForSale) {
  c_.AddItem(MakeStackable("Junk", 0), 5);
  InventoryPanel panel(c_, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  OpenTab(comp, panel, kEtcTab);
  panel.OpenMenu();
  std::vector<int> reachable = ReachableMenuEntries(panel.menu());
  EXPECT_NE(std::count(reachable.begin(), reachable.end(), kStackSell), 0);
}

// The test screen is 20 rows, so a bag of 40 can't fit and the list has to
// scroll instead of running off the bottom of the window.
TEST_F(InventoryPanelTest, KeepsTheCursorInViewWhenTheBagOverflows) {
  for (int i = 0; i < 40; ++i) {
    c_.PickUp(std::make_unique<EquipInstance>(sword_));
  }
  InventoryPanel panel(c_, account_, panel_focus_);
  panel_focus_ = kInventoryPanel;
  ftxui::Component comp = panel.MakeComponent([]() {});
  comp->OnEvent(ftxui::Event::ArrowDown);  // tab bar -> item list
  RenderComponent(comp);
  for (int i = 0; i < 39; ++i) {
    comp->OnEvent(ftxui::Event::ArrowDown);
    // Rendered at every step, because the frame scrolls at render time.
    // Checking only at the end wouldn't show whether the view kept up or only
    // caught up.
    EXPECT_NE(RenderComponent(comp).find("> "), std::string::npos)
        << "the cursor left the window after " << i + 1 << " steps down";
  }
}

TEST_F(InventoryPanelTest, ScrollIndicatorOnlyOnOverflow) {
  InventoryPanel small(c_, account_, panel_focus_);
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  EXPECT_EQ(RenderComponent(small.MakeComponent([]() {})).find("┃"),
            std::string::npos)
      << "one item fits, so there is nothing to indicate";

  for (int i = 0; i < 40; ++i) {
    c_.PickUp(std::make_unique<EquipInstance>(sword_));
  }
  InventoryPanel big(c_, account_, panel_focus_);
  EXPECT_NE(RenderComponent(big.MakeComponent([]() {})).find("┃"),
            std::string::npos)
      << "41 items do not fit, so how far down the list is should show";
}

// Etc rows are plain text rather than an ftxui::Menu, so nothing marks the
// cursor for the frame unless the panel does.
TEST_F(InventoryPanelTest, KeepsTheCursorInViewOnAStackableTab) {
  for (int i = 0; i < 40; ++i) {
    c_.AddItem(MakeStackable("Etc " + std::to_string(i), 1), 1);
  }
  InventoryPanel panel(c_, account_, panel_focus_);
  panel_focus_ = kInventoryPanel;
  ftxui::Component comp = panel.MakeComponent([]() {});
  OpenTab(comp, panel, kEtcTab);
  comp->OnEvent(ftxui::Event::ArrowDown);  // tab bar -> stack list
  for (int i = 0; i < 39; ++i) {
    comp->OnEvent(ftxui::Event::ArrowDown);
    EXPECT_NE(RenderComponent(comp).find("> "), std::string::npos)
        << "the cursor left the window after " << i + 1 << " steps down";
  }
}

// --- cursor_row ---

// What the item menu is placed against. It must be where the cursor was drawn,
// not where the selected index says it should be.
TEST_F(InventoryPanelTest, CursorRowIsTheRowTheCursorWasDrawnOn) {
  panel_focus_ = kInventoryPanel;
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  InventoryPanel panel(c_, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  comp->OnEvent(ftxui::Event::ArrowDown);  // tab bar -> item list
  comp->OnEvent(ftxui::Event::ArrowDown);  // -> second item
  ftxui::Screen screen = RenderToScreen(comp);
  EXPECT_EQ(panel.cursor_row(), RowWithCursor(screen));
}

// Once the list scrolls, the index and the screen row diverge. Moved one item
// at a time, since jumping to the end can't tell a cursor that kept up from one
// that caught up.
TEST_F(InventoryPanelTest, CursorRowFollowsAListThatHasScrolled) {
  panel_focus_ = kInventoryPanel;
  FillBag(40);
  InventoryPanel panel(c_, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  comp->OnEvent(ftxui::Event::ArrowDown);  // tab bar -> item list
  for (int i = 0; i < 39; ++i) {
    comp->OnEvent(ftxui::Event::ArrowDown);
    ftxui::Screen screen = RenderToScreen(comp);
    ASSERT_EQ(panel.cursor_row(), RowWithCursor(screen))
        << "cursor row wrong on item " << i + 1;
  }
  // The two really did diverge, which is the point: the index is well past the
  // bottom of a twenty-row screen while its row is still inside the window.
  EXPECT_GT(panel.selected(), panel.cursor_row());
  EXPECT_LT(panel.cursor_row(), 20);
}

// The Equip tab draws a row plain or split into coloured cells when the
// character can't equip it. sword_ takes the coloured path, so this uses an
// item a level-1 Beginner can wear.
TEST_F(InventoryPanelTest, CursorRowFindsAnEquippableItem) {
  panel_focus_ = kInventoryPanel;
  EquipPrototype plain;
  plain.set_name("Plain Sword");
  plain.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  plain.add_equip_job_categories(EQUIP_JOB_CATEGORY_UNIVERSAL);
  c_.PickUp(std::make_unique<EquipInstance>(plain));
  c_.PickUp(std::make_unique<EquipInstance>(plain));
  ASSERT_TRUE(c_.MeetsLevel(plain));
  ASSERT_TRUE(c_.MeetsJob(plain));
  InventoryPanel panel(c_, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  comp->OnEvent(ftxui::Event::ArrowDown);  // tab bar -> item list
  comp->OnEvent(ftxui::Event::ArrowDown);  // -> second item
  ftxui::Screen screen = RenderToScreen(comp);
  EXPECT_EQ(panel.cursor_row(), RowWithCursor(screen));
}

// In the game the bag isn't at the top of the screen, since the equipped panel
// is above it. The reported row must be the screen row, so it has to include
// that offset.
TEST_F(InventoryPanelTest, CursorRowIsAScreenRow) {
  panel_focus_ = kInventoryPanel;
  FillBag(40);
  InventoryPanel panel(c_, account_, panel_focus_);
  ftxui::Component comp = panel.MakeComponent([]() {});
  comp->OnEvent(ftxui::Event::ArrowDown);  // tab bar -> item list
  for (int i = 0; i < 39; ++i) {
    comp->OnEvent(ftxui::Event::ArrowDown);
    ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80),
                                                 ftxui::Dimension::Fixed(20));
    // Three rows of something else above it, like the equipped panel.
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

// The Etc tab draws its own rows instead of using ftxui::Menu, so it needs its
// own marking.
TEST_F(InventoryPanelTest, CursorRowFollowsTheStackListToo) {
  for (int i = 0; i < 30; ++i) {
    c_.AddItem(MakeStackable("Shell " + std::to_string(i)), 1);
  }
  InventoryPanel panel(c_, account_, panel_focus_);
  // The stack list draws its cursor only while the panel has focus, and this
  // test compares cursor_row() with where that cursor landed.
  panel_focus_ = kInventoryPanel;
  ftxui::Component comp = panel.MakeComponent([]() {});
  OpenTab(comp, panel, kEtcTab);
  comp->OnEvent(ftxui::Event::ArrowDown);  // tab bar -> stack list
  for (int i = 0; i < 29; ++i) {
    comp->OnEvent(ftxui::Event::ArrowDown);
    ftxui::Screen screen = RenderToScreen(comp);
    ASSERT_EQ(panel.cursor_row(), RowWithCursor(screen))
        << "cursor row wrong on stack " << i + 1;
  }
  EXPECT_GT(panel.selected_stack(), panel.cursor_row());
}

// --- highlighting ---

// The bag arrives at level 4, and the gold border sends the player to it
// instead of leaving them to find the new panel.
TEST_F(InventoryPanelTest, LightsItsBorderGoldWhenHighlighted) {
  InventoryPanel panel(c_, account_, panel_focus_);
  ftxui::Component component = panel.MakeComponent([]() {});
  ASSERT_EQ(BorderColor(component->Render()), kTheme);
  panel.SetHighlighted(true);
  EXPECT_EQ(BorderColor(component->Render()), kYellow);
  panel.SetHighlighted(false);
  EXPECT_EQ(BorderColor(component->Render()), kTheme);
}

// Two rules from two different renderers: the one under the tab bar and the one
// under the stack list's column headers. Both must turn gold, so this checks
// every rule the panel drew, not just the first.
TEST_F(InventoryPanelTest, LightsEveryInnerRuleGoldToo) {
  c_.AddItem(MakeStackable("Red Shell"), 5);
  InventoryPanel panel(c_, account_, panel_focus_);
  ftxui::Component component = panel.MakeComponent([]() {});
  panel_focus_ = kInventoryPanel;
  OpenTab(component, panel, kEtcTab);

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

// The gold outlasts the four-second card, so a player who was away when the
// shop opened still sees the tab marked as new.
TEST_F(InventoryPanelTest, ANewShopTabIsWrittenInGold) {
  LevelTo(UnlockLevel(Feature::kShop));
  InventoryPanel panel(c_, account_, panel_focus_);
  ftxui::Component component = panel.MakeComponent([]() {});
  EXPECT_EQ(LabelColor(component->Render(), "Shop"), kYellow);
  EXPECT_EQ(LabelColor(component->Render(), "Etc"), kTheme)
      << "the tabs that were always there say nothing";
}

// Moving onto it turns the gold off: the tab counts as seen when opened, not
// when it appears.
TEST_F(InventoryPanelTest, OpeningTheShopTabStopsItAnnouncingItself) {
  LevelTo(UnlockLevel(Feature::kShop));
  InventoryPanel panel(c_, account_, panel_focus_);
  ftxui::Component component = panel.MakeComponent([]() {});
  panel_focus_ = kInventoryPanel;
  ASSERT_EQ(LabelColor(component->Render(), "Shop"), kYellow);

  OpenTab(component, panel, kShopTab);
  ASSERT_TRUE(panel.on_shop_tab()) << "the walk has to actually arrive";
  // Read unfocused, because a focused active chip is black on white regardless,
  // which would hide what is being tested.
  panel_focus_ = kCharPanel;
  EXPECT_EQ(LabelColor(component->Render(), "Shop"), kTheme);
}

// An advancement puts gear in the bag (a weapon at the 1st, an off-hand at the
// 2nd), and the gold on the tab says to go and look.
TEST_F(InventoryPanelTest, TheEquipTabGoesGoldForTheGearAnAdvancementGave) {
  LevelTo(UnlockLevel(Feature::kBag));
  InventoryPanel panel(c_, account_, panel_focus_);
  ftxui::Component component = panel.MakeComponent([]() {});
  EXPECT_EQ(LabelColor(component->Render(), "Equip"), kTheme)
      << "a Beginner has been handed nothing";

  c_.AdvanceJob(JOB_SWORDMAN);
  EXPECT_EQ(LabelColor(component->Render(), "Equip"), kYellow);
}

// Tracked per advancement, like the Advance tab's own key: having looked at the
// 1st job's weapon doesn't mean having seen the 2nd job's off-hand.
TEST_F(InventoryPanelTest, TheSecondAdvancementGildsTheEquipTabAgain) {
  LevelTo(UnlockLevel(Feature::kBag));
  c_.AdvanceJob(JOB_SWORDMAN);
  InventoryPanel panel(c_, account_, panel_focus_);
  ftxui::Component component = panel.MakeComponent([]() {});
  panel.MarkActiveTabSeen();
  ASSERT_EQ(LabelColor(component->Render(), "Equip"), kTheme);

  c_.AdvanceJob(JOB_FIGHTER);
  EXPECT_EQ(LabelColor(component->Render(), "Equip"), kYellow);
}

// Moving back onto the tab turns the gold off, just as for the shop.
TEST_F(InventoryPanelTest, OpeningTheEquipTabStopsItAnnouncingItself) {
  LevelTo(UnlockLevel(Feature::kBag));
  c_.AdvanceJob(JOB_SWORDMAN);
  InventoryPanel panel(c_, account_, panel_focus_);
  ftxui::Component component = panel.MakeComponent([]() {});
  // Read unfocused, because the active chip is black on white while the panel
  // has focus, which would hide what is being tested.
  ASSERT_EQ(LabelColor(component->Render(), "Equip"), kYellow);

  panel_focus_ = kInventoryPanel;
  component->OnEvent(ftxui::Event::ArrowRight);
  component->OnEvent(ftxui::Event::ArrowLeft);
  panel_focus_ = kCharPanel;
  EXPECT_EQ(LabelColor(component->Render(), "Equip"), kTheme);
}

// The other half of the rule: a tab already open under the cursor is being
// read, and arriving on the panel marks it seen.
TEST_F(InventoryPanelTest, ArrivingOnThePanelReadsTheOpenTab) {
  LevelTo(UnlockLevel(Feature::kBag));
  c_.AdvanceJob(JOB_SWORDMAN);
  InventoryPanel panel(c_, account_, panel_focus_);
  ftxui::Component component = panel.MakeComponent([]() {});
  ASSERT_EQ(LabelColor(component->Render(), "Equip"), kYellow);

  panel.MarkActiveTabSeen();
  EXPECT_EQ(LabelColor(component->Render(), "Equip"), kTheme);
}

// It stays off: the record is on the character, so it survives the panel being
// rebuilt, as happens on a relaunch.
TEST_F(InventoryPanelTest, AnOpenedShopTabStaysQuietForANewPanel) {
  LevelTo(UnlockLevel(Feature::kShop));
  account_.MarkSeen(kShopTabKey);
  InventoryPanel panel(c_, account_, panel_focus_);
  ftxui::Component component = panel.MakeComponent([]() {});
  EXPECT_EQ(LabelColor(component->Render(), "Shop"), kTheme);
}

// --- Spare Arcane Symbols on the equip tab ---

class SpareSymbolTest : public InventoryPanelTest {
 protected:
  CharacterInstance Traveller() {
    Character proto;
    proto.set_level(200);
    proto.set_job(JOB_HERO);
    proto.set_job_stage(4);
    return CharacterInstance(rng_, std::move(proto));
  }
};

// The first copy is equipped, and every later one is fed into the worn one.
// Only one symbol per area is ever equipped, so the two entries replace each
// other instead of both appearing.
TEST_F(SpareSymbolTest, EquipAndCombineTradePlaces) {
  CharacterInstance c = Traveller();
  c.PickUp(std::make_unique<EquipInstance>(VanishingJourneySymbol()));
  InventoryPanel first(c, account_, panel_focus_);
  first.MakeComponent([]() {});
  first.OpenMenu();
  std::vector<int> reachable = ReachableMenuEntries(first.menu());
  EXPECT_NE(std::count(reachable.begin(), reachable.end(), kMenuAction), 0);
  EXPECT_EQ(std::count(reachable.begin(), reachable.end(), kMenuCombine), 0);

  ASSERT_TRUE(c.Equip(0));
  c.PickUp(std::make_unique<EquipInstance>(VanishingJourneySymbol()));
  InventoryPanel second(c, account_, panel_focus_);
  second.MakeComponent([]() {});
  second.OpenMenu();
  reachable = ReachableMenuEntries(second.menu());
  EXPECT_EQ(std::count(reachable.begin(), reachable.end(), kMenuAction), 0);
  EXPECT_NE(std::count(reachable.begin(), reachable.end(), kMenuCombine), 0);
}

// A second copy of a ring already worn still offers Equip: it swaps with the
// worn one instead of joining it, which is how a better copy goes on.
TEST_F(InventoryPanelTest, ASecondCopyOfAWornRingIsStillOffered) {
  EquipPrototype ring;
  ring.set_name("Silver Blossom Ring");
  ring.set_equip_slot(EQUIP_SLOT_RING);
  ring.add_equip_job_categories(EQUIP_JOB_CATEGORY_UNIVERSAL);
  c_.PickUp(std::make_unique<EquipInstance>(ring));
  c_.PickUp(std::make_unique<EquipInstance>(ring));

  InventoryPanel spare(c_, account_, panel_focus_);
  spare.MakeComponent([]() {});
  spare.OpenMenu();
  std::vector<int> reachable = ReachableMenuEntries(spare.menu());
  EXPECT_NE(std::count(reachable.begin(), reachable.end(), kMenuAction), 0);

  ASSERT_TRUE(c_.Equip(0));
  InventoryPanel worn(c_, account_, panel_focus_);
  worn.MakeComponent([]() {});
  worn.OpenMenu();
  reachable = ReachableMenuEntries(worn.menu());
  EXPECT_NE(std::count(reachable.begin(), reachable.end(), kMenuAction), 0);
}

// No upgrade applies to a symbol, so neither is on its menu.
TEST_F(SpareSymbolTest, ASpareOffersNoScrollOrStarForce) {
  CharacterInstance c = Traveller();
  c.PickUp(std::make_unique<EquipInstance>(VanishingJourneySymbol()));
  InventoryPanel panel(c, account_, panel_focus_);
  panel.MakeComponent([]() {});
  panel.OpenMenu();
  std::string rendered = RenderElement(panel.menu().Render(0, 0));
  EXPECT_EQ(rendered.find("Scroll"), std::string::npos);
  EXPECT_EQ(rendered.find("Star Force"), std::string::npos);
}

// Combine opens the dialog that asks how many to feed in.
TEST_F(SpareSymbolTest, CombineOpensTheDialog) {
  CharacterInstance c = Traveller();
  c.PickUp(std::make_unique<EquipInstance>(VanishingJourneySymbol()));
  ASSERT_TRUE(c.Equip(0));
  c.PickUp(std::make_unique<EquipInstance>(VanishingJourneySymbol()));
  InventoryPanel panel(c, account_, panel_focus_);
  panel.MakeComponent([]() {});
  panel.OpenMenu();
  ASSERT_TRUE(StepTo(panel.menu(), kMenuCombine));
  ScrollPanel scrolls(c, no_scrolls_);
  EXPECT_EQ(panel.OnMenuEvent(ftxui::Event::Return, scrolls), kSymbolCombine);
}

// Ordinary gear never offers Combine, whatever else its menu holds.
TEST_F(SpareSymbolTest, GearNeverOffersCombine) {
  CharacterInstance c = Traveller();
  EquipPrototype sword;
  sword.set_name("Sword");
  sword.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  sword.add_equip_job_categories(EQUIP_JOB_CATEGORY_UNIVERSAL);
  c.PickUp(std::make_unique<EquipInstance>(sword));
  InventoryPanel panel(c, account_, panel_focus_);
  panel.MakeComponent([]() {});
  panel.OpenMenu();
  std::vector<int> reachable = ReachableMenuEntries(panel.menu());
  EXPECT_EQ(std::count(reachable.begin(), reachable.end(), kMenuCombine), 0);
}

// Every tab at the right column's narrowest width. The rows fill that width
// exactly, so a column measured wrong runs into the border.
TEST_F(InventoryPanelTest, NoTabWeldsARowToTheRightBorder) {
  LevelTo(200);
  UnlockEverything();
  c_.AddMeso(1234567890);
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  InventoryPanel panel(c_, account_, panel_focus_);
  panel.SetWidth(kRightColumnMin);
  ftxui::Component comp = panel.MakeComponent([]() {});
  for (int tab = 0; tab < kNumInventoryTabs; ++tab) {
    OpenTab(comp, panel, tab);
    std::vector<std::string> touching =
        RowsTouchingTheRightBorder(comp->Render());
    EXPECT_TRUE(touching.empty())
        << "tab " << tab << ": " << (touching.empty() ? "" : touching.front());
  }
}
}  // namespace
}  // namespace ms
