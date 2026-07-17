/* MapSelectPanel is the modal for choosing where to farm. The left half lists
 * maps -- name, and mobs' mean level weighted by how many of each spawn --
 * lowest first, one level band at a time. The right half shows the mobs of
 * whichever map the cursor is on -- each mob's name, level, and how many spawn
 * at once -- so the player can see what they would be fighting before they
 * commit. Opening the panel puts the cursor on the map being farmed, band and
 * all, which is how the player sees where they are.
 *
 * Travel is free: every map is always selectable, with no adjacency or unlock
 * gating. The panel is a view -- it moves its own cursor but never writes to
 * the game state; the controller reads selected_map() when the player confirms.
 */
#ifndef MS_SRC_FRONTEND_MAP_SELECT_PANEL_H_
#define MS_SRC_FRONTEND_MAP_SELECT_PANEL_H_

#include <string>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/game_state.h"

namespace ms {

class MapSelectPanel {
 public:
  explicit MapSelectPanel(const GameState& state);

  // Puts the cursor back on the map being farmed. Call when the screen opens.
  void Reset();
  // Moves the cursor `delta` rows, clamped to the ends of the band.
  void MoveCursor(int delta);
  // Moves `delta` level bands, clamped to the ends. The cursor goes to the top
  // of the band it lands on.
  void ChangePage(int delta);
  ftxui::Element Render() const;
  // Key into GameState::maps of the highlighted map; empty when there are none.
  std::string selected_map() const;

 private:
  ftxui::Element RenderMapList() const;
  ftxui::Element RenderMobTable() const;

  const GameState& state_;
  // Map keys per level band, each band in display order: lowest weighted level
  // first, ties broken by name. Always kBandCount long, bands may be empty.
  // Fixed at construction, since maps are static data.
  std::vector<std::vector<std::string>> pages_;
  int page_ = 0;
  // Row within pages_[page_].
  int selected_ = 0;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_MAP_SELECT_PANEL_H_
