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

#include <string>

#include "ftxui/component/event.hpp"
#include "src/equip_instance.h"
#include "src/frontend/equipped_panel.h"
#include "src/frontend/inventory_panel.h"
#include "src/frontend/map_select_panel.h"
#include "src/frontend/scroll_panel.h"
#include "src/frontend/sell_panel.h"
#include "src/frontend/star_force_panel.h"
#include "src/frontend/trace_recover_panel.h"
#include "src/frontend/types.h"
#include "src/game_state.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"

namespace ms {

class TuiController {
 public:
  // panel_focus is a reference shared with panel components and
  // Container::Tab; the controller mutates it as focus changes.
  TuiController(GameState& state, EquippedPanel& equip_panel,
                InventoryPanel& inventory_panel, ScrollPanel& scroll_panel,
                StarForcePanel& star_force_panel,
                TraceRecoverPanel& trace_recover_panel, SellPanel& sell_panel,
                MapSelectPanel& map_select_panel, int& panel_focus);

  // Open the equip or bag context menu. Called from MakeComponent callbacks.
  void OpenEquipMenu();
  void OpenInventoryMenu();
  // Float the AP-allocation confirm dialog over the main view: assign one AP to
  // `field` (max=false) or all available AP (max=true), pending confirmation.
  void OpenApConfirm(StatField field, bool max);
  // Open the map selection screen, on the map being farmed.
  void OpenMapSelect();

  // Message for the pending AP-allocation confirm, e.g. "Assign 5 AP to STR?".
  std::string ap_confirm_message() const;
  bool ap_confirm_cancel_selected() const {
    return ap_confirm_cancel_;
  }

  // Returns true if the event was consumed.
  bool OnEvent(ftxui::Event event);

  Screen screen() const {
    return screen_;
  }
  const ScrollResult& scroll_result() const {
    return scroll_result_;
  }
  const StarForceResult& star_force_result() const {
    return star_force_result_;
  }
  const TraceRecoveryResult& trace_recovery_result() const {
    return trace_recovery_result_;
  }
  // Returns the item being scrolled while in kScrollSelect or kScrollResult,
  // or nullptr otherwise.
  const EquipInstance* scroll_item() const;
  // Returns the item being inspected while in kInspect, or nullptr otherwise.
  // May be an EquipTrace if the selected bag item was destroyed.
  const EquipTabItem* inspect_item() const;
  // Returns the item being star forced while in kStarForce, or nullptr
  // otherwise. Do not call in kStarForceResult (item may be destroyed).
  const EquipInstance* star_force_item() const;
  // Returns the trace being recovered while in kTraceRecover, or nullptr.
  const EquipTabItem* trace_recover_item() const;

 private:
  bool OnItemMenuEvent(ftxui::Event event);
  bool OnInspectEvent(ftxui::Event event);
  bool OnScrollSelectEvent(ftxui::Event event);
  bool OnScrollResultEvent(ftxui::Event event);
  bool OnApConfirmEvent(ftxui::Event event);
  bool OnStarForceEvent(ftxui::Event event);
  bool OnStarForceResultEvent(ftxui::Event event);
  bool OnTraceRecoverEvent(ftxui::Event event);
  bool OnTraceRecoverResultEvent(ftxui::Event event);
  bool OnSellEvent(ftxui::Event event);
  bool OnMapSelectEvent(ftxui::Event event);

  GameState& state_;
  EquippedPanel& equip_panel_;
  InventoryPanel& inventory_panel_;
  ScrollPanel& scroll_panel_;
  StarForcePanel& star_force_panel_;
  TraceRecoverPanel& trace_recover_panel_;
  SellPanel& sell_panel_;
  MapSelectPanel& map_select_panel_;
  int& panel_focus_;
  Screen screen_ = kMain;
  EquipSlot scroll_slot_ = EQUIP_SLOT_UNSPECIFIED;
  int scroll_index_ = 0;
  EquipSlot inspect_slot_ = EQUIP_SLOT_UNSPECIFIED;
  int inspect_index_ = 0;
  EquipSlot star_force_slot_ = EQUIP_SLOT_UNSPECIFIED;
  int star_force_index_ = 0;
  int trace_index_ = 0;
  ItemCategory sell_category_ = ITEM_CATEGORY_UNSPECIFIED;
  int sell_index_ = 0;
  StatField ap_confirm_field_ = STAT_FIELD_UNSPECIFIED;
  bool ap_confirm_max_ = false;
  bool ap_confirm_cancel_ = false;  // which button is highlighted in the dialog
  ScrollResult scroll_result_;
  StarForceResult star_force_result_;
  TraceRecoveryResult trace_recovery_result_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_TUI_CONTROLLER_H_
