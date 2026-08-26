/* CombatPanel shows the fight the character is currently in: which map, how
 * close the next swing is, and how much life the mob being hit has left.
 * Focusing it and pressing Enter opens the map selection screen, which is how
 * the player travels.
 *
 * Read-only, like the fight it watches. It renders whatever CombatSim last
 * stepped to, so the bars move only because combat moved -- there is no
 * animation state of its own to fall out of sync. Produces a new Element on
 * each Render() call.
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
  // Rows the panel takes on screen, borders included. It grows with the number
  // of mob types engaged, so whatever shares the column with it has to ask
  // rather than assume -- see MainLayout.
  int Height() const;
  // The columns the panel may take, borders included. The same width the
  // character panel above it is given -- they share a column -- and all it
  // buys down here is room for a long map name. See CharacterPanel::SetWidth.
  void SetWidth(int width) {
    width_ = width;
  }
  // on_travel fires when the player presses Enter with the panel focused.
  ftxui::Component MakeComponent(std::function<void()> on_travel);

 private:
  // Width inside the window's border, which the map row is padded to.
  int ContentWidth() const {
    return width_ - 2;
  }

  // Display name of the map being farmed, or "-" when there is none.
  std::string MapName() const;

  // See SetWidth.
  int width_ = kLeftColumnMin;
  const GameState& state_;
  const CombatSim& sim_;
  int& panel_focus_;
  // How long the map row has been the focused one, for sliding a long map
  // name under it. Mutable because the render is where the move is noticed.
  mutable SelectionClock name_clock_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_PANELS_COMBAT_PANEL_H_
