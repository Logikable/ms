/* The card a cleared boss fight ends on: what was beaten, and what it paid.
 *
 * A view like the fight screen beside it -- it holds nothing and decides
 * nothing. Gold rather than the game's steel blue, the way the level-up card
 * is: a clear is news, and a daily boss is news the player waited a day for.
 */
#ifndef MS_SRC_FRONTEND_SCREENS_BOSS_CLEAR_PANEL_H_
#define MS_SRC_FRONTEND_SCREENS_BOSS_CLEAR_PANEL_H_

#include <string>

#include "ftxui/dom/elements.hpp"
#include "src/combat/boss_run.h"

namespace ms {

// The card for `title` paying `reward`, its [Continue] inside the border so
// the whole of what is read sits in one box. `seconds` is how long the clear
// took, carried by the title row: "Normal Zakum in 2:47". `show_honor` is
// HonorVisible -- the clear pays honor either way.
ftxui::Element BossClearPanel(const std::string& title, double seconds,
                              const BossReward& reward, ftxui::Element prompt,
                              bool show_honor);

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_BOSS_CLEAR_PANEL_H_
