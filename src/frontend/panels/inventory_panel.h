/* InventoryPanel shows the character's inventory as two tabs: Equip (equip-tab
 * items as a navigable menu) and Etc (a read-only stackable list).
 *
 * Focus moves top-to-bottom through three zones, Down descending and Up
 * ascending, matching the character panel. The top zone is the Equip/Etc tab
 * bar: there Left/Right switch tabs and the active tab is drawn white to show
 * the row is selected. Down descends into the tab's item list (only when it is
 * non-empty); there Up off the top row returns to the tab bar and Enter opens
 * the item context menu via the on_enter callback passed to MakeComponent().
 * The Etc tab has no menu actions beyond Sell.
 *
 * The Expand tab holds the far right of the bar, past the meso counter. It is
 * a door rather than a page: standing on it draws one line saying so, and
 * Enter opens the bag up to the whole screen, on its first tab. The bar wraps
 * there -- Right off Expand comes round to Equip, and Left off Equip goes to
 * Expand.
 *
 * Call MakeComponent() exactly once; the returned Component captures references
 * to internal state, so the panel object must outlive the Component.
 */
#ifndef MS_SRC_FRONTEND_PANELS_INVENTORY_PANEL_H_
#define MS_SRC_FRONTEND_PANELS_INVENTORY_PANEL_H_

#include <functional>
#include <string>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/screen/box.hpp"
#include "src/account.h"
#include "src/character/character.h"
#include "src/character/stat_preset.h"
#include "src/frontend/panel_widths.h"
#include "src/frontend/screens/scroll_panel.h"
#include "src/frontend/types.h"
#include "src/frontend/widgets/inventory_list.h"
#include "src/frontend/widgets/item_columns.h"
#include "src/frontend/widgets/item_menu.h"
#include "src/frontend/widgets/item_row.h"
#include "src/frontend/widgets/marquee.h"
#include "src/item/equip_instance.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"

namespace ms {

class InventoryPanel {
 public:
  InventoryPanel(CharacterInstance& character, AccountInstance& account,
                 int& panel_focus);
  // `on_enter` is Enter on a row or a tab -- the item menu, the tab's own, or
  // the shop. `on_expand` opens the bag to the whole screen and back.
  ftxui::Component MakeComponent(std::function<void()> on_enter,
                                 std::function<void()> on_expand = nullptr);
  void OpenMenu();
  // Drives the item context menu and runs the chosen action, returning the
  // next screen. `gear` is the preset the Equipped panel is SHOWING, which is
  // where an Equip puts the item: what is looked at is what is dressed.
  Screen OnMenuEvent(ftxui::Event event, ScrollPanel& scroll_panel,
                     StatPreset gear = StatPreset::kFirst);

  // The item context menu for the active tab: the equip menu on Equip, the
  // sell menu on Etc.
  ItemMenu& menu();
  // The {Sort, Close} menu Enter opens on a tab. Kept apart from menu()
  // because it is about the tab rather than about anything in it, and the two
  // are never open at once.
  ItemMenu& tab_menu() {
    return tab_menu_;
  }
  // Resets it. Only ever opened on Equip or Etc -- see TabMenuEntry.
  void OpenTabMenu();
  // Handles Up/Down/Escape/Return for it and runs the chosen action.
  Screen OnTabMenuEvent(ftxui::Event event);
  // The screen row the open menu anchors under: the highlighted item's, or the
  // row below the tab bar while the cursor is up there. Read from the RENDER,
  // not selected(), which is a position in the data and stops agreeing once
  // the list scrolls.
  int cursor_row() const {
    return on_tab_bar() ? bar_box_.y_min + 2 : cursor_box_.y_min;
  }
  // The columns the panel may take, which the layout works out from the
  // terminal's. What a wide terminal brings goes to the Equip tab's name
  // column; the stack tabs keep theirs.
  void SetWidth(int width) {
    width_ = width;
  }
  int selected() const {
    return selected_;
  }
  // True when the Etc tab is active: the one tab of stacks whose rows the
  // cursor walks.
  bool on_stackable_tab() const;
  // Whether the Shop tab is the active one. The shop is a screen rather than a
  // list, so the controller asks this to tell Enter on the tab bar apart from
  // Enter on an item.
  bool on_shop_tab() const;
  // And the Bank tab, the other door out of the panel, on the same terms.
  bool on_bank_tab() const;
  // Whether the cursor stands up on the tab bar. The controller asks to tell
  // Enter on a tab apart from Enter on an item, and the layout asks so the tab
  // menu hangs at the panel's left rather than out past the item columns.
  bool on_tab_bar() const {
    return zone_ == kZoneTabs;
  }
  // Which tab is open, as an InventoryTab. The Multi-Sell screen opens on it,
  // and on whichever row the cursor stands on there.
  int active_tab() const {
    return active_tab_;
  }
  // The stack the Etc cursor stands on, as an index into the character's
  // stacks -- Etc lists only part of them, so the row is not the index. -1
  // when the tab has no row to stand on.
  int selected_stack() const;
  // The column the item menu hangs at, from the panel's left border: past the
  // cursor, name and slot cells, so it covers stats rather than a name. Asked
  // of the panel, whose width the name column follows.
  int menu_column() const;

  // Records the active tab as opened, which puts its gold out. Called when the
  // player steps onto a tab and when focus arrives on the panel: a tab already
  // under the cursor has been seen as surely as one stepped onto.
  void MarkActiveTabSeen();

  // Lights the border gold while a level-up is celebrated: the bag arrives at
  // level 4, and a card in the middle of the screen does not say where to
  // look. No clock of its own -- whoever lit it turns it off.
  void SetHighlighted(bool highlighted) {
    highlighted_ = highlighted;
  }

  // Whether the bag is currently drawn over the whole screen. Set from the
  // render, like SetHighlighted: the screen the player is on is the
  // controller's to know, and the panel only needs it to label the button.
  void SetExpanded(bool expanded) {
    expanded_ = expanded;
  }

 private:
  // The two menus OnMenuEvent drives, one per tab family: the equip menu on
  // Equip, the {Inspect, Sell, Multi-Sell} menu on Etc.
  Screen OnEquipMenuEvent(ftxui::Event event, ScrollPanel& scroll_panel,
                          StatPreset gear);
  Screen OnStackMenuEvent(ftxui::Event event);

  // Whether the border is currently lit gold. Set from outside, read by the
  // render; no part of the panel's own state machine.
  bool highlighted_ = false;
  // See SetExpanded.
  bool expanded_ = false;
  // See SetWidth.
  int width_ = kRightColumnMin;

  // The columns the Equip tab draws at the panel's width, with the mechanics
  // the account has not unlocked left out.
  ItemColumns Columns() const;

  // The two vertical focus zones: the tab bar on top and the active tab's item
  // list below it.
  enum Zone { kZoneTabs, kZoneList };

  // The tab menu's entries. It opens only on a tab that lists something --
  // Shop and Expand are doors, and Enter goes through them instead -- so Sort
  // always has a list to act on.
  enum TabMenuEntry : int { kTabMenuSort = 0, kTabMenuClose = 1 };

  // Files the active tab, which is what Sort does.
  void SortActiveTab();
  // How many stacks the Etc tab lists, which is every stack the bag holds:
  // the currencies are counted in the purse and are on no tab with slots.
  int EtcRowCount() const;
  // The Token tab: two read-only columns, the shop's currencies beside the
  // bosses' soul shards. Nothing on it can be selected, so it takes no cursor
  // and Enter on the bar above it opens the {Sort, Close} menu.
  ftxui::Element RenderCurrencySheet();
  // Rows on the Token tab: as many as its longer column.
  int CurrencyRowCount() const;
  // Rows of it the panel can show at once, from the last frame's box.
  int CurrencySheetHeight() const;
  // Scrolls the sheet `delta` rows, which is what Up and Down do on a tab with
  // no cursor to move. Clamped rather than wrapped: this is a position in a
  // page, not a cursor going round a ring.
  void ScrollCurrencySheet(int delta);

  // What OpenMenu opens, by tab.
  void OpenStackMenu();
  void OpenEquipMenu();
  // OpenEquipMenu's three passes, in order. What the player has not REACHED is
  // hidden before what the item refuses -- the first is about them, the second
  // about this item -- and the gold lands last, on what is left.
  void HideLockedFeatures();
  void HideRefusedUpgrades(const EquipInstance& equip);
  void HighlightUnusedUpgrades();
  // The Equip tab's menu on a spare Arcane Symbol, which offers a different
  // set from every other item.
  void OpenSymbolMenu(const EquipInstance& symbol);
  // Wraps the active tab's body in the titled window with the tab bar on top.
  ftxui::Element RenderContent(ftxui::Component menu);
  // One row of the Equip list, with the cursor and whatever the row has to say
  // in red or dim. The ftxui::Menu's row transform.
  ftxui::Element RenderRow(const ftxui::EntryState& state);
  // The Expand tab, drawn right-aligned in the tab row.
  ftxui::Element RenderExpandTab(bool row_selected) const;
  // The key handlers, one per place the cursor can be: the tab bar, the Etc
  // list, the Equip list.
  bool OnTabBarEvent(const ftxui::Event& event,
                     const std::function<void()>& on_enter,
                     const std::function<void()>& on_expand);
  bool OnStackListEvent(const ftxui::Event& event,
                        const std::function<void()>& on_enter);
  bool OnEquipListEvent(const ftxui::Event& event,
                        const std::function<void()>& on_enter);
  // Rebuilds rows_/entries_ from the equip inventory and returns the Equip tab
  // body (column headers + the navigable menu, or "(empty)").
  // The Equip tab as an ftxui::Menu, which is what carries its item menu. The
  // stack tabs go through the shared RenderStackList instead.
  ftxui::Element RenderOwnEquipList(ftxui::Component menu);
  // Whether the active tab's item list has no rows to descend into.
  bool ActiveTabEmpty() const;
  // Rows in the list below the tab bar, for whichever tab is active. The bar
  // is not one of them, and the shop tab has no list of its own at all.
  int ListCount() const;
  // Where the cursor stands in the panel's one vertical ring: the tab bar is
  // stop 0 and the list rows are the stops after it.
  int CursorStop() const;
  // Moves the cursor `delta` stops around that ring, the tab bar included. So
  // Down off the last row returns to the bar, and Up off the bar goes to the
  // last row -- one rule rather than a pair of edge cases.
  void MoveCursor(int delta);
  // The content tabs this character has unlocked, left to right. Locked tabs
  // are absent rather than greyed, so the bar simply ends early. Expand is not
  // one of these, being a door rather than a page.
  std::vector<int> VisibleTabs() const;
  // Moves one stop along the bar, Expand included. The bar is a ring: Right
  // off Expand comes round to the first tab, and Left off the first tab goes
  // to Expand.
  void StepTab(int direction);

  CharacterInstance& character_;
  // Not const: opening a tab and walking a gold trail are the account's to
  // record, so the panel writes as well as reads.
  AccountInstance& account_;
  int& panel_focus_;
  Zone zone_ = kZoneTabs;  // which focus zone holds the cursor
  // Written by ftxui::reflect on the highlighted row each render.
  ftxui::Box cursor_box_;
  // And on the tab bar, so a tab menu knows the row to open under.
  ftxui::Box bar_box_;
  // And on the Token tab's scrolling frame, which is how Up and Down know how
  // far one of them may take the sheet.
  ftxui::Box sheet_box_;
  // The first row of the sheet on screen.
  int currency_scroll_ = 0;
  int selected_ = 0;
  // When the selection last moved, for sliding a long name under its column.
  SelectionClock
      name_clock_;          // selected row on the Equip tab (ftxui::Menu index)
  int selected_stack_ = 0;  // selected row of the Etc view
  // Whether the cursor stands out on the Expand tab. Kept apart from
  // active_tab_ because the two are different facts: the list goes on showing
  // the tab it was showing, and Left steps back onto it.
  bool on_expand_ = false;
  int active_tab_ = kEquipTab;
  std::vector<InventoryRowState> rows_;
  std::vector<std::string>
      entries_;         // labels derived from rows_ for ftxui::Menu
  ItemMenu menu_;       // Equip tab context menu.
  ItemMenu sell_menu_;  // Etc tab context menu.
  ItemMenu tab_menu_;   // the {Sort, Close} menu Enter opens on a tab.
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_PANELS_INVENTORY_PANEL_H_
