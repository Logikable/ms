/* EquippedPanel shows what the character is wearing, as two tabs: Gear, and
 * the Arcane Symbols they carry into Arcane River. Each entry displays name,
 * stat bonuses, and remaining upgrade slots; a symbol shows its level, how far
 * along the next one it is, and the Arcane Force it grants.
 *
 * Focus moves top-to-bottom through zones, the way the bag's does. The top
 * zone is the tab bar: there Left and Right switch tabs. Down descends into
 * the tab's list, and Up off the top row returns to the bar. Enter opens the
 * item context menu via the on_enter callback passed to MakeComponent().
 *
 * The [Expand] button at the right of the tab bar is the last zone rather than
 * part of the bar, for the reason the bag's buttons are: Up from the bar
 * reaches it in one key. It arrives with the bar itself -- before there is a
 * second tab there is no bar to hang it in.
 *
 * Call MakeComponent() exactly once; the returned Component captures references
 * to internal state, so the panel object must outlive the Component.
 */
#ifndef MS_SRC_FRONTEND_PANELS_EQUIPPED_PANEL_H_
#define MS_SRC_FRONTEND_PANELS_EQUIPPED_PANEL_H_

#include <chrono>
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
#include "src/frontend/widgets/equipped_list.h"
#include "src/frontend/widgets/item_menu.h"
#include "src/frontend/widgets/marquee.h"
#include "src/protos/equip.pb.h"

namespace ms {

class EquippedPanel {
 public:
  // The tabs, in the order the bar lists them.
  enum Tab { kGearTab, kSymbolTab };

  EquippedPanel(CharacterInstance& character, AccountInstance& account,
                int& panel_focus);
  // `on_enter` is Enter on a row. `on_expand` is the tab bar's button, which
  // opens the panel up to the whole screen and closes it again.
  ftxui::Component MakeComponent(std::function<void()> on_enter,
                                 std::function<void()> on_expand = nullptr);
  void OpenMenu();
  // Handles Up/Down/Escape/Return for the item context menu and executes the
  // selected action. Returns the next screen state.
  Screen OnMenuEvent(ftxui::Event event, ScrollPanel& scroll_panel);

  // The context menu for the active tab: the gear menu on Gear, the symbol
  // menu on Symbols.
  ItemMenu& menu();
  int selected() const {
    return selected_;
  }
  // Which tab is open. The controller asks so that Enter on a symbol reaches
  // the symbol's screens rather than an equip's.
  int active_tab() const {
    return active_tab_;
  }
  // The screen row the highlighted item was last drawn on, for anchoring the
  // item menu beside it. Read from the render, not from selected(), which is a
  // position in the data and not on the screen. One frame behind, which is
  // right: opening the menu does not move the list.
  int cursor_row() const {
    return cursor_box_.y_min;
  }
  // Returns the slot of the currently highlighted item, or
  // EQUIP_SLOT_UNSPECIFIED if the list is empty or the cursor is on the bar.
  EquipSlot selected_slot() const;

  // Lights the panel's border gold, to send the player's eye to it while a
  // level-up is being celebrated -- this panel arrives at level 3, and a card
  // in the middle of the screen does not say where to look. The panel keeps no
  // clock of its own: whoever lit it turns it off again.
  void SetHighlighted(bool highlighted) {
    highlighted_ = highlighted;
  }

  // The columns the panel may take, borders included -- its column's width,
  // which the layout works out from the terminal's. What a wide terminal
  // brings goes to the name column, up to the longest name the game ships.
  void SetWidth(int width) {
    width_ = width;
  }

  // Whether the panel is currently drawn over the whole screen. Set from the
  // render, like SetHighlighted: the screen the player is on is the
  // controller's to know, and the panel only needs it to label the button.
  void SetExpanded(bool expanded) {
    expanded_ = expanded;
  }
  // The column the item menu hangs at, measured from the panel's left border:
  // past the cursor and the name and slot cells, so the menu covers an item's
  // stats rather than its name.
  int menu_column() const;

 private:
  // Which focus zone holds the cursor. The bar and the button it carries are
  // stops in the same ring as the rows, so one pair of keys walks the whole
  // panel.
  enum Zone { kZoneTabs, kZoneList, kZoneButtons };

  // One row of the list, with the cursor and a dim pass over anything worn
  // that is doing nothing. The ftxui::Menu's row transform.
  ftxui::Element RenderRow(const ftxui::EntryState& state);
  // Refills entries_/slots_/inactive_ from the active tab.
  void RebuildRows();
  // The titled window around the tab bar and the list.
  ftxui::Element RenderContent(ftxui::Component menu);
  ftxui::Element RenderTabBar(bool row_selected, bool button_focused) const;
  // The tabs the character has reached. Symbols arrives with Arcane River.
  std::vector<int> VisibleTabs() const;
  // Moves `direction` tabs, stopping at the ends of the bar.
  void StepTab(int direction);
  // Moves the cursor `delta` stops through the ring the bar and the rows make.
  void MoveCursor(int delta);
  // Where the cursor stands in that ring: 0 on the bar, the row plus one below.
  int CursorStop() const;
  // The active tab's rows as they stand. Asked of the character rather than of
  // the last render, so a keypress that arrives before the first one still
  // finds the list that is really there. `slide` is what the selected row's
  // name is sliding by; zero holds every name at its head.
  std::vector<EquippedRow> Rows(
      std::chrono::steady_clock::duration slide) const;
  int ListCount() const;
  // Whether the bar is a stop in the ring, which it is only once there is a
  // second tab to reach. With one tab the list wraps on itself, as it did
  // before there were tabs at all.
  bool HasTabBar() const;
  bool OnTabBarEvent(const ftxui::Event& event);
  bool OnButtonEvent(const ftxui::Event& event,
                     const std::function<void()>& on_expand);
  bool OnListEvent(const ftxui::Event& event,
                   const std::function<void()>& on_enter);
  // The header row over the active tab's columns.
  std::string Header() const;
  // The columns the name cell gets, which is whatever the cells after it
  // leave of the panel's width.
  int NameWidth() const;

  CharacterInstance& character_;
  // Not const: walking a gold trail is recorded on the account.
  AccountInstance& account_;
  int& panel_focus_;
  // See SetWidth.
  int width_ = kRightColumnMin;
  int active_tab_ = kGearTab;
  Zone zone_ = kZoneList;
  int selected_ = 0;
  // When the selection last moved, for sliding a long name under its column.
  SelectionClock name_clock_;
  bool highlighted_ = false;
  // See SetExpanded.
  bool expanded_ = false;
  std::vector<std::string> entries_;
  // Parallel to entries_: the byte length of each row's name cell, so a row
  // can be drawn with its name coloured apart from the columns after it.
  std::vector<int> name_bytes_;
  // Parallel to entries_: whether the row's name is drawn gold, which the worn
  // weapon is while an upgrade waits that the player has not come to look at.
  std::vector<bool> led_;
  // Parallel to entries_: whether that item is currently doing nothing, which
  // the entry transform draws as a dimmed row.
  std::vector<bool> inactive_;
  // Written by ftxui::reflect on the highlighted row each render.
  ftxui::Box cursor_box_;
  ItemMenu menu_;
  ItemMenu symbol_menu_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_PANELS_EQUIPPED_PANEL_H_
