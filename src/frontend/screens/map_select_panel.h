/* MapSelectPanel is the dialog for choosing where to farm. The left half lists
 * maps (the name, and the mobs' average level weighted by how many of each
 * spawn), lowest first, one level band at a time. The right half shows the mobs
 * of the map under the cursor (each mob's name, level, and how many spawn at
 * once), so the player can see what they would fight before they commit.
 * Opening the panel puts the cursor on the map being farmed, in its band, which
 * shows the player where they are.
 *
 * The bands are a chip bar above the list, in the game's single tab style, and
 * the bar is a cursor stop above the first map, as in the bag and the shop.
 * Left and Right belong to the bar and do nothing in the list.
 *
 * Enter opens a context menu on the highlighted map (Move, Inspect, Close),
 * placed at its row, as the shop's is. The panel places the menu itself,
 * because a centred window has no fixed screen position to place it from.
 *
 * Travel is free: every map can always be selected, with no adjacency or unlock
 * gating. The panel only displays: it moves its own cursor but never changes
 * the game state, and the controller reads selected_map() when the player
 * confirms.
 */
#ifndef MS_SRC_FRONTEND_SCREENS_MAP_SELECT_PANEL_H_
#define MS_SRC_FRONTEND_SCREENS_MAP_SELECT_PANEL_H_

#include <string>
#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/frontend/types.h"
#include "src/frontend/widgets/item_menu.h"
#include "src/game_state.h"

namespace ms {

class MapSelectPanel {
 public:
  explicit MapSelectPanel(const GameState& state);

  // Puts the cursor back on the map being farmed. Call when the screen opens.
  void Reset();
  // Moves the cursor `delta` stops. The band's rows and the chip bar above them
  // form one ring: Up from the first map lands on the bar, and Up again wraps
  // to the last map. It never moves into the next band, since bands change with
  // Left and Right and one key should move one thing.
  void MoveCursor(int delta);
  // Moves `delta` level bands, stopping at the ends, and puts the cursor at the
  // top of the new band. Does nothing unless the cursor is on the chip bar,
  // since in the list these keys would change the list under the cursor.
  void ChangePage(int delta);
  ftxui::Element Render() const;
  // The GameState::maps key of the highlighted map, or empty when there are
  // none.
  std::string selected_map() const;

  // Opens the context menu on the highlighted map. Does nothing while the
  // cursor is on the band bar, which isn't a map.
  void OpenMenu();
  bool menu_open() const;
  // Handles the context menu and returns the next screen. The caller changes
  // the map, since the panel never changes the game state, and the menu closes
  // itself on the way out.
  Screen OnMenuEvent(ftxui::Event event);

 private:
  // The cursor's position: 0 is the chip bar, then one stop per map.
  int CursorStop() const;

  ftxui::Element RenderBandBar() const;
  // The force column's header for the band on screen: "AF" or "SAC" if any map
  // there requires one, and empty otherwise.
  std::string PageForceHeader() const;
  ftxui::Element RenderMapList() const;
  ftxui::Element RenderMobTable() const;
  // The row the context menu opens at, measured from the top of the window.
  int MenuRow() const;

  const GameState& state_;
  // Map keys for each level band, in display order: lowest weighted level
  // first, ties broken by name. Always kBandCount long, and bands may be empty.
  // Built at construction, since maps are static data.
  std::vector<std::vector<std::string>> pages_;
  int page_ = 0;
  // The row within pages_[page_]. Kept while the cursor is on the bar, so
  // moving off the bar returns to the same row.
  int selected_ = 0;
  enum Zone { kZoneTabs, kZoneList };
  Zone zone_ = kZoneList;
  ItemMenu menu_;
  bool menu_open_ = false;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_MAP_SELECT_PANEL_H_
