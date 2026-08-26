/* InventoryPanel shows the character's inventory as three tabs: Equip (equip-
 * tab items as a navigable menu), Use, and Etc (read-only stackable lists).
 *
 * Focus moves top-to-bottom through two zones, Down descending and Up
 * ascending, matching the character panel. The top zone is the Equip/Use/Etc
 * tab bar: there Left/Right switch tabs and the active tab is drawn white to
 * show the row is selected. Down descends into the tab's item list (only when
 * it is non-empty); there Up off the top row returns to the tab bar and Enter
 * opens the item context menu via the on_enter callback passed to
 * MakeComponent(). The Use and Etc tabs have no menu actions beyond Sell.
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
#include "src/frontend/panel_widths.h"
#include "src/frontend/screens/scroll_panel.h"
#include "src/frontend/types.h"
#include "src/frontend/widgets/inventory_list.h"
#include "src/frontend/widgets/item_menu.h"
#include "src/frontend/widgets/marquee.h"
#include "src/item/equip_instance.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"

namespace ms {

class InventoryPanel {
 public:
  InventoryPanel(CharacterInstance& character, AccountInstance& account,
                 int& panel_focus);
  ftxui::Component MakeComponent(std::function<void()> on_enter);
  void OpenMenu();
  // Handles Up/Down/Escape/Return for the item context menu and executes the
  // selected action. Returns the next screen state. On the Equip tab this
  // drives the equip menu; on Use/Etc it drives the {Sell, Close} menu.
  Screen OnMenuEvent(ftxui::Event event, ScrollPanel& scroll_panel);

  // The context menu for the active tab: the equip menu on Equip, the sell menu
  // on Use/Etc.
  ItemMenu& menu();
  // The screen row the highlighted item was last drawn on, for anchoring the
  // item menu beside it. Read from the render, not from selected(), which is a
  // position in the data and stops agreeing once the list scrolls. One frame
  // behind, which is right: opening the menu does not move the list.
  int cursor_row() const {
    return cursor_box_.y_min;
  }
  // The columns the panel may take, borders included -- its column's width,
  // which the layout works out from the terminal's. What a wide terminal
  // brings goes to the Equip tab's name column; the stack tabs, whose rows
  // are a name and a count, keep theirs.
  void SetWidth(int width) {
    width_ = width;
  }
  int selected() const {
    return selected_;
  }
  // True when a Use or Etc tab is active (as opposed to the Equip tab).
  bool on_stackable_tab() const;
  // Whether the Shop tab is the active one. The shop is a screen rather than a
  // list, so the controller asks this to tell Enter on the tab bar apart from
  // Enter on an item.
  bool on_shop_tab() const;
  // The active Use/Etc tab's item category, or ITEM_CATEGORY_UNSPECIFIED on the
  // Equip tab.
  ItemCategory active_category() const;
  // Which tab is open, as an InventoryTab. The Multi-Sell screen opens on it,
  // and on whichever row the cursor stands on there.
  int active_tab() const {
    return active_tab_;
  }
  // The selected stack row on the active Use/Etc tab.
  int selected_stack() const {
    return selected_stack_;
  }

  // Records the active tab as opened, which is what puts its gold out. Called
  // by the panel when the player steps onto a tab, and by the controller when
  // focus arrives on the panel -- a tab already open under the cursor has been
  // seen just as surely as one stepped onto.
  void MarkActiveTabSeen();

  // Lights the panel's border gold, to send the player's eye to it while a
  // level-up is being celebrated -- the bag arrives at level 4, and a card in
  // the middle of the screen does not say where to look. The panel keeps no
  // clock of its own: whoever lit it turns it off again.
  void SetHighlighted(bool highlighted) {
    highlighted_ = highlighted;
  }

 private:
  // Whether the border is currently lit gold. Set from outside, read by the
  // render; no part of the panel's own state machine.
  bool highlighted_ = false;
  // See SetWidth.
  int width_ = kRightColumnMin;

  // The columns the Equip tab's name cell gets, which is whatever the cells
  // after it leave of the panel's width.
  int NameWidth() const {
    return ItemNameWidthFor(width_ - 2);
  }

  // The two vertical focus zones: the Equip/Use/Etc tab bar on top, the active
  // tab's item list below. Down descends into the list, Up ascends back.
  enum Zone { kZoneTabs, kZoneList };

  // What OpenMenu opens, by tab.
  void OpenStackMenu();
  void OpenEquipMenu();
  // The Equip tab's menu on a spare Arcane Symbol, which offers a different
  // set from every other item.
  void OpenSymbolMenu(const EquipInstance& symbol);
  // Wraps the active tab's body in the titled window with the tab bar on top.
  ftxui::Element RenderContent(ftxui::Component menu);
  // One row of the Equip list, with the cursor and whatever the row has to say
  // in red or dim. The ftxui::Menu's row transform.
  ftxui::Element RenderRow(const ftxui::EntryState& state);
  // The key handlers, one per place the cursor can be: the tab bar, a Use/Etc
  // list, the Equip list.
  bool OnTabBarEvent(const ftxui::Event& event,
                     const std::function<void()>& on_enter);
  bool OnStackListEvent(const ftxui::Event& event,
                        const std::function<void()>& on_enter);
  bool OnEquipListEvent(const ftxui::Event& event,
                        const std::function<void()>& on_enter);
  // Rebuilds rows_/entries_ from the equip inventory and returns the Equip tab
  // body (column headers + the navigable menu, or "(empty)").
  ftxui::Element RenderEquipList(ftxui::Component menu);
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
  // The tabs this character has unlocked, left to right. Locked tabs are
  // absent rather than greyed, so the bar simply ends early.
  std::vector<int> VisibleTabs() const;
  // Moves the active tab one step along the visible bar. The ends are walls:
  // Left on the first tab and Right on the last do nothing.
  void StepTab(int direction);

  CharacterInstance& character_;
  // Not const: opening a tab and walking a gold trail are the account's to
  // record, so the panel writes as well as reads.
  AccountInstance& account_;
  int& panel_focus_;
  Zone zone_ = kZoneTabs;  // which focus zone holds the cursor
  // Written by ftxui::reflect on the highlighted row each render.
  ftxui::Box cursor_box_;
  int selected_ = 0;
  // When the selection last moved, for sliding a long name under its column.
  SelectionClock
      name_clock_;          // selected row on the Equip tab (ftxui::Menu index)
  int selected_stack_ = 0;  // selected row on the active Use/Etc tab
  int active_tab_ = 0;      // 0 = Equip, 1 = Use, 2 = Etc
  std::vector<InventoryRowState> rows_;
  std::vector<std::string>
      entries_;         // labels derived from rows_ for ftxui::Menu
  ItemMenu menu_;       // Equip tab context menu.
  ItemMenu sell_menu_;  // Use/Etc tab context menu.
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_PANELS_INVENTORY_PANEL_H_
