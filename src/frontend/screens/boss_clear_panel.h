/* The card shown when a boss fight is cleared: what was beaten, and what it
 * paid.
 *
 * It only displays, like the fight screen, and holds no state. It is gold
 * instead of the game's steel blue, like the level-up card, because a clear is
 * news, and a daily boss is news the player waited a day for.
 */
#ifndef MS_SRC_FRONTEND_SCREENS_BOSS_CLEAR_PANEL_H_
#define MS_SRC_FRONTEND_SCREENS_BOSS_CLEAR_PANEL_H_

#include <string>

#include "ftxui/dom/elements.hpp"
#include "src/combat/boss_run.h"

namespace ms {

// The card for `title` paying `reward`, with its buttons (`prompt`) inside the
// border so everything to read is in one box. `seconds` is how long the clear
// took, shown on the title row: "Normal Zakum in 2:47". `show_honor` is
// HonorVisible; the clear pays honor either way.
ftxui::Element BossClearPanel(const std::string& title, double seconds,
                              const BossReward& reward, ftxui::Element prompt,
                              bool show_honor);

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_BOSS_CLEAR_PANEL_H_
