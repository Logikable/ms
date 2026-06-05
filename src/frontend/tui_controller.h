/* TuiController owns the screen-state machine for the TUI. It handles
 * keyboard events and drives transitions between screens. Tui holds a
 * TuiController and delegates event handling; tests can construct
 * TuiController directly without the ftxui event loop.
 *
 * panel_focus is owned by the caller and shared with the panel components
 * so Container::Tab can read it; TuiController mutates it on Tab.
 */
#ifndef MS_SRC_FRONTEND_TUI_CONTROLLER_H_
#define MS_SRC_FRONTEND_TUI_CONTROLLER_H_

#include "ftxui/component/event.hpp"
#include "src/frontend/bag_panel.h"
#include "src/frontend/equipped_panel.h"
#include "src/frontend/scroll_panel.h"
#include "src/frontend/types.h"
#include "src/game_state.h"
#include "src/protos/equip.pb.h"

namespace ms {

class TuiController {
 public:
  // panel_focus is a reference shared with panel components and
  // Container::Tab; the controller mutates it as focus changes.
  TuiController(GameState& state, EquippedPanel& equip_panel,
                BagPanel& bag_panel, ScrollPanel& scroll_panel,
                int& panel_focus);

  // Open the equip or bag context menu. Called from MakeComponent callbacks.
  void OpenEquipMenu();
  void OpenBagMenu();

  // Returns true if the event was consumed. Returns false for navigation
  // events in kScrollSelect; the caller should forward those to the scroll
  // panel.
  bool OnEvent(ftxui::Event event);

  Screen screen() const {
    return screen_;
  }
  const ScrollResult& scroll_result() const {
    return scroll_result_;
  }

 private:
  GameState& state_;
  EquippedPanel& equip_panel_;
  BagPanel& bag_panel_;
  ScrollPanel& scroll_panel_;
  int& panel_focus_;
  Screen screen_ = kMain;
  EquipSlot scroll_slot_ = EQUIP_SLOT_UNSPECIFIED;
  int scroll_index_ = 0;
  ScrollResult scroll_result_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_TUI_CONTROLLER_H_
