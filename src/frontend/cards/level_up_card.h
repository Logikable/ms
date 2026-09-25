/* The card shown for a few seconds when the player gains a level.
 *
 * The caller decides how long it stays up and whether it is drawn at all; see
 * //src/frontend:celebration.
 *
 * It is gold rather than the game's steel blue, and drawn over the middle of
 * whatever screen the player is on. It is meant to be noticed by someone not
 * looking directly at the game, so it isn't part of the layout and gets more
 * room than its content needs.
 */
#ifndef MS_SRC_FRONTEND_CARDS_LEVEL_UP_CARD_H_
#define MS_SRC_FRONTEND_CARDS_LEVEL_UP_CARD_H_

#include <cstdint>
#include <string>
#include <vector>

#include "ftxui/dom/elements.hpp"

namespace ms {

// The card as a bordered window: the levels gained, a divider, then what they
// paid. Always five rows inside the border, so it has one shape the player
// learns.
//
// The gains are totals for the whole climb, and a total of zero is left out;
// "+0 SP" on a celebration looks like a slight. Honor is left out until Inner
// Ability unlocks, since a currency with nothing to spend it on isn't news.
// `unlocks` names what the climb unlocked, in gold, sharing the body.
ftxui::Element LevelUpCard(int from_level, int to_level, int ap, int sp,
                           int hyper_sp = 0, int64_t honor = 0,
                           const std::vector<std::string>& unlocks = {});

}  // namespace ms

#endif  // MS_SRC_FRONTEND_CARDS_LEVEL_UP_CARD_H_
