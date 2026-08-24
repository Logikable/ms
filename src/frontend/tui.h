/* Tui owns the ftxui event loop and all panel and component state for one
 * game session. Construct with a GameState reference, then call Run() which
 * blocks until the player leaves. Screen-state logic lives in TuiController;
 * Tui handles the ftxui component tree and rendering.
 *
 * Two pieces of session bookkeeping ride along, because the loop is what
 * knows the player has left: SavePolicy writes the game on the way out of
 * Run() however that happened -- the quit dialog, Ctrl+C, a closed window --
 * and ProgressWatcher spots the level and job changes a card is raised for.
 */
#ifndef MS_SRC_FRONTEND_TUI_H_
#define MS_SRC_FRONTEND_TUI_H_

#include <chrono>
#include <memory>
#include <string>

#include "ftxui/component/component.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "src/combat/battle_analysis.h"
#include "src/combat/fight.h"
#include "src/combat/offline.h"
#include "src/frontend/celebration.h"
#include "src/frontend/keybinds.h"
#include "src/frontend/panels/character_panel.h"
#include "src/frontend/panels/combat_panel.h"
#include "src/frontend/panels/equipped_panel.h"
#include "src/frontend/panels/inventory_panel.h"
#include "src/frontend/panels/menu_panel.h"
#include "src/frontend/progress_watcher.h"
#include "src/frontend/screens/all_stats_panel.h"
#include "src/frontend/screens/analysis_panel.h"
#include "src/frontend/screens/boss_select_panel.h"
#include "src/frontend/screens/buy_panel.h"
#include "src/frontend/screens/inspect_panel.h"
#include "src/frontend/screens/job_inspect_panel.h"
#include "src/frontend/screens/keybinds_panel.h"
#include "src/frontend/screens/map_select_panel.h"
#include "src/frontend/screens/mob_inspect_panel.h"
#include "src/frontend/screens/multi_sell_panel.h"
#include "src/frontend/screens/party_inspect_panel.h"
#include "src/frontend/screens/party_select_panel.h"
#include "src/frontend/screens/scroll_panel.h"
#include "src/frontend/screens/sell_equip_panel.h"
#include "src/frontend/screens/sell_panel.h"
#include "src/frontend/screens/shop_panel.h"
#include "src/frontend/screens/skill_inspect_panel.h"
#include "src/frontend/screens/star_force_panel.h"
#include "src/frontend/screens/trace_recover_panel.h"
#include "src/frontend/tui_controller.h"
#include "src/game_state.h"
#include "src/multiplayer/session.h"
#include "src/save.h"

namespace ms {

class Tui {
 public:
  // `save_path` is where the game is written; empty turns saving off, which
  // is how the workbench avoids ever touching a player's file.
  //
  // `server` is the multiplayer server as host:port; empty plays alone. The
  // connection is opened for the whole session and closed on the way out.
  Tui(GameState& state, std::string save_path = "", std::string server = "");
  // Raises the card showing what the character earned while the game was
  // closed. Called before Run(), so the first thing the player sees is what
  // they came back to; a report not worth a card raises nothing.
  void ShowOfflineReport(OfflineReport report);
  void Run();

 private:
  // Wires each main-view panel to the controller call its Enter makes.
  void BuildComponents();
  // The component tree Run() drives: the panel ring, the renderer, the event
  // handler that can end the loop, and the key map outside all of it.
  ftxui::Component MakeRoot(ftxui::ScreenInteractive& screen);
  // The whole frame: the screen the player is on, with a celebration card
  // floated over it when one is up.
  ftxui::Element RenderFrame();
  // The Multi-Sell screen, with its "Are you sure?" dialog over the list.
  ftxui::Element RenderMultiSell();
  // Whichever screen the controller is showing, celebration aside.
  ftxui::Element RenderScreen();
  ftxui::Element RenderMain();
  // Floats `dialog` centred over the main view.
  ftxui::Element OverMain(ftxui::Element dialog);
  // The dialogs, and the screens that take more than a line to build.
  ftxui::Element ApAllocDialog();
  ftxui::Element SkillLearnDialog();
  ftxui::Element JobAdvanceDialog();
  ftxui::Element QuitDialog();
  // The box a menu entry raised, standing on the corner menu it opened from.
  ftxui::Element RenderMenuBox();
  // The party screen, with its member menu or its question over it.
  ftxui::Element RenderParty();
  // The member behind Inspect, with their item's card over it when the player
  // has pressed Enter on a row.
  ftxui::Element RenderPartyInspect();
  // "Kick Bree from the party?", floated over the party screen.
  ftxui::Element PartyConfirmDialog();
  // What the server had to say, floated over whatever screen is up.
  ftxui::Element PartyNoticeDialog();
  // "Fight Normal Zakum?", floated over the boss screen.
  ftxui::Element BossConfirmDialog();
  // "Stop fighting Zakum?", floated over the fight.
  ftxui::Element BossNoticeDialog();
  ftxui::Element BossAbortDialog();
  // The fight screen, or the fight list if there is no fight.
  ftxui::Element RenderBossFight();
  // The dialog standing over the arena, or null while the fight is on.
  ftxui::Element BossFightOverlay();
  ftxui::Element RenderShopInspect();
  // The inspect screen for a row of the buy-back shelf: the item as the sale
  // left it, rebuilt from the shelf plus the catalog.
  ftxui::Element RenderBuyBackInspect(const BuyBackEntry& entry);
  ftxui::Element RenderJobInspect();
  ftxui::Element RenderTraceRecover();
  ftxui::Element RenderInspect();
  ftxui::Element RenderScroll();
  ftxui::Element RenderExpBar();
  // Advances the world by the time since the previous call: combat, and the
  // playtime the session is accruing. Both come off one reading of a
  // monotonic clock, so they cannot disagree about how long a tick was.
  void Tick();
  // Raises the card for whatever the watcher noticed. Called after events as
  // well as after ticks, because combat levels a character during a tick while
  // an advancement and the Level-Up item happen during an event.
  void NoticeProgress();
  // The panel the player is looking at, or kNoPanel when the main screen is
  // not what is in front of them. panel_focus_ still names a panel while the
  // shop is open, but it is not one they can see, so nothing there counts as
  // visited.
  Panel FocusedPanel() const;
  bool OnEvent(ftxui::Event event);

  GameState& state_;
  SavePolicy save_policy_;
  // The connection to the other players, or null for a game played alone.
  std::unique_ptr<MultiplayerSession> multiplayer_;
  ProgressWatcher progress_watcher_;
  // The card and the lit panels, or nothing most of the time.
  Celebration celebration_;
  // The live fight: stepped by the ticker, read by the combat panel.
  CombatSim combat_sim_;
  // What the Battle Analysis tool has measured. Fed by the ticker, and only
  // while the map is the fight in front of the player.
  BattleAnalysis analysis_;
  std::chrono::steady_clock::time_point last_combat_update_;
  // Shared with equip_panel_, inventory_panel_, and Container::Tab; mutated by
  // controller_ (Tab) and panels (Equip/Unequip actions).
  int panel_focus_ = kEquipPanel;

  // The player's keys, over the bindings the save carries. Every key the
  // components see has been through it.
  KeyMap keys_;

  // Main view panels (always constructed; rendered in kMain and kItemMenu).
  CharacterPanel char_panel_;
  CombatPanel combat_panel_;
  MenuPanel menu_panel_;
  AnalysisPanel analysis_panel_;
  EquippedPanel equip_panel_;
  InventoryPanel inventory_panel_;
  ScrollPanel scroll_panel_;
  InspectPanel inspect_panel_;
  InspectPanel trace_inspect_panel_;  // left panel on kTraceRecover (preview)
  SkillInspectPanel skill_inspect_panel_;
  StarForcePanel star_force_panel_;
  TraceRecoverPanel trace_recover_panel_;
  SellPanel sell_panel_;
  SellEquipPanel sell_equip_panel_;
  MultiSellPanel multi_sell_panel_;
  MapSelectPanel map_select_panel_;
  MobInspectPanel mob_inspect_panel_;
  BossSelectPanel boss_select_panel_;
  PartySelectPanel party_select_panel_;
  PartyInspectPanel party_inspect_panel_;
  // The job's book, read before the advancement is taken.
  JobInspectPanel job_inspect_panel_;
  // The keys, on a screen of their own, reached from the Settings box.
  KeybindsPanel keybinds_panel_;
  // Every stat on one screen, reached from the Character panel's last row.
  AllStatsPanel all_stats_panel_;
  ShopPanel shop_panel_;
  BuyPanel buy_panel_;

  // Screen-state machine: owns screen_ and event-handling logic.
  TuiController controller_;

  // ftxui components built in Run().
  ftxui::Component equip_component_;
  ftxui::Component inventory_component_;
  ftxui::Component char_component_;
  ftxui::Component combat_component_;
  ftxui::Component menu_component_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_TUI_H_
