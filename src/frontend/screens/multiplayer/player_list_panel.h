/* PlayerListPanel is the screen listing everyone connected: their name and
 * level, in the order they arrived.
 *
 * The cursor moves through the list and then the Close button below it, all one
 * ring, and Enter on a player opens a menu beside that row: Inspect and Close
 * for anyone, plus Trade for everyone except you. Everyone is listed, including
 * the reader, since a list that hid you would look like a broken connection
 * when you are the only player online.
 *
 * The panel only displays. It draws the last snapshot it was given and never
 * asks the connection for anything: the controller reads what the cursor is on
 * and does the asking.
 */
#ifndef MS_SRC_FRONTEND_SCREENS_PLAYER_LIST_PANEL_H_
#define MS_SRC_FRONTEND_SCREENS_PLAYER_LIST_PANEL_H_

#include <string>

#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/item_menu.h"
#include "src/multiplayer/client.h"

namespace ms {

class PlayerListPanel {
 public:
  PlayerListPanel();

  // The lobby as the panel should draw it. Passed in every frame.
  void SetSnapshot(const MultiplayerSnapshot& snapshot);
  // Puts the cursor back at the top of the list. Call when the screen opens.
  void Reset();
  // Moves the cursor `delta` stops, wrapping at the ends. Close is the last
  // stop in the ring, so Down from the last player lands on it and Down again
  // wraps to the top.
  void MoveCursor(int delta);
  ftxui::Element Render() const;

  // The menu Enter opens on a player. Trade is left out entirely on your own
  // row rather than dimmed, since trading with yourself is never possible.
  void OpenMenu();
  void CloseMenu();
  void MoveMenuCursor(int delta);

  // Whether the cursor is on Close rather than a player.
  bool on_close() const;
  // The player under the cursor. Empty while it is on Close.
  std::string selected_account() const;
  std::string selected_name() const;
  // The menu entry under the cursor, as a PlayerMenuItem.
  int menu_selected() const;
  bool menu_open() const {
    return menu_open_;
  }

 private:
  // The cursor's position, brought back into the ring. Players arrive and leave
  // under it, and a cursor past the end would waste the keypress that should
  // have moved it.
  int Cursor() const;
  // The row the menu opens at, measured from the top of the window.
  int MenuRow() const;

  MultiplayerSnapshot snapshot_;
  // The cursor's position: one stop per player, then Close.
  int cursor_ = 0;
  ItemMenu menu_;
  bool menu_open_ = false;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_PLAYER_LIST_PANEL_H_
