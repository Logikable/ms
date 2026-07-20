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
#include "src/frontend/amount_selector.h"
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
  // Float the AP-allocation amount entry over the main view, seeded to spend up
  // to all available AP on `field` (defaulting to the max).
  void OpenApAllocate(StatField field);
  // Open the map selection screen, on the map being farmed.
  void OpenMapSelect();

  // The stat the pending AP allocation targets, and its amount selector, for
  // the dialog Tui floats over the main view.
  StatField ap_alloc_field() const {
    return ap_field_;
  }
  const AmountSelector& ap_selector() const {
    return ap_selector_;
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
  bool OnApAllocEvent(ftxui::Event event);
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
  StatField ap_field_ = STAT_FIELD_UNSPECIFIED;
  AmountSelector ap_selector_;
  ScrollResult scroll_result_;
  StarForceResult star_force_result_;
  TraceRecoveryResult trace_recovery_result_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_TUI_CONTROLLER_H_
