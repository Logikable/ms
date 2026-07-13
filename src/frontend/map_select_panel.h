/* MapSelectPanel is the modal for choosing where to farm. The left half lists
 * every map -- its name, the rounded average level of its mobs, and how many
 * spawn at once -- weakest first. The right half shows the mobs of whichever
 * map the cursor is on, so the player can see what they would be fighting
 * before they commit. Opening the panel puts the cursor on the map being
 * farmed, which is how the player sees where they are.
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
  // Moves the cursor `delta` rows, clamped to the ends of the list.
  void MoveCursor(int delta);
  ftxui::Element Render() const;
  // Key into GameState::maps of the highlighted map; empty when there are none.
  std::string selected_map() const;

 private:
  ftxui::Element RenderMapList() const;
  ftxui::Element RenderMobTable() const;

  const GameState& state_;
  // Map keys in display order: lowest-level map first, ties broken by name.
  // Fixed at construction, since maps are static data.
  std::vector<std::string> order_;
  int selected_ = 0;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_MAP_SELECT_PANEL_H_
