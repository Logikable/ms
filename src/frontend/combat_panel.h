/* CombatPanel shows the fight the character is currently in: which map, how
 * close the next swing is, and how much life the mob being hit has left.
 *
 * Read-only, like the fight it watches. It renders whatever CombatSim last
 * stepped to, so the bars move only because combat moved -- there is no
 * animation state of its own to fall out of sync. Produces a new Element on
 * each Render() call.
 */
#ifndef MS_SRC_FRONTEND_COMBAT_PANEL_H_
#define MS_SRC_FRONTEND_COMBAT_PANEL_H_

#include <string>

#include "ftxui/dom/elements.hpp"
#include "src/combat/fight.h"
#include "src/frontend/character_panel.h"
#include "src/game_state.h"

namespace ms {

class CombatPanel {
 public:
  // Lines up with the character panel it sits under.
  static constexpr int kTotalWidth = CharacterPanel::kTotalWidth;

  CombatPanel(const GameState& state, const CombatSim& sim);
  ftxui::Element Render() const;

 private:
  // Width inside the window's border. Rows are padded to it, which is what
  // fixes the panel at kTotalWidth -- so lay it out beside a filler(), not as a
  // bare vbox child, which would stretch it to the full terminal.
  static constexpr int kContentWidth = kTotalWidth - 2;

  // Display name of the map being farmed, or "-" when there is none.
  std::string MapName() const;

  const GameState& state_;
  const CombatSim& sim_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_COMBAT_PANEL_H_
