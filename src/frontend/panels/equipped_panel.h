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
#include "src/protos/equip.pb.h"

namespace ms {

class EquippedPanel {
 public:
  EquippedPanel(CharacterInstance& character, int& panel_focus);
  ftxui::Component MakeComponent(std::function<void()> on_enter);
  void OpenMenu();
  // Handles Up/Down/Escape/Return for the item context menu and executes the
  // selected action. Returns the next screen state.
  Screen OnMenuEvent(ftxui::Event event, int& panel_focus,
                     ScrollPanel& scroll_panel);

  ItemMenu& menu() {
    return menu_;
  }
  int selected() const {
    return selected_;
  }
  // The screen row the highlighted item was last drawn on, for anchoring the
  // item menu beside it. Read from the render rather than worked out from
  // selected(), which is a position in the data and not on the screen.
  //
  // Filled during the previous render, so it is one frame behind. That is what
  // is wanted: the menu opens on a keypress, which does not move the list, so
  // the row it was drawn on last frame is the row it is on now.
  int cursor_row() const {
    return cursor_box_.y_min;
  }
  // Returns the slot of the currently highlighted item, or
  // EQUIP_SLOT_UNSPECIFIED if the list is empty.
  EquipSlot selected_slot() const;

 private:
  CharacterInstance& character_;
  int& panel_focus_;
  int selected_ = 0;
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
