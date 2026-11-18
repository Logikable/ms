/* StatInspectPanel is the card Enter opens on a stat of the Stats tab or the
 * All Stats screen: the stat's name, then the three parts %stat splits it into
 * and the total they add up to.
 *
 * Every card is the same width, so moving between stats doesn't resize it.
 */
#ifndef MS_SRC_FRONTEND_SCREENS_STAT_INSPECT_PANEL_H_
#define MS_SRC_FRONTEND_SCREENS_STAT_INSPECT_PANEL_H_

#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/stat_rows.h"

namespace ms {

class StatInspectPanel {
 public:
  void SetStat(InspectStat stat, const StatBreakdown& breakdown) {
    stat_ = stat;
    breakdown_ = breakdown;
  }

  ftxui::Element Render() const;

  // The width the card takes, borders included.
  static int Columns();

 private:
  InspectStat stat_ = InspectStat::kStr;
  StatBreakdown breakdown_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_STAT_INSPECT_PANEL_H_
