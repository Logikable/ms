/* The card shown for a few seconds when a mob finishes the player off.
 *
 * The same panel-in-name-only as the level-up and advancement cards: it holds
 * nothing and decides nothing, and how long it stays up is the caller's
 * business.
 *
 * Red rather than their gold, because it is the one card that is not good
 * news, and drawn at the same size so it lands in the same place with the same
 * weight.
 */
#ifndef MS_SRC_FRONTEND_PANELS_DEATH_POPUP_PANEL_H_
#define MS_SRC_FRONTEND_PANELS_DEATH_POPUP_PANEL_H_

#include "ftxui/dom/elements.hpp"

namespace ms {

// The card as a bordered window: five rows inside the border with the one line
// it has to say held in the middle of them.
ftxui::Element DeathPopupPanel();

}  // namespace ms

#endif  // MS_SRC_FRONTEND_PANELS_DEATH_POPUP_PANEL_H_
