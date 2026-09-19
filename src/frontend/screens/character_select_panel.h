/* CharacterSelectPanel is the screen behind the menu's Characters entry: the
 * account's characters on the left, whoever the cursor is on read out on the
 * right.
 *
 * The list is sorted most recently played first, with a check beside the
 * character who farms while the game is closed -- the one a launch opens on.
 * The cursor walks the rows and then the buttons under them, all one ring,
 * and Enter on a row raises a menu anchored to it.
 *
 * The card beside the list is the Character panel's own display, less the
 * tabs: the name, what they are, and the stats down to the swing, from the
 * shared rows in stat_rows.h so this screen and the panel cannot disagree.
 * Both windows are the same fixed height, so nothing moves as the cursor
 * walks.
 *
 * The panel is a view. It reads the roster and moves its own cursor; every
 * change to the account -- playing somebody, creating, deleting, moving the
 * check -- is the controller's, through //src/roster.h.
 */
#ifndef MS_SRC_FRONTEND_SCREENS_CHARACTER_SELECT_PANEL_H_
#define MS_SRC_FRONTEND_SCREENS_CHARACTER_SELECT_PANEL_H_

#include <string>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/character/character.h"
#include "src/frontend/widgets/item_menu.h"
#include "src/game_state.h"
#include "src/roster.h"

namespace ms {

// The rows each window takes, borders included, and the characters the list
// has room for before it scrolls. Fixed rather than fitted, so a roster of
// one and a roster of ten stand the buttons in the same place.
inline constexpr int kCharacterPanelHeight = 25;
inline constexpr int kCharacterListRows = kCharacterPanelHeight - 2 - 4;

// What Enter does where the cursor stands.
enum class CharacterAction {
  // A character's row: the menu below.
  kMenu,
  kCreate,
  kQuit,
};

// Entries of that menu. Delete sits above Close and away from where the
// cursor lands, as every entry there is no coming back from does.
enum CharacterMenuItem : int {
  kCharacterMenuPlay = 0,
  kCharacterMenuSetOffline = 1,
  kCharacterMenuDelete = 2,
  kCharacterMenuClose = 3,
};

class CharacterSelectPanel {
 public:
  explicit CharacterSelectPanel(GameState& state);

  // Reads the roster again and puts the cursor on the character being
  // played. Call when the screen opens.
  void Reset();
  // Reads it again and leaves the cursor where it is, held inside whatever
  // the list is now. Call after anything that changes the list under the
  // player: they are looking at a row, and it is still the row they meant.
  void Refresh();
  // Moves the cursor `delta` stops, coming out the other end. The button row
  // is the last stop of the ring.
  void MoveCursor(int delta);
  // Moves `delta` buttons along the row, clamped to its ends. Does nothing
  // while the cursor is still up in the list.
  void MoveButton(int delta);
  ftxui::Element Render() const;

  // What pressing Enter would do where the cursor stands.
  CharacterAction Chosen() const;
  // The slot the cursor is on, or -1 while it is on the buttons.
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
  // Where the cursor is, held inside the ring however the roster changed
  // under it.
  int Cursor() const;
  bool on_buttons() const;
  ftxui::Element RenderList() const;
  ftxui::Element RenderButtons() const;
  // The character card: the roster row under the cursor, read off a
  // character rebuilt from their sheet. A placeholder while the cursor is on
  // the buttons, so the card never disappears from beside the list.
  ftxui::Element RenderCard() const;
  // Rebuilds `preview_` from the slot under the cursor, unless it already
  // holds them. Called by the render, which is the only thing that needs it.
  void PreviewSelected() const;
  // The row the menu hangs from, counted inside the window.
  int MenuRow() const;

  GameState& state_;
  std::vector<RosterEntry> rows_;
  int cursor_ = 0;
  int button_ = 0;
  bool menu_open_ = false;
  ItemMenu menu_;
  // The character the card is drawn from, rebuilt against this build's own
  // catalogs -- a saved character names their items rather than describing
  // them. Mutable because the render is what fills it, and which character
  // it holds.
  mutable CharacterInstance preview_;
  mutable int preview_slot_ = -1;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_CHARACTER_SELECT_PANEL_H_
