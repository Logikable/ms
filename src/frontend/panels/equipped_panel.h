/* EquippedPanel shows what the character is wearing, in two tabs: Gear, and the
 * Arcane Symbols carried into Arcane River. Each gear row shows the item's
 * columns (see ItemColumns). A symbol shows its level, its progress to the next
 * one, and the Arcane Force it grants.
 *
 * Focus moves top to bottom through zones, as in the bag. The top zone is the
 * tab bar, where Left and Right switch tabs. Once cubing unlocks the presets,
 * the Gear tab has a second row under the bar with the three gear presets,
 * which Left and Right move between. Down moves into the tab's list, and Up
 * from the top row returns to the bar. Enter opens the item context menu
 * through the on_enter callback passed to MakeComponent().
 *
 * The Expand tab sits at the far right of the bar. It is a door rather than a
 * page: selecting it shows one line saying so, and Enter opens the panel to
 * fill the screen, on its first tab. The bar wraps there: Right from Expand
 * goes to Gear, and Left from Gear goes to Expand.
 *
 * Call MakeComponent() exactly once. The returned Component holds references to
 * internal state, so the panel must outlive it.
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
#include "src/frontend/widgets/item_columns.h"
#include "src/frontend/widgets/item_menu.h"
#include "src/frontend/widgets/marquee.h"
#include "src/protos/equip.pb.h"

namespace ms {

class EquippedPanel {
 public:
  // The tabs, in bar order.
  enum Tab { kGearTab, kSymbolTab };

  EquippedPanel(CharacterInstance& character, AccountInstance& account,
                int& panel_focus);
  // `on_enter` is Enter on a row. `on_expand` is Enter on the Expand tab, which
  // opens the panel to fill the screen and closes it again.
  ftxui::Component MakeComponent(std::function<void()> on_enter,
                                 std::function<void()> on_expand = nullptr);
  void OpenMenu();
  // Handles Up, Down, Escape and Return for the item context menu and runs the
  // selected action. Returns the next screen state.
  Screen OnMenuEvent(ftxui::Event event, ScrollPanel& scroll_panel);

  // The context menu for the active tab: the gear menu on Gear, the symbol menu
  // on Symbols.
  ItemMenu& menu();
  int selected() const {
    return selected_;
  }
  // The gear preset the Gear tab shows, which the item menu, the bag's Equip
  // and every item comparison card act on. It starts on the preset in use, or
  // on Farm with the autoswap on, until the player moves along the preset row.
  StatPreset gear_preset() const {
    return gear_preset_;
  }
  // Which content tab is open. The controller checks it so Enter on a symbol
  // opens the symbol's screens rather than an equip's. Moving onto Expand
  // doesn't change it, since Expand is where the cursor is, not what the panel
  // lists.
  int active_tab() const {
    return active_tab_;
  }
  // The screen row where the highlighted item was last drawn, for placing the
  // item menu beside it. Read from the render, not from selected(), which is a
  // position in the data rather than on screen. It is one frame behind, which
  // is fine because opening the menu doesn't move the list.
  int cursor_row() const {
    return cursor_box_.y_min;
  }
  // The slot of the highlighted item, or EQUIP_SLOT_UNSPECIFIED if the list is
  // empty or the cursor is on the bar.
  EquipSlot selected_slot() const;

  // Turns the panel's border gold to draw the player's eye during a level-up
  // celebration. This panel arrives at level 3, and a card in the middle of the
  // screen doesn't say where to look. The panel keeps no timer: whoever turned
  // it on turns it off.
  void SetHighlighted(bool highlighted) {
    highlighted_ = highlighted;
  }

  // The width the panel may take, borders included: its column's width, which
  // the layout computes from the terminal's. Extra width goes to the name
  // column, up to the longest name in the game.
  void SetWidth(int width) {
    width_ = width;
  }

  // Whether the panel is drawn over the whole screen. Set from the render, like
  // SetHighlighted: the controller knows which screen is showing, and the panel
  // only needs it to label the button.
  void SetExpanded(bool expanded) {
    expanded_ = expanded;
  }

  // Lists someone else's gear instead of the player's, for the Inspect screen.
  // Enter on the preset row no longer equips one, since a party member's
  // presets aren't the reader's to switch, and the gold trail to their weapon
  // isn't drawn, since that trail is about the reader's own upgrades. Enter on
  // a row still calls on_enter, which on that screen opens the item's card
  // instead of the menu.
  void SetReadOnly(bool read_only) {
    read_only_ = read_only;
  }
  // The column the item menu opens at, measured from the panel's left border:
  // past the cursor, name and slot cells, so the menu covers the item's stats
  // rather than its name.
  int menu_column() const;

 private:
  // Which focus zone has the cursor. The bar is a stop in the same ring as the
  // rows, so one pair of keys moves through the whole panel.
  enum Zone { kZoneTabs, kZonePresets, kZoneList };

  // The three passes OpenMenu makes over the gear menu: hide what the account
  // hasn't unlocked, then what this item can't take, then place the gold trail
  // on the entries that remain.
  void HideLockedEntries();
  void HideRefusedEntries(EquipSlot slot);
  void HighlightTrail();

  // One row of the list, with the cursor, and dimmed if the worn item does
  // nothing. The ftxui::Menu's row transform.
  ftxui::Element RenderRow(const ftxui::EntryState& state);
  // Refills entries_, slots_ and inactive_ from the active tab.
  void RebuildRows();
  // The titled window around the tab bar and the list.
  ftxui::Element RenderContent(ftxui::Component menu);
  ftxui::Element RenderTabBar(bool row_selected) const;
  // The Farm/Boss/Drop row under the bar, and whether it is shown. The Gear tab
  // has it once cubing unlocks the presets.
  ftxui::Element RenderPresetBar(bool row_selected) const;
  bool ShowsPresetBar() const;
  // Moves `direction` chips along the preset row, which doesn't wrap: it is a
  // row of three, not a ring like the bar above it.
  void StepPreset(int direction);
  bool OnPresetBarEvent(const ftxui::Event& event);
  // The content tabs the character has reached, left to right. Symbols arrives
  // with Arcane River. Expand isn't one of these, since it is a door rather
  // than a page.
  std::vector<int> VisibleTabs() const;
  // Moves `direction` stops along the bar, including Expand. The bar wraps:
  // Right from Expand goes to the first tab, and Left from the first tab goes
  // to Expand.
  void StepTab(int direction);
  // Moves the cursor `delta` stops through the ring formed by the bar and the
  // rows.
  void MoveCursor(int delta);
  // The cursor's position in that ring: 0 on the bar, the row plus one below
  // it.
  int CursorStop() const;
  // The active tab's current rows. Built from the character rather than the
  // last render, so a keypress before the first render still finds the real
  // list. `slide` is how far the selected row's name has scrolled; zero keeps
  // every name at its start.
  std::vector<EquippedRow> Rows(
      std::chrono::steady_clock::duration slide) const;
  int ListCount() const;
  bool OnTabBarEvent(const ftxui::Event& event,
                     const std::function<void()>& on_expand);
  bool OnListEvent(const ftxui::Event& event,
                   const std::function<void()>& on_enter);
  // The header row over the active tab's columns.
  std::string Header() const;
  // The columns the list draws at the panel's width, leaving out mechanics the
  // account hasn't unlocked.
  ItemColumns Columns() const;

  CharacterInstance& character_;
  // Not const, because following a gold trail is recorded on the account.
  AccountInstance& account_;
  int& panel_focus_;
  // See SetWidth.
  int width_ = kRightColumnMin;
  int active_tab_ = kGearTab;
  // Whether the cursor is on the Expand tab. Kept separate from active_tab_
  // because the list keeps showing its tab, and Left moves back onto it.
  bool on_expand_ = false;
  Zone zone_ = kZoneList;
  StatPreset gear_preset_ = StatPreset::kFirst;
  int selected_ = 0;
  // When the selection last moved, for scrolling a long name.
  SelectionClock name_clock_;
  bool highlighted_ = false;
  // See SetExpanded.
  bool expanded_ = false;
  // See SetReadOnly.
  bool read_only_ = false;
  std::vector<std::string> entries_;
  // Parallel to entries_: the byte length of each row's name cell, so the name
  // can be coloured separately from the columns after it.
  std::vector<int> name_bytes_;
  // Parallel to entries_: whether the row's name is gold. The worn weapon's is,
  // while an upgrade waits that the player hasn't come to look at.
  std::vector<bool> led_;
  // Parallel to entries_: whether the row is dimmed. That is an item that does
  // nothing, or one the open preset wears only because it inherits from the
  // first preset.
  std::vector<bool> inactive_;
  // Set by ftxui::reflect on the highlighted row each render.
  ftxui::Box cursor_box_;
  ItemMenu menu_;
  ItemMenu symbol_menu_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_PANELS_EQUIPPED_PANEL_H_
