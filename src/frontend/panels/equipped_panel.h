/* EquippedPanel shows currently equipped items as a navigable menu. Each entry
 * displays name, stat bonuses, and remaining upgrade slots. Enter opens the
 * item context menu via the on_enter callback passed to MakeComponent().
 *
 * Call MakeComponent() exactly once; the returned Component captures references
 * to internal state, so the panel object must outlive the Component.
 */
#ifndef MS_SRC_FRONTEND_PANELS_EQUIPPED_PANEL_H_
#define MS_SRC_FRONTEND_PANELS_EQUIPPED_PANEL_H_

#include <functional>
#include <string>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/screen/box.hpp"
#include "src/character/character.h"
#include "src/frontend/screens/scroll_panel.h"
#include "src/frontend/types.h"
#include "src/frontend/widgets/item_menu.h"
#include "src/frontend/widgets/marquee.h"
#include "src/protos/equip.pb.h"

namespace ms {

class EquippedPanel {
 public:
  EquippedPanel(CharacterInstance& character, int& panel_focus);
  ftxui::Component MakeComponent(std::function<void()> on_enter);
  void OpenMenu();
  // Handles Up/Down/Escape/Return for the item context menu and executes the
  // selected action. Returns the next screen state.
  Screen OnMenuEvent(ftxui::Event event, ScrollPanel& scroll_panel);

  ItemMenu& menu() {
    return menu_;
  }
  int selected() const {
    return selected_;
  }
  // The screen row the highlighted item was last drawn on, for anchoring the
  // item menu beside it. Read from the render, not from selected(), which is a
  // position in the data and not on the screen. One frame behind, which is
  // right: opening the menu does not move the list.
  int cursor_row() const {
    return cursor_box_.y_min;
  }
  // Returns the slot of the currently highlighted item, or
  // EQUIP_SLOT_UNSPECIFIED if the list is empty.
  EquipSlot selected_slot() const;

  // Lights the panel's border gold, to send the player's eye to it while a
  // level-up is being celebrated -- this panel arrives at level 3, and a card
  // in the middle of the screen does not say where to look. The panel keeps no
  // clock of its own: whoever lit it turns it off again.
  void SetHighlighted(bool highlighted) {
    highlighted_ = highlighted;
  }

 private:
  // One row of the list, with the cursor and a dim pass over anything worn
  // that is doing nothing. The ftxui::Menu's row transform.
  ftxui::Element RenderRow(const ftxui::EntryState& state);
  // The stat column of one row: the attack this job swings with, then the stat
  // its damage is built on.
  std::string RowInfo(const EquipStats& stats) const;
  // Refills entries_/slots_/inactive_ from what is worn.
  void RebuildRows();
  // The titled window around the list, or around "empty".
  ftxui::Element RenderContent(ftxui::Component menu);

  CharacterInstance& character_;
  int& panel_focus_;
  int selected_ = 0;
  // When the selection last moved, for sliding a long name under its column.
  SelectionClock name_clock_;
  bool highlighted_ = false;
  std::vector<std::string> entries_;
  std::vector<EquipSlot> slots_;
  // Parallel to entries_: whether that item is currently doing nothing, which
  // the entry transform draws as a dimmed row.
  std::vector<bool> inactive_;
  // Written by ftxui::reflect on the highlighted row each render.
  ftxui::Box cursor_box_;
  ItemMenu menu_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_PANELS_EQUIPPED_PANEL_H_
