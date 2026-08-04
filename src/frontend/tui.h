/* Tui owns the ftxui event loop and all panel and component state for one
 * game session. Construct with a GameState reference, then call Run() which
 * blocks until the player leaves. Screen-state logic lives in TuiController;
 * Tui handles the ftxui component tree and rendering.
 *
 * Tui is also what decides when the game is written to disk, because it is
 * what owns the loop the player is leaving: it saves on the way out of Run()
 * however that happened -- the quit dialog, Ctrl+C, or a closed window -- and
 * every kAutosaveInterval in between.
 */
#ifndef MS_SRC_FRONTEND_TUI_H_
#define MS_SRC_FRONTEND_TUI_H_

#include <chrono>
#include <string>

#include "ftxui/component/component.hpp"
#include "src/combat/fight.h"
#include "src/frontend/panels/character_panel.h"
#include "src/frontend/panels/combat_panel.h"
#include "src/frontend/panels/equipped_panel.h"
#include "src/frontend/panels/inventory_panel.h"
#include "src/frontend/screens/buy_panel.h"
#include "src/frontend/screens/inspect_panel.h"
#include "src/frontend/screens/map_select_panel.h"
#include "src/frontend/screens/scroll_panel.h"
#include "src/frontend/screens/sell_panel.h"
#include "src/frontend/screens/shop_panel.h"
#include "src/frontend/screens/skill_inspect_panel.h"
#include "src/frontend/screens/star_force_panel.h"
#include "src/frontend/screens/trace_recover_panel.h"
#include "src/frontend/tui_controller.h"
#include "src/game_state.h"

namespace ms {

// How long the game goes between automatic saves. Short enough that closing
// the window costs a player almost nothing, long enough that a few-kilobyte
// write is nowhere near the 300ms combat tick in cost.
constexpr std::chrono::seconds kAutosaveInterval(30);

class Tui {
 public:
  // `save_path` is where the game is written; empty turns saving off, which
  // is how the workbench avoids ever touching a player's file.
  explicit Tui(GameState& state, std::string save_path = "");
  void Run();

 private:
  ftxui::Element RenderFrame();
  ftxui::Element RenderMain();
  ftxui::Element RenderExpBar();
  // Advances the world by the time since the previous call: combat, and the
  // playtime the session is accruing. Both come off one reading of a
  // monotonic clock, so they cannot disagree about how long a tick was.
  void Tick();
  // Writes the game to save_path_. No-op when saving is off. Failures are
  // logged and swallowed: a save that cannot be written is not a reason to
  // take the game down, and the previous save is still there.
  void Save();
  // Save(), but only once kAutosaveInterval has passed since the last one.
  void AutosaveIfDue();
  bool OnEvent(ftxui::Event event);

  GameState& state_;
  // Where the game is written, or empty when saving is off.
  std::string save_path_;
  std::chrono::steady_clock::time_point last_save_;
  // The live fight: stepped by the ticker, read by the combat panel.
  CombatSim combat_sim_;
  std::chrono::steady_clock::time_point last_combat_update_;
  // Shared with equip_panel_, inventory_panel_, and Container::Tab; mutated by
  // controller_ (Tab) and panels (Equip/Unequip actions).
  int panel_focus_ = kEquipPanel;

  // Main view panels (always constructed; rendered in kMain and kItemMenu).
  CharacterPanel char_panel_;
  CombatPanel combat_panel_;
  EquippedPanel equip_panel_;
  InventoryPanel inventory_panel_;
  ScrollPanel scroll_panel_;
  InspectPanel inspect_panel_;
  InspectPanel trace_inspect_panel_;  // left panel on kTraceRecover (preview)
  SkillInspectPanel skill_inspect_panel_;
  StarForcePanel star_force_panel_;
  TraceRecoverPanel trace_recover_panel_;
  SellPanel sell_panel_;
  MapSelectPanel map_select_panel_;
  ShopPanel shop_panel_;
  BuyPanel buy_panel_;

  // Screen-state machine: owns screen_ and event-handling logic.
  TuiController controller_;

  // ftxui components built in Run().
  ftxui::Component equip_component_;
  ftxui::Component inventory_component_;
  ftxui::Component char_component_;
  ftxui::Component combat_component_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_TUI_H_
