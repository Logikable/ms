/* MapSelectPanel is the modal for choosing where to farm. The left half lists
 * maps -- name, and mobs' mean level weighted by how many of each spawn --
 * lowest first, one level band at a time. The right half shows the mobs of
 * whichever map the cursor is on -- each mob's name, level, and how many spawn
 * at once -- so the player can see what they would be fighting before they
 * commit. Opening the panel puts the cursor on the map being farmed, band and
 * all, which is how the player sees where they are.
 *
 * The bands are a chip bar over the list, in the game's one tab style, and the
 * bar is a cursor stop above the first map -- the same shape the bag and the
 * shop use. Left and Right belong to the bar and do nothing in the list.
 *
 * Enter opens a context menu on the highlighted map -- Move, Inspect, Close --
 * anchored to its row, the way the shop's is. The panel puts the menu up
 * itself, because a centred window has no fixed place on screen to hang one
 * from.
 *
 * Travel is free: every map is always selectable, with no adjacency or unlock
 * gating. The panel is a view -- it moves its own cursor but never writes to
 * the game state; the controller reads selected_map() when the player confirms.
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
  // Moves the cursor `delta` stops. The band's rows and the chip bar over them
  // are one ring: Up off the first map lands on the bar, and Up again wraps to
  // the last map. It does not roll over into the neighbouring band -- bands are
  // Left and Right, and one key should move one thing.
  void MoveCursor(int delta);
  // Moves `delta` level bands, clamped to the ends, and puts the cursor on the
  // top of the band it lands on. Does nothing unless the cursor is on the chip
  // bar: in the list these keys would quietly change the list under it.
  void ChangePage(int delta);
  ftxui::Element Render() const;
  // Key into GameState::maps of the highlighted map; empty when there are none.
  std::string selected_map() const;

  // Opens the context menu over the highlighted map, stepping the cursor off
  // the band bar onto it. Does nothing on a band holding no maps.
  void OpenMenu();
  bool menu_open() const;
  // Drives the context menu and says what should be on screen afterwards:
  // kMapMenu while it stays up, kMobInspect for Inspect, kMapSelect once it
  // closes, and kMain for Move -- the caller is what actually changes the map,
  // since the panel never writes to the game state. The menu closes itself on
  // the way out, so the screen it opens is not drawn with it still standing.
  Screen OnMenuEvent(ftxui::Event event);

 private:
  // Where the cursor stands: 0 is the chip bar, then one stop per map.
  int CursorStop() const;

  ftxui::Element RenderBandBar() const;
  ftxui::Element RenderMapList() const;
  ftxui::Element RenderMobTable() const;
  // The row the context menu opens on, measured from the top of the window.
  int MenuRow() const;

  const GameState& state_;
  // Map keys per level band, each band in display order: lowest weighted level
  // first, ties broken by name. Always kBandCount long, bands may be empty.
  // Fixed at construction, since maps are static data.
  std::vector<std::vector<std::string>> pages_;
  int page_ = 0;
  // Row within pages_[page_]. Held while the cursor is on the bar, so stepping
  // off the bar comes back to the row it left.
  int selected_ = 0;
  enum Zone { kZoneTabs, kZoneList };
  Zone zone_ = kZoneList;
  ItemMenu menu_;
  bool menu_open_ = false;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_MAP_SELECT_PANEL_H_
