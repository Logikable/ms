/* The screen a cleared boss fight's [Analysis] opens: who did what damage.
 *
 * The party's totals for the whole fight are on top, then the players, then a
 * table by skill for whoever the Players cursor is on. "All" is the party's
 * tables merged by skill name. A solo fight has no Players panel, and its table
 * takes the space.
 *
 * Tab switches the arrows between Players and the table, and only the table
 * scrolls. The panel keeps its own copy of the numbers, so it outlives the
 * fight.
 */
#ifndef MS_SRC_FRONTEND_SCREENS_BOSS_ANALYSIS_PANEL_H_
#define MS_SRC_FRONTEND_SCREENS_BOSS_ANALYSIS_PANEL_H_

#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/combat/damage_breakdown.h"

namespace ms {

class BossAnalysisPanel {
 public:
  // Every player's table with their name, and the fight's length in seconds.
  // Puts the cursor on All with the arrows on Players.
  void Open(std::vector<PlayerBreakdown> players, double seconds);
  // Tab and the arrows. Returns false for anything else, including Escape,
  // since the controller handles leaving.
  bool OnEvent(const ftxui::Event& event);
  ftxui::Element Render() const;

  // Whether there is a Players panel, which needs more than one player.
  bool party() const {
    return players_.size() > 1;
  }
  bool table_focused() const {
    return table_focused_;
  }
  // The Players row under the cursor: 0 is All, then players() in order.
  int player_cursor() const {
    return player_cursor_;
  }
  int skill_cursor() const {
    return skill_cursor_;
  }
  // The players, highest damage first, as the panel lists them.
  const std::vector<PlayerBreakdown>& players() const {
    return players_;
  }
  // The table the cursor has selected, highest damage first.
  const std::vector<BreakdownRow>& table() const;

 private:
  ftxui::Element RenderTotals() const;
  ftxui::Element RenderPlayers() const;
  ftxui::Element RenderTable() const;
  // The number of skill rows the table shows at once.
  int VisibleSkillRows() const;

  std::vector<PlayerBreakdown> players_;
  // Parallel to players_: each player's total.
  std::vector<double> player_totals_;
  std::vector<BreakdownRow> merged_;
  double party_total_ = 0.0;
  double seconds_ = 0.0;
  bool table_focused_ = false;
  int player_cursor_ = 0;
  int skill_cursor_ = 0;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_BOSS_ANALYSIS_PANEL_H_
