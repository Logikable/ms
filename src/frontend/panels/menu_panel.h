/* The menu pinned to the bottom-right corner: the way into everything that is
 * not a panel of its own.
 *
 * It takes the corner over from the hotkeys tip at the level the tip retires,
 * so the corner is never empty and never holds both. Entries sit in one row,
 * Left and Right move between them, and Enter opens the one under the cursor.
 *
 * Entries arrive as the character reaches them, and the row is laid out from
 * the right: Settings holds the corner from the start so the panel is never an
 * empty box, Analysis sits to its left, and Boss and Party arrive left of both
 * at 110. A build with no multiplayer in it has no Party entry at all.
 *
 * An entry either opens a screen -- Boss does -- or opens a box that stands on
 * the corner and lists what it leads to. There is one box, whichever entry
 * raised it, so the two behave alike and the panel holds one cursor.
 */
#ifndef MS_SRC_FRONTEND_PANELS_MENU_PANEL_H_
#define MS_SRC_FRONTEND_PANELS_MENU_PANEL_H_

#include <functional>
#include <string>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/combat/battle_analysis.h"
#include "src/game_state.h"

namespace ms {

// What the menu can open. Ordered as they are drawn, left to right.
enum class MenuEntry {
  kBoss,
  kParty,
  kAnalysis,
  kSettings,
};

// What the Settings box holds, top to bottom.
enum class SettingsEntry {
  kKeybinds,
  kOptions,
};

// What the Analysis box holds, top to bottom. The first entry starts the
// measurement or stops it, whichever the tool is not already doing.
enum class AnalysisEntry {
  kStartStop,
  kView,
};

class MenuPanel {
 public:
  // Rows the panel takes on screen, borders included.
  static constexpr int kHeight = 3;

  MenuPanel(const GameState& state, const BattleAnalysis& analysis,
            int& panel_focus);

  // Moves the cursor `delta` entries, coming out the other end.
  void MoveCursor(int delta);
  ftxui::Element Render() const;
  // The entry under the cursor. Settings for a character with nothing else
  // yet, which is the one entry that is always there.
  MenuEntry selected() const;
  // on_open fires when the player presses Enter with the panel focused.
  ftxui::Component MakeComponent(std::function<void(MenuEntry)> on_open);

  // The box: what `entry` leads to, listed in a box that stands on the panel
  // and draws over whatever is behind it. It opens with the cursor still on
  // the menu row below, which is what the player presses Up to leave.
  void OpenBox(MenuEntry entry);
  void CloseBox();
  bool box_open() const {
    return box_open_;
  }
  // Which entry the open box hangs from. Meaningless while it is closed.
  MenuEntry box_entry() const {
    return box_entry_;
  }
  // Moves the cursor `delta` stops up the box. The menu row is a stop of the
  // same ring, so the cursor comes back out of the box the way it went in.
  void MoveBoxCursor(int delta);
  // The entry the cursor is on, or -1 while it is still on the menu row.
  int box_cursor() const {
    return box_cursor_;
  }
  // Columns between the right edge of the open box and the right edge of the
  // panel. RenderBox() already carries it; a caller wanting to place the box
  // itself asks here.
  int BoxRightMargin() const;
  SettingsEntry selected_settings_entry() const;
  AnalysisEntry selected_analysis_entry() const;
  ftxui::Element RenderBox() const;

  // The save key that latches the Boss entry's gold, so it stops being new
  // once the player has opened the screen it leads to.
  static const char* boss_seen_key() {
    return kBossSeenKey;
  }

 private:
  static constexpr char kBossSeenKey[] = "boss";

  // The entries this character has, left to right. Asked rather than stored:
  // one arrives on a level-up, which the panel is never told about.
  std::vector<MenuEntry> Entries() const;

  // What `entry`'s box holds, top to bottom. Empty for an entry that opens a
  // screen instead of a box.
  std::vector<std::string> BoxEntries(MenuEntry entry) const;

  // Columns the open box takes, borders included.
  int BoxWidth() const;

  const GameState& state_;
  const BattleAnalysis& analysis_;
  int& panel_focus_;
  bool box_open_ = false;
  MenuEntry box_entry_ = MenuEntry::kSettings;
  // Which entry of the box the cursor is on, or -1 for the menu row below it.
  int box_cursor_ = -1;
  // Which entry the cursor is on, as an index into Entries(). An index rather
  // than a MenuEntry, so the cursor follows the row as entries arrive to its
  // left: reaching 110 slides it onto Boss, which is the gold one and the
  // reason the panel is worth looking at that minute.
  int cursor_ = 0;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_PANELS_MENU_PANEL_H_
