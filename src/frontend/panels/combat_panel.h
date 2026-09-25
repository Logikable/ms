/* CombatPanel shows the character's current fight: the map, how close the next
 * attack is, and how much HP the mobs being hit have left. Focusing it and
 * pressing Enter opens the map selection screen, which is how the player
 * travels.
 *
 * It is read-only. It draws whatever state CombatSim last stepped to, so the
 * bars move only when combat moves, and it has no animation state of its own to
 * fall out of sync. Each Render() call builds a new Element.
 */
#ifndef MS_SRC_FRONTEND_PANELS_COMBAT_PANEL_H_
#define MS_SRC_FRONTEND_PANELS_COMBAT_PANEL_H_

#include <functional>
#include <string>

#include "ftxui/component/component.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/combat/fight.h"
#include "src/frontend/panel_widths.h"
#include "src/frontend/widgets/marquee.h"
#include "src/game_state.h"

namespace ms {

class CombatPanel {
 public:
  CombatPanel(const GameState& state, const CombatSim& sim, int& panel_focus);
  ftxui::Element Render() const;
  // The rows the panel takes on screen, borders included. It grows with the
  // number of engaged mob types, so whatever shares its column has to ask
  // instead of assuming (see MainLayout).
  int Height() const;
  // The width the panel may take, borders included. It matches the character
  // panel above it, since they share a column, and here it only gives a long
  // map name more room. See CharacterPanel::SetWidth.
  void SetWidth(int width) {
    width_ = width;
  }
  // on_travel fires when the player presses Enter while the panel has focus.
  ftxui::Component MakeComponent(std::function<void()> on_travel);

 private:
  // The width inside the window's border, which the map row is padded to.
  int ContentWidth() const {
    return width_ - 2;
  }

  // The display name of the map being farmed, or "-" when there is none.
  std::string MapName() const;

  // See SetWidth.
  int width_ = kLeftColumnMin;
  const GameState& state_;
  const CombatSim& sim_;
  int& panel_focus_;
  // How long the map row has had focus, for scrolling a long map name. Mutable
  // because the render is where the change is noticed.
  mutable SelectionClock name_clock_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_PANELS_COMBAT_PANEL_H_
