/* CharacterSelectPanel is the screen behind the menu's Characters entry: the
 * account's characters on the left, and details for the one under the cursor on
 * the right.
 *
 * The list is sorted by most recently played, with a check beside the character
 * who farms while the game is closed, which is the one the game opens on. The
 * cursor moves through the rows and then the buttons below them, all in one
 * ring, and Enter on a row opens a menu beside it.
 *
 * The card beside the list shows the Character panel's display without the
 * tabs: the name, the job, and the stats down to attack speed, from the shared
 * rows in stat_rows.h so this screen and the panel always agree. It is computed
 * with the account applied (Link Skills, account level, the autoswap setting),
 * so the numbers match what playing the character shows. Left and Right move
 * its Farm/Boss chips, the same pair as that tab. Both windows are a fixed
 * height, so nothing moves as the cursor moves.
 *
 * The panel only displays. It reads the roster and moves its own cursor; every
 * change to the account (playing a character, creating, deleting, moving the
 * check) is done by the controller through //src/roster.h.
 */
#ifndef MS_SRC_FRONTEND_SCREENS_CHARACTER_SELECT_PANEL_H_
#define MS_SRC_FRONTEND_SCREENS_CHARACTER_SELECT_PANEL_H_

#include <string>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/character/character.h"
#include "src/character/stat_preset.h"
#include "src/frontend/widgets/item_menu.h"
#include "src/game_state.h"
#include "src/roster.h"

namespace ms {

// The rows each window takes, borders included, and the number of characters
// the list fits before scrolling. Fixed rather than fitted, so the buttons are
// in the same place with one character or ten.
inline constexpr int kCharacterPanelHeight = 25;
inline constexpr int kCharacterListRows = kCharacterPanelHeight - 2 - 4;

// What Enter does where the cursor is.
enum class CharacterAction {
  // A character's row: the menu below.
  kMenu,
  kCreate,
  kQuit,
};

// Entries of that menu. Delete sits above Close and away from where the cursor
// starts, like every entry that can't be undone.
enum CharacterMenuItem : int {
  kCharacterMenuPlay = 0,
  kCharacterMenuSetOffline = 1,
  kCharacterMenuDelete = 2,
  kCharacterMenuClose = 3,
};

class CharacterSelectPanel {
 public:
  explicit CharacterSelectPanel(GameState& state);

  // Rereads the roster and puts the cursor on the character being played. Call
  // when the screen opens.
  void Reset();
  // Rereads the roster and keeps the cursor where it is, within the new list.
  // Call after anything that changes the list while the player is looking at
  // it, since the row they are on is still the one they meant.
  void Refresh();
  // Moves the cursor `delta` stops, wrapping at the ends. The button row is the
  // last stop in the ring.
  void MoveCursor(int delta);
  // Moves `delta` buttons along the row, stopping at the ends. Does nothing
  // while the cursor is in the list.
  void MoveButton(int delta);
  // Sets the card to Farm for a `delta` to the left and Boss to the right.
  // There are two chips, so the direction is the choice, as on the All Stats
  // screen. Does nothing while the cursor is on the buttons, where the arrows
  // move along the row, or for a character using one allocation for everything.
  void SwitchActivity(int delta);
  // Which activity the card shows. The Character panel's Stats tab shows the
  // same pair.
  Activity activity() const {
    return ShowsActivityBar() ? activity_ : Activity::kFarming;
  }
  ftxui::Element Render() const;

  // What pressing Enter would do where the cursor is.
  CharacterAction Chosen() const;
  // The slot under the cursor, or -1 while it is on the buttons.
  int selected_slot() const;
  // Their name, for a question that has to say who it is about.
  std::string selected_name() const;

  void OpenMenu();
  void CloseMenu();
  bool menu_open() const {
    return menu_open_;
  }
  void MoveMenuCursor(int delta);
  int menu_selected() const;

 private:
  // The cursor's position, kept within the ring however the roster changed.
  int Cursor() const;
  bool on_buttons() const;
  ftxui::Element RenderList() const;
  ftxui::Element RenderButtons() const;
  // The character card for the roster row under the cursor, computed from a
  // character rebuilt from their sheet. A placeholder while the cursor is on
  // the buttons, so the card never disappears from beside the list.
  ftxui::Element RenderCard() const;
  // Rebuilds `preview_` from the slot under the cursor, unless it already holds
  // that character. Called by the render, which is the only thing that needs
  // it.
  void PreviewSelected() const;
  // The row the menu opens at, counted inside the window.
  int MenuRow() const;
  // Whether the card shows the Farm/Boss chips: this requires the previewed
  // character's own autoswap setting and the level the allocations unlock at.
  // Without it one allocation covers everything and there is nothing to pick.
  bool ShowsActivityBar() const;

  GameState& state_;
  std::vector<RosterEntry> rows_;
  int cursor_ = 0;
  int button_ = 0;
  bool menu_open_ = false;
  ItemMenu menu_;
  // The character the card is drawn from, rebuilt against this build's own
  // catalogs, since a saved character names their items rather than describing
  // them. Mutable because the render fills it and decides which character it
  // holds.
  mutable CharacterInstance preview_;
  mutable int preview_slot_ = -1;
  Activity activity_ = Activity::kFarming;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_CHARACTER_SELECT_PANEL_H_
