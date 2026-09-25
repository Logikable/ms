/* The card shown for a few seconds when a mob kills the player.
 *
 * The caller decides how long it stays up; see //src/frontend:celebration,
 * which keeps one card on screen at a time.
 *
 * It is red instead of gold because it is the only card that isn't good news,
 * and it is the same size so it appears in the same place with the same weight.
 */
#ifndef MS_SRC_FRONTEND_CARDS_DEATH_CARD_H_
#define MS_SRC_FRONTEND_CARDS_DEATH_CARD_H_

#include "ftxui/dom/elements.hpp"

namespace ms {

// The card as a bordered window: five rows inside the border, with its one line
// in the middle.
ftxui::Element DeathCard();

}  // namespace ms

#endif  // MS_SRC_FRONTEND_CARDS_DEATH_CARD_H_
