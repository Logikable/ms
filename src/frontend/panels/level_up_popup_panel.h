/* The card shown for a few seconds when the player gains a level.
 *
 * A panel in name only, like the hotkeys tip: it holds nothing and decides
 * nothing. How long it stays up, and whether it is drawn at all, is the
 * caller's business.
 *
 * Gold rather than the game's steel blue, and drawn over the middle of
 * whatever screen the player is on. The point of it is to be seen by someone
 * who is not looking directly at the game, so it does not wait its turn in the
 * layout, and it is given more room than its content needs.
 */
#ifndef MS_SRC_FRONTEND_PANELS_LEVEL_UP_POPUP_PANEL_H_
#define MS_SRC_FRONTEND_PANELS_LEVEL_UP_POPUP_PANEL_H_

#include <cstdint>
#include <string>
#include <vector>

#include "ftxui/dom/elements.hpp"

namespace ms {

// The card as a bordered window: the level climbed, a rule, then what it paid.
// Always five rows inside the border, so it is one shape the player learns
// rather than a box that grows with the news.
//
// `ap`, `sp`, `hyper_sp` and `honor` are totals for the whole climb. A total
// of zero is left off -- "+0 SP" on a card celebrating something reads as a
// slight. The honor is left off as well until Inner Ability has been opened,
// on this character or another one: it is paid from level 2, and a currency
// with nothing to spend it on is not news.
//
// `unlocks` names anything the climb opened, in gold, sharing the body with the
// gains rather than added below them.
ftxui::Element LevelUpPopupPanel(int from_level, int to_level, int ap, int sp,
                                 int hyper_sp = 0, int64_t honor = 0,
                                 const std::vector<std::string>& unlocks = {});

}  // namespace ms

#endif  // MS_SRC_FRONTEND_PANELS_LEVEL_UP_POPUP_PANEL_H_
