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

#include "ftxui/dom/elements.hpp"

namespace ms {

// The card as a bordered window: the level climbed, a rule, then what it paid,
// one line each. Always the same size -- five rows inside the border and a
// floor under its width -- so it is one shape the player learns rather than a
// box that grows and shrinks with the news.
//
// `ap` and `sp` are totals for the whole climb, so a jump of several levels
// reports what it earned rather than what the last one did. A total of zero is
// left off entirely -- a Beginner earns no SP they can reach, and "+0 SP" on a
// card celebrating something would read as a slight.
ftxui::Element LevelUpPopupPanel(int from_level, int to_level, int ap, int sp);

}  // namespace ms

#endif  // MS_SRC_FRONTEND_PANELS_LEVEL_UP_POPUP_PANEL_H_
