/* The Battle Analysis overlay: what the stretch being measured is worth.
 *
 * Status first, then how far behind GMS's clock the game is running, then the
 * ten numbers -- what was earned, and the rate each came in at. Every rate is
 * per real second or hour, and the slowdown row is why the damage per second
 * reads lower than the numbers flying off the monsters suggest.
 *
 * The panel is a view. It reads the tool and never works it: starting and
 * stopping happen on the menu box this overlay was opened from.
 */
#ifndef MS_SRC_FRONTEND_SCREENS_ANALYSIS_PANEL_H_
#define MS_SRC_FRONTEND_SCREENS_ANALYSIS_PANEL_H_

#include <string>

#include "ftxui/dom/elements.hpp"
#include "src/combat/battle_analysis.h"
#include "src/game_state.h"

namespace ms {

// Seconds as HH:mm:ss. Hours are not wrapped: a measurement left running
// overnight should say so rather than starting again at zero.
std::string FormatClock(double seconds);

class AnalysisPanel {
 public:
  AnalysisPanel(const GameState& state, const BattleAnalysis& analysis);

  ftxui::Element Render() const;

 private:
  // Columns inside the window's border. Wide enough for the longest status and
  // for a damage total in the trillions.
  static constexpr int kContentWidth = 40;

  const GameState& state_;
  const BattleAnalysis& analysis_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_ANALYSIS_PANEL_H_
