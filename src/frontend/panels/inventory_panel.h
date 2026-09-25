/* InventoryPanel shows the character's bag in tabs: Equip (a navigable list of
 * equipment), Token (a read-only sheet of currencies), Etc (a list of stacks),
 * and the Shop and Bank doors once they unlock.
 *
 * Focus moves top to bottom through two zones, as in the character panel. The
 * top zone is the tab bar, where Left and Right switch tabs and the active tab
 * is drawn white to show the row is selected. Down moves into the tab's list
 * when it has rows. There, Up from the top row returns to the tab bar, and
 * Enter opens the item's context menu through the on_enter callback passed to
 * MakeComponent().
 *
 * The Expand tab sits at the far right of the bar, past the meso counter. It is
 * a door rather than a page: selecting it shows one line saying so, and Enter
 * opens the bag to fill the screen, on its first tab. The bar wraps there:
 * Right from Expand goes to Equip, and Left from Equip goes to Expand.
 *
 * Call MakeComponent() exactly once. The returned Component holds references to
 * internal state, so the panel must outlive it.
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
  // `on_enter` is Enter on a row or a tab: the item menu, the tab menu, or the
  // shop. `on_expand` opens the bag to fill the screen and closes it again.
  ftxui::Component MakeComponent(std::function<void()> on_enter,
                                 std::function<void()> on_expand = nullptr);
  void OpenMenu();
  // Drives the item context menu and runs the chosen action, returning the next
  // screen. `gear` is the preset the Equipped panel is showing, which is where
  // Equip puts the item, so the gear on screen is the gear that changes.
  Screen OnMenuEvent(ftxui::Event event, ScrollPanel& scroll_panel,
                     StatPreset gear = StatPreset::kFirst);

  // The item context menu for the active tab: the equip menu on Equip, the
  // stack menu on Etc.
  ItemMenu& menu();
  // The {Sort, Close} menu Enter opens on a tab. Kept separate from menu()
  // because it is about the tab, not anything in it, and the two are never open
  // at once.
  ItemMenu& tab_menu() {
    return tab_menu_;
  }
  // Resets it. Only opened on Equip or Etc (see TabMenuEntry).
  void OpenTabMenu();
  // Handles Up, Down, Escape and Return for it and runs the chosen action.
  Screen OnTabMenuEvent(ftxui::Event event);
  // The screen row the open menu is placed under: the highlighted item's, or
  // the row below the tab bar while the cursor is there. Read from the render,
  // not selected(), which is a position in the data and stops matching once the
  // list scrolls.
  int cursor_row() const {
    return on_tab_bar() ? bar_box_.y_min + 2 : cursor_box_.y_min;
  }
  // The width the panel may take, which the layout computes from the
  // terminal's. Extra width goes to the Equip tab's name column. The stack tabs
  // keep theirs.
  void SetWidth(int width) {
    width_ = width;
  }
  int selected() const {
    return selected_;
  }
  // True when the Etc tab is active, the one stack tab with a cursor.
  bool on_stackable_tab() const;
  // Whether the Shop tab is active. The shop is a screen rather than a list, so
  // the controller checks this to tell Enter on the tab bar from Enter on an
  // item.
  bool on_shop_tab() const;
  // The same for the Bank tab, the other door out of the panel.
  bool on_bank_tab() const;
  // Whether the cursor is on the tab bar. The controller checks it to tell
  // Enter on a tab from Enter on an item, and the layout checks it so the tab
  // menu opens at the panel's left instead of past the item columns.
  bool on_tab_bar() const {
    return zone_ == kZoneTabs;
  }
  // The open tab, as an InventoryTab. The Multi-Sell screen opens on it, at the
  // row the cursor is on.
  int active_tab() const {
    return active_tab_;
  }
  // The stack the Etc cursor is on, as an index into the character's stacks.
  // Etc lists only some of them, so the row number isn't the index. -1 when the
  // tab has no rows.
  int selected_stack() const;
  // The column the item menu opens at, from the panel's left border: past the
  // cursor, name and slot cells, so it covers stats rather than a name. The
  // panel computes it because the name column follows its width.
  int menu_column() const;

  // Marks the active tab as opened, which turns off its gold. Called when the
  // player moves onto a tab and when the panel gets focus, since a tab already
  // under the cursor has been seen.
  void MarkActiveTabSeen();

  // Turns the border gold during a level-up celebration. The bag arrives at
  // level 4, and a card in the middle of the screen doesn't say where to look.
  // The panel keeps no timer: whoever turned it on turns it off.
  void SetHighlighted(bool highlighted) {
    highlighted_ = highlighted;
  }

  // Whether the bag is drawn over the whole screen. Set from the render, like
  // SetHighlighted: the controller knows which screen is showing, and the panel
  // only needs it to label the button.
  void SetExpanded(bool expanded) {
    expanded_ = expanded;
  }

 private:
  // The two menus OnMenuEvent drives: the equip menu on Equip, and the
  // {Inspect, Sell, Multi-Sell} menu on Etc.
  Screen OnEquipMenuEvent(ftxui::Event event, ScrollPanel& scroll_panel,
                          StatPreset gear);
  Screen OnStackMenuEvent(ftxui::Event event);

  // Whether the border is gold. Set from outside and read by the render. It
  // isn't part of the panel's own state.
  bool highlighted_ = false;
  // See SetExpanded.
  bool expanded_ = false;
  // See SetWidth.
  int width_ = kRightColumnMin;

  // The columns the Equip tab draws at the panel's width, leaving out mechanics
  // the account hasn't unlocked.
  ItemColumns Columns() const;

  // The two focus zones: the tab bar on top and the active tab's list below.
  enum Zone { kZoneTabs, kZoneList };

  // The tab menu's entries. It opens only on a tab that lists something (Shop
  // and Expand are doors, and Enter goes through them), so Sort always has a
  // list to act on.
  enum TabMenuEntry : int { kTabMenuSort = 0, kTabMenuClose = 1 };

  // Sorts the active tab.
  void SortActiveTab();
  // How many stacks the Etc tab lists, which is every stack in the bag.
  // Currencies are kept in the purse and don't take slots on any tab.
  int EtcRowCount() const;
  // The Token tab: two read-only columns, the shop's currencies beside the
  // bosses' soul shards. Nothing on it is selectable, so it has no cursor, and
  // Enter on the bar above it opens the {Sort, Close} menu.
  ftxui::Element RenderCurrencySheet();
  // Rows on the Token tab: as many as its longer column.
  int CurrencyRowCount() const;
  // How many rows of the sheet fit at once, from the last frame's box.
  int CurrencySheetHeight() const;
  // Scrolls the sheet `delta` rows, which is what Up and Down do on a tab with
  // no cursor. Clamped rather than wrapped, since this is a page position, not
  // a cursor going round a ring.
  void ScrollCurrencySheet(int delta);

  // What OpenMenu opens, for each tab.
  void OpenStackMenu();
  void OpenEquipMenu();
  // OpenEquipMenu's three passes, in order. What the player hasn't reached is
  // hidden before what the item can't take (the first is about the player, the
  // second about the item), and the gold goes on last, on what remains.
  void HideLockedFeatures();
  void HideRefusedUpgrades(const EquipInstance& equip);
  void HighlightUnusedUpgrades();
  // The Equip tab's menu for a spare Arcane Symbol, which offers different
  // entries from every other item.
  void OpenSymbolMenu(const EquipInstance& symbol);
  // Wraps the active tab's body in the titled window with the tab bar on top.
  ftxui::Element RenderContent(ftxui::Component menu);
  // One row of the Equip list, with the cursor and any red or dimmed parts. The
  // ftxui::Menu's row transform.
  ftxui::Element RenderRow(const ftxui::EntryState& state);
  // The Expand tab, drawn right-aligned in the tab row.
  ftxui::Element RenderExpandTab(bool row_selected) const;
  // The key handlers, one for each place the cursor can be: the tab bar, the
  // Etc list and the Equip list.
  bool OnTabBarEvent(const ftxui::Event& event,
                     const std::function<void()>& on_enter,
                     const std::function<void()>& on_expand);
  bool OnStackListEvent(const ftxui::Event& event,
                        const std::function<void()>& on_enter);
  bool OnEquipListEvent(const ftxui::Event& event,
                        const std::function<void()>& on_enter);
  // The Equip tab's body, rebuilding rows_ and entries_ from the equip
  // inventory: column headers over an ftxui::Menu, which drives its item menu.
  // The stack tabs use the shared RenderStackList instead.
  ftxui::Element RenderOwnEquipList(ftxui::Component menu);
  // Whether the active tab's list has no rows to move into.
  bool ActiveTabEmpty() const;
  // Rows in the list below the tab bar for the active tab. The bar isn't
  // counted, and the Shop tab has no list.
  int ListCount() const;
  // The cursor's position in the panel's vertical ring: the tab bar is stop 0
  // and the list rows are the stops after it.
  int CursorStop() const;
  // Moves the cursor `delta` stops around that ring, including the tab bar.
  // Down from the last row returns to the bar, and Up from the bar goes to the
  // last row, so both edges follow one rule.
  void MoveCursor(int delta);
  // The content tabs this character has unlocked, left to right. Locked tabs
  // are left out rather than greyed, so the bar just ends early. Expand isn't
  // one of these, since it is a door rather than a page.
  std::vector<int> VisibleTabs() const;
  // Moves one stop along the bar, including Expand. The bar wraps: Right from
  // Expand goes to the first tab, and Left from the first tab goes to Expand.
  void StepTab(int direction);

  CharacterInstance& character_;
  // Not const, because opening a tab and following a gold trail are recorded on
  // the account.
  AccountInstance& account_;
  int& panel_focus_;
  Zone zone_ = kZoneTabs;  // which focus zone has the cursor
  // Set by ftxui::reflect on the highlighted row each render.
  ftxui::Box cursor_box_;
  // Also set on the tab bar, so a tab menu knows which row to open under.
  ftxui::Box bar_box_;
  // Also set on the Token tab's scrolling frame, which tells Up and Down how
  // far they may scroll the sheet.
  ftxui::Box sheet_box_;
  // The first sheet row on screen.
  int currency_scroll_ = 0;
  // The selected row on the Equip tab (the ftxui::Menu index).
  int selected_ = 0;
  // When the selection last moved, for scrolling a long name.
  SelectionClock name_clock_;
  int selected_stack_ = 0;  // selected row of the Etc list
  // Whether the cursor is on the Expand tab. Kept separate from active_tab_
  // because the list keeps showing its tab, and Left moves back onto it.
  bool on_expand_ = false;
  int active_tab_ = kEquipTab;
  std::vector<InventoryRowState> rows_;
  // Labels built from rows_ for ftxui::Menu.
  std::vector<std::string> entries_;
  ItemMenu menu_;       // Equip tab context menu
  ItemMenu sell_menu_;  // Etc tab context menu
  ItemMenu tab_menu_;   // the {Sort, Close} menu Enter opens on a tab
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_PANELS_INVENTORY_PANEL_H_
