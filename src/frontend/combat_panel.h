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
#ifndef MS_SRC_FRONTEND_COMBAT_PANEL_H_
#define MS_SRC_FRONTEND_COMBAT_PANEL_H_

#include <functional>
#include <string>

#include "ftxui/component/component.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/combat/fight.h"
#include "src/frontend/character_panel.h"
#include "src/game_state.h"

namespace ms {

class CombatPanel {
 public:
  // Lines up with the character panel it sits under.
  static constexpr int kTotalWidth = CharacterPanel::kTotalWidth;

  CombatPanel(const GameState& state, const CombatSim& sim, int& panel_focus);
  ftxui::Element Render() const;
  // on_travel fires when the player presses Enter with the panel focused.
  ftxui::Component MakeComponent(std::function<void()> on_travel);

 private:
  // Width inside the window's border. Rows are padded to it, which is what
  // fixes the panel at kTotalWidth -- so lay it out beside a filler(), not as a
  // bare vbox child, which would stretch it to the full terminal.
  static constexpr int kContentWidth = kTotalWidth - 2;

  // Display name of the map being farmed, or "-" when there is none.
  std::string MapName() const;

  const GameState& state_;
  const CombatSim& sim_;
  int& panel_focus_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_COMBAT_PANEL_H_
