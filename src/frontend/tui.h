/* Tui owns the ftxui event loop and all panel and component state for one game
 * session. Construct it with a GameState, then call Run(), which blocks until
 * the player leaves. TuiController holds the screen-state logic; Tui handles
 * the ftxui component tree and rendering.
 *
 * Two pieces of session bookkeeping live here too, because the loop knows when
 * the player has left. SavePolicy saves the game on the way out of Run(),
 * however that happened (the quit dialog, Ctrl+C, a closed window).
 * ProgressWatcher detects the level and job changes that raise a card.
 */
#ifndef MS_SRC_FRONTEND_TUI_H_
#define MS_SRC_FRONTEND_TUI_H_

#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
#include <random>
#include <string>

#include "ftxui/component/component.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "src/audio/music_director.h"
#include "src/audio/music_player.h"
#include "src/combat/battle_analysis.h"
#include "src/combat/fight.h"
#include "src/combat/offline.h"
#include "src/frontend/celebration.h"
#include "src/frontend/keybinds.h"
#include "src/frontend/main_layout.h"
#include "src/frontend/panels/character_panel.h"
#include "src/frontend/panels/combat_panel.h"
#include "src/frontend/panels/equipped_panel.h"
#include "src/frontend/panels/inventory_panel.h"
#include "src/frontend/panels/menu_panel.h"
#include "src/frontend/progress_watcher.h"
#include "src/frontend/screens/all_stats_panel.h"
#include "src/frontend/screens/analysis_panel.h"
#include "src/frontend/screens/bank_panel.h"
#include "src/frontend/screens/boss_select_panel.h"
#include "src/frontend/screens/buff_info_panel.h"
#include "src/frontend/screens/buy_panel.h"
#include "src/frontend/screens/cube_panel.h"
#include "src/frontend/screens/hyper_stat_inspect_panel.h"
#include "src/frontend/screens/inspect_panel.h"
#include "src/frontend/screens/job_inspect_panel.h"
#include "src/frontend/screens/jukebox_panel.h"
#include "src/frontend/screens/keybinds_panel.h"
#include "src/frontend/screens/link_skill_panel.h"
#include "src/frontend/screens/map_select_panel.h"
#include "src/frontend/screens/mob_inspect_panel.h"
#include "src/frontend/screens/multi_sell_panel.h"
#include "src/frontend/screens/options_panel.h"
#include "src/frontend/screens/party_select_panel.h"
#include "src/frontend/screens/player_inspect_panel.h"
#include "src/frontend/screens/player_list_panel.h"
#include "src/frontend/screens/scroll_panel.h"
#include "src/frontend/screens/sell_equip_panel.h"
#include "src/frontend/screens/sell_panel.h"
#include "src/frontend/screens/shop_panel.h"
#include "src/frontend/screens/skill_inspect_panel.h"
#include "src/frontend/screens/star_force_panel.h"
#include "src/frontend/screens/trace_recover_panel.h"
#include "src/frontend/screens/trade_panel.h"
#include "src/frontend/tui_controller.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/multiplayer/session.h"
#include "src/save.h"

namespace ms {

class Tui {
 public:
  // `save_path` is where the game is saved; empty turns saving off, which is
  // how the workbench avoids touching a player's file. `server` is host:port;
  // empty means single-player. With `bgm` false the null sound device is used,
  // which the game treats the same as a machine with no sound card.
  Tui(GameState& state, std::string save_path = "", std::string server = "",
      bool bgm = true);
  // Shows the card with what the character earned while the game was closed.
  // Called before Run(), so the player sees it first. A report not worth a card
  // shows nothing.
  void ShowOfflineReport(OfflineReport report);
  void Run();

 private:
  // Connects each main-view panel's Enter to its controller call.
  void BuildComponents();
  // The component tree Run() drives: the panel ring, the renderer, the event
  // handler that can end the loop, and the key map wrapped around all of it.
  ftxui::Component MakeRoot(ftxui::ScreenInteractive& screen);
  // The whole frame: the current screen, with a celebration card over it if one
  // is up.
  ftxui::Element RenderFrame();
  // The Multi-Sell screen, with its "Are you sure?" dialog over the list.
  ftxui::Element RenderMultiSell();
  // Whichever screen the controller is showing, without the celebration.
  ftxui::Element RenderScreen();
  // The menu the open screen shows over the main layout, or null if none is
  // open. See the definition for where each is anchored.
  ftxui::Element OpenMenu(const MainWidths& widths);
  ftxui::Element RenderMain();
  // A panel expanded to the whole terminal, which RenderMain draws while the
  // player has one open. See the definition.
  ftxui::Element RenderExpandedPanel();
  // Shows `dialog` centred over the main view.
  ftxui::Element OverMain(ftxui::Element dialog);
  // The dialogs, and the screens that take more than a line to build.
  ftxui::Element ApAllocDialog();
  ftxui::Element SkillLearnDialog();
  ftxui::Element JobAdvanceDialog();
  // How much of one currency to offer, over the trade screen.
  ftxui::Element TradeAmountDialog();
  // How much of a balance to move, and in which direction.
  ftxui::Element BankAmountDialog();
  ftxui::Element RenderBankInspect();
  ftxui::Element TradeItemAmountDialog();
  ftxui::Element TradeConfirmDialog();
  ftxui::Element RenderTradeInspect();
  ftxui::Element QuitDialog();
  // "Reset Farm Hyper Stats?": the free way to unspend them.
  ftxui::Element HyperResetDialog();
  // "Reset V Matrix?": the same, from the V page.
  ftxui::Element VMatrixResetDialog();
  // The presets that the menu's preset can swap with, in a column over the
  // screen the row is on. Cancel is below them, where every dialog's buttons
  // are.
  ftxui::Element PresetMoveDialog();
  // The reroll confirmation, over the lines it would replace, each coloured by
  // rank as on the tab. Locked lines aren't listed, since they aren't affected.
  ftxui::Element AbilityRerollDialog();
  // The confirmation for buying a buff permanently. The price turns red and
  // [Confirm] greys out if the character can't afford it, but the player can
  // still see the price.
  ftxui::Element BuffBuyDialog();
  // The box a menu entry opened, above the corner menu it came from.
  ftxui::Element RenderMenuBox();
  // The party screen, with its member menu or confirmation over it.
  ftxui::Element RenderParty();
  // The inspected member, with an item's card over it when the player has
  // pressed Enter on a row.
  ftxui::Element RenderPlayerInspect();
  // "Kick Bree from the party?", over the party screen.
  ftxui::Element PartyConfirmDialog();
  // The server's message, over whatever screen is up.
  ftxui::Element PartyNoticeDialog();
  // "Fight Normal Zakum?", over the boss screen.
  ftxui::Element BossConfirmDialog();
  // A notice rather than a question: the action can't be done, and the only
  // button says so. Every screen that raises a notice draws this.
  ftxui::Element NoticeDialog();
  // "Stop fighting Zakum?", over the fight.
  ftxui::Element BossAbortDialog();
  // The fight screen, or the fight list if there is no fight.
  ftxui::Element RenderBossFight();
  // The dialog over the arena, or null while the fight is in progress.
  ftxui::Element BossFightOverlay();
  ftxui::Element RenderShopInspect();
  // The inspect screen for a buy-back row: the item as it was sold, rebuilt
  // from the shelf plus the catalog.
  ftxui::Element RenderBuyBackInspect(const BuyBackEntry& entry);
  ftxui::Element RenderJobInspect();
  ftxui::Element RenderTraceRecover();
  // The item as it is, the panel, and the item with one more star. It caches
  // both items so the result screen can draw the same three columns behind its
  // window, by which time a destroyed item is no longer in the bag.
  ftxui::Element RenderStarForce();
  ftxui::Element RenderStarForceResult();
  // The three columns, from the cache. Both callers draw it.
  ftxui::Element StarForceColumns();
  // The cubing screen: the cube shelf beside the item's card, with the
  // confirmation centred over both when one is open.
  ftxui::Element RenderCubing();
  ftxui::Element RenderInspect();
  ftxui::Element RenderScroll();
  // Advances the game by the time since the previous call: combat, and the
  // session's playtime. Both use one reading of a monotonic clock, so they
  // always agree on the tick's length.
  void Tick();
  // Restarts the session for the character just chosen on the character select:
  // a new fight, and both watchers reset for them. Everything else (the
  // account, keys, music) belongs to the player, not the character, and is
  // unchanged.
  void StartPlayingCharacter();
  // Raises the card for whatever the watcher noticed. Called after events as
  // well as ticks, because combat levels a character during a tick, while an
  // advancement happens during an event.
  void NoticeProgress();
  // Sets the volume for where the player is and tells the director where that
  // is. The director chooses what plays; this sets how loud.
  void UpdateMusic();
  // The track the map or boss names, or empty if neither names one.
  std::string NormalTrack() const;
  // The panel the player is looking at, or kNoPanel. panel_focus_ still names a
  // panel while the shop is open, but not one the player can see.
  Panel FocusedPanel() const;
  bool OnEvent(ftxui::Event event);

  GameState& state_;
  SavePolicy save_policy_;
  // The connection to other players, or null for single-player.
  std::unique_ptr<MultiplayerSession> multiplayer_;
  ProgressWatcher progress_watcher_;
  // The card and the lit panels, which are usually empty.
  Celebration celebration_;
  // The live fight, advanced by the ticker and read by the combat panel.
  CombatSim combat_sim_;
  // The music. Silent in a build made with --define=audio=off, on a machine
  // with no sound device, and with --nobgm.
  MusicPlayer music_player_;
  // A separate random stream from the game's, so picking the next track doesn't
  // change the rolls a seeded run should repeat.
  std::mt19937 music_rng_{std::random_device{}()};
  // Chooses what plays now and next, in the account's playback mode.
  MusicDirector music_director_{music_player_, music_rng_};
  // What the Battle Analysis tool has measured. The ticker feeds it only while
  // the player is looking at the map fight.
  BattleAnalysis analysis_;
  std::chrono::steady_clock::time_point last_combat_update_;
  // Whether the ticker uses a boss fight's faster step. This is the only thing
  // the loop and ticker threads share; everything else about the fight belongs
  // to the loop.
  std::atomic<bool> in_boss_fight_ = false;
  // Shared with equip_panel_, inventory_panel_ and Container::Tab. Changed by
  // controller_ (Tab) and by panels (Equip/Unequip).
  int panel_focus_ = kEquipPanel;

  // The player's key map, built from the save's bindings. Every key the
  // components see has passed through it.
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
  // The second card: kTraceRecover's recovered item, and kStarForce's next
  // star.
  InspectPanel preview_inspect_panel_;
  SkillInspectPanel skill_inspect_panel_;
  StarForcePanel star_force_panel_;
  CubePanel cube_panel_;
  // The item the star force screen last drew, and the same item with one more
  // star. Kept after the attempt because the result window is drawn over these
  // two cards, and a destroyed item is no longer in the bag.
  std::optional<EquipInstance> star_force_before_;
  std::optional<EquipInstance> star_force_after_;
  TraceRecoverPanel trace_recover_panel_;
  SellPanel sell_panel_;
  SellEquipPanel sell_equip_panel_;
  MultiSellPanel multi_sell_panel_;
  MapSelectPanel map_select_panel_;
  MobInspectPanel mob_inspect_panel_;
  BossSelectPanel boss_select_panel_;
  PartySelectPanel party_select_panel_;
  // Everyone connected, and the player selected with Inspect in either list.
  PlayerListPanel player_list_panel_;
  TradePanel trade_panel_;
  PlayerInspectPanel player_inspect_panel_;
  // The card for an item another player is wearing. It is separate from
  // inspect_panel_ because a set card counts the pieces the wearer has on, and
  // the wearer here is someone else.
  InspectPanel player_item_panel_;
  // The job's book, viewed before taking the advancement.
  JobInspectPanel job_inspect_panel_;
  // The key bindings screen, opened from the Settings box.
  KeybindsPanel keybinds_panel_;
  OptionsPanel options_panel_;
  JukeboxPanel jukebox_panel_;
  // Every stat on one screen, opened from the Character panel's last row.
  AllStatsPanel all_stats_panel_;
  HyperStatInspectPanel hyper_stat_inspect_panel_;
  // One buff's effects and its two prices, opened from the Buffs tab's menu.
  BuffInfoPanel buff_info_panel_;
  ShopPanel shop_panel_;
  BuyPanel buy_panel_;
  // The bag above the account's shared storage, opened from the Bank tab.
  BankPanel bank_panel_;
  LinkSkillPanel link_skill_panel_;

  // Screen-state machine: owns screen_ and the event-handling logic.
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
