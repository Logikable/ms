/* The menu pinned to the bottom-right corner, leading to everything that isn't
 * a panel of its own.
 *
 * It replaces the hotkeys tip at the level the tip retires, so the corner is
 * never empty and never holds both. Entries sit in one row. Left and Right move
 * between them, and Enter opens the one under the cursor.
 *
 * Entries appear as the character reaches them, and the row is laid out from
 * the right. Settings holds the corner from the start so the panel is never
 * empty. Multiplayer arrives at 10, Boss at 110, Dailies at the level of the
 * first Arcane Symbol, and Characters at 210, each in its place left of
 * Settings. Analysis is always at the left end. A build without multiplayer has
 * no Multiplayer entry.
 *
 * An entry either opens a screen (as Boss does) or opens a box above the corner
 * listing where it leads. There is one box whichever entry opened it, so all
 * entries behave alike and the panel has one cursor.
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

// What the menu can open, in drawing order from left to right.
enum class MenuEntry {
  kAnalysis,
  kDailies,
  kBoss,
  kMultiplayer,
  kCharacters,
  kSettings,
};

// The Settings box, top to bottom. Jukebox appears only in a build with music
// (see kAudioEnabled).
enum class SettingsEntry {
  kJukebox,
  kKeybinds,
  kOptions,
};

// The Multiplayer box, top to bottom.
enum class MultiplayerEntry {
  kPlayers,
  kParty,
};

// The Analysis box, top to bottom. The first entry starts or stops the
// measurement, whichever the tool isn't already doing.
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

  // Moves the cursor `delta` entries, wrapping at the ends.
  void MoveCursor(int delta);
  ftxui::Element Render() const;
  // The entry under the cursor. Analysis for a character with nothing else yet,
  // since every character has it.
  MenuEntry selected() const;
  // on_open fires when the player presses Enter while the panel has focus.
  ftxui::Component MakeComponent(std::function<void(MenuEntry)> on_open);

  // Opens the box listing where `entry` leads, drawn above the panel over
  // whatever is behind it. It opens with the cursor still on the menu row, and
  // the player presses Up to enter it.
  void OpenBox(MenuEntry entry);
  void CloseBox();
  bool box_open() const {
    return box_open_;
  }
  // The entry the open box belongs to. Meaningless while the box is closed.
  MenuEntry box_entry() const {
    return box_entry_;
  }
  // Moves the cursor `delta` stops up the box. The menu row is a stop in the
  // same ring, so the cursor leaves the box the way it came in.
  void MoveBoxCursor(int delta);
  // The entry the cursor is on, or -1 while it is still on the menu row.
  int box_cursor() const {
    return box_cursor_;
  }
  // Columns between the right edge of the open box and the right edge of the
  // panel. RenderBox() already applies it. A caller placing the box itself
  // reads it here.
  int BoxRightMargin() const;
  // The width of the open box, borders included. The margin above is computed
  // from it, so it must match what RenderBox draws.
  int BoxWidth() const;
  // The Settings entries in this build, top to bottom.
  static std::vector<SettingsEntry> SettingsEntries();
  SettingsEntry selected_settings_entry() const;
  MultiplayerEntry selected_multiplayer_entry() const;
  AnalysisEntry selected_analysis_entry() const;
  ftxui::Element RenderBox() const;

  // The save key that turns off the Boss entry's gold once the player has
  // opened its screen.
  static const char* boss_seen_key() {
    return kBossSeenKey;
  }

 private:
  static constexpr char kBossSeenKey[] = "boss";

  // The entries this character has, left to right. Computed each time instead
  // of stored, because a new one can appear on a level-up the panel isn't told
  // about.
  std::vector<MenuEntry> Entries() const;

  // What `entry`'s box lists, top to bottom. Empty for an entry that opens a
  // screen instead.
  std::vector<std::string> BoxEntries(MenuEntry entry) const;

  // One row of the open box, caret included. The render draws this and BoxWidth
  // measures it, so the box is never a column short.
  static std::string BoxRow(const std::string& entry, bool on_cursor = false);

  const GameState& state_;
  const BattleAnalysis& analysis_;
  int& panel_focus_;
  bool box_open_ = false;
  MenuEntry box_entry_ = MenuEntry::kSettings;
  // The box entry under the cursor, or -1 for the menu row below it.
  int box_cursor_ = -1;
  // The entry under the cursor, as an index into Entries(). An index rather
  // than a MenuEntry because entries appear to the right of Analysis, so the
  // cursor stays where the player left it.
  int cursor_ = 0;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_PANELS_MENU_PANEL_H_
