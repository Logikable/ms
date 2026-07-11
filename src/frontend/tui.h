/* Tui owns the ftxui event loop and all panel and component state for one
 * game session. Construct with a GameState reference, then call Run() which
 * blocks until the user exits (Ctrl-C). Screen-state logic lives in
 * TuiController; Tui handles the ftxui component tree and rendering.
 */
#ifndef MS_SRC_FRONTEND_TUI_H_
#define MS_SRC_FRONTEND_TUI_H_

#include <chrono>

#include "ftxui/component/component.hpp"
#include "src/frontend/ap_alloc_panel.h"
#include "src/frontend/character_panel.h"
#include "src/frontend/equipped_panel.h"
#include "src/frontend/inspect_panel.h"
#include "src/frontend/inventory_panel.h"
#include "src/frontend/scroll_panel.h"
#include "src/frontend/star_force_panel.h"
#include "src/frontend/trace_recover_panel.h"
#include "src/frontend/tui_controller.h"
#include "src/game_state.h"

namespace ms {

class Tui {
 public:
  explicit Tui(GameState& state);
  void Run();

 private:
  ftxui::Element RenderFrame();
  ftxui::Element RenderMain();
  ftxui::Element RenderExpBar();
  // Advances farming by the wall-clock time since the previous call.
  void AdvanceFarmingTick();
  bool OnEvent(ftxui::Event event);

  GameState& state_;
  std::chrono::steady_clock::time_point last_farming_update_;
  // Shared with equip_panel_, inventory_panel_, and Container::Tab; mutated by
  // controller_ (Tab) and panels (Equip/Unequip actions).
  int panel_focus_ = kEquipPanel;

  // Main view panels (always constructed; rendered in kMain and kItemMenu).
  CharacterPanel char_panel_;
  EquippedPanel equip_panel_;
  InventoryPanel inventory_panel_;
  ScrollPanel scroll_panel_;
  InspectPanel inspect_panel_;
  InspectPanel trace_inspect_panel_;  // left panel on kTraceRecover (preview)
  ApAllocPanel ap_alloc_panel_;
  StarForcePanel star_force_panel_;
  TraceRecoverPanel trace_recover_panel_;

  // Screen-state machine: owns screen_ and event-handling logic.
  TuiController controller_;

  // ftxui components built in Run().
  ftxui::Component equip_component_;
  ftxui::Component inventory_component_;
  ftxui::Component char_component_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_TUI_H_
