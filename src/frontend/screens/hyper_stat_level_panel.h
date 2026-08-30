/* HyperStatLevelPanel is the confirm dialog for raising one Hyper Stat a
 * level: which stat, the rung it is leaving, and what the rung costs against
 * the points the allocation has left.
 *
 * The panel owns no game state. Reset() seeds it; OnEvent() reports which way
 * the answer went. A player short of the points gets a red count and a greyed
 * [Confirm] rather than a dialog that refuses them after they press it -- the
 * Hyper tab greys the row's [+] for the same reason, so this is the second of
 * two doors on the same check.
 */
#ifndef MS_SRC_FRONTEND_SCREENS_HYPER_STAT_LEVEL_PANEL_H_
#define MS_SRC_FRONTEND_SCREENS_HYPER_STAT_LEVEL_PANEL_H_

#include <string>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/confirm_prompt.h"

namespace ms {

class HyperStatLevelPanel {
 public:
  // Seeds the dialog for taking `stat_name` out of `level` for `cost` points,
  // against an allocation holding `points`.
  void Reset(const std::string& stat_name, int level, int cost, int points);
  ftxui::Element Render() const;
  ConfirmChoice OnEvent(ftxui::Event event);
  // Whether the allocation covers the rung. The controller asks before
  // spending, so the check the greyed button shows is the check enforced.
  bool affordable() const {
    return points_ >= cost_;
  }

 private:
  std::string stat_name_;
  int level_ = 0;
  int cost_ = 0;
  int points_ = 0;
  ConfirmPrompt confirm_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_HYPER_STAT_LEVEL_PANEL_H_
