/* PlayerListPanel is the screen listing everyone connected: their name and
 * their level, in the order they arrived.
 *
 * The cursor walks the list and then the Close button under it, all one ring,
 * and Enter on a player raises a menu anchored to that row: Inspect and Close
 * for anybody, with Trade for everybody but you. Everybody is here, the reader
 * included -- a list that hid you would read as a broken connection to the
 * only player online.
 *
 * The panel is a view. It draws whatever snapshot it was last handed and asks
 * the connection for nothing: the controller reads what the cursor is on and
 * does the asking.
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

  // The lobby as the panel should draw it. Handed in every frame.
  void SetSnapshot(const MultiplayerSnapshot& snapshot);
  // Puts the cursor back on the top of the list. Call when the screen opens.
  void Reset();
  // Moves the cursor `delta` stops, coming out the other end. Close is the
  // last stop of the ring, so Down off the last player lands on it and Down
  // again wraps to the top.
  void MoveCursor(int delta);
  ftxui::Element Render() const;

  // The menu Enter raises on a player. Trade is hidden on your own row rather
  // than dimmed: trading yourself is not something the state is standing in
  // the way of.
  void OpenMenu();
  void CloseMenu();
  void MoveMenuCursor(int delta);

  // Whether the cursor is on Close rather than on a player.
  bool on_close() const;
  // The player the cursor is on. Empty while it is on Close.
  std::string selected_account() const;
  std::string selected_name() const;
  // Which entry the menu cursor is on, as a PlayerMenuItem.
  int menu_selected() const;
  bool menu_open() const {
    return menu_open_;
  }

 private:
  // Where the cursor stands, folded back into the ring. Players arrive and
  // leave under it, and a cursor left past the end would swallow the keypress
  // that should have moved it.
  int Cursor() const;
  // The row the menu opens on, measured from the top of the window.
  int MenuRow() const;

  MultiplayerSnapshot snapshot_;
  // Where the cursor stands: one stop per player, then Close.
  int cursor_ = 0;
  ItemMenu menu_;
  bool menu_open_ = false;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_PLAYER_LIST_PANEL_H_
