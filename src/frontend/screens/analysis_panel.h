/* The Battle Analysis overlay: what the measured period earned.
 *
 * Status first, then how much slower than GMS the game is running, then the ten
 * numbers: what was earned, and the rate of each. Every rate is per real second
 * or hour, and the slowdown row explains why damage per second reads lower than
 * the numbers shown over the monsters suggest.
 *
 * The panel only displays. It reads the tool and never controls it: starting
 * and stopping are on the menu box this overlay was opened from.
 */
#ifndef MS_SRC_FRONTEND_SCREENS_ANALYSIS_PANEL_H_
#define MS_SRC_FRONTEND_SCREENS_ANALYSIS_PANEL_H_

#include <string>

#include "ftxui/dom/elements.hpp"
#include "src/combat/battle_analysis.h"
#include "src/game_state.h"

namespace ms {

// Seconds as HH:mm:ss, for a measurement's own clock. Hours don't wrap, so a
// measurement left running overnight says so instead of starting again at zero.
std::string FormatElapsed(double seconds);

class AnalysisPanel {
 public:
  AnalysisPanel(const GameState& state, const BattleAnalysis& analysis);

  ftxui::Element Render() const;

 private:
  // The width inside the window's border. Wide enough for the longest status
  // and for a damage total in the trillions.
  static constexpr int kContentWidth = 40;

  const GameState& state_;
  const BattleAnalysis& analysis_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_ANALYSIS_PANEL_H_
