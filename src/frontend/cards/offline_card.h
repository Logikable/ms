/* The card a returning player sees: how long they were away, and what their
 * character earned while the game was closed.
 *
 * See //src/combat:offline for how the report is filled.
 */
#ifndef MS_SRC_FRONTEND_CARDS_OFFLINE_CARD_H_
#define MS_SRC_FRONTEND_CARDS_OFFLINE_CARD_H_

#include <string>

#include "ftxui/dom/elements.hpp"
#include "src/combat/offline.h"

namespace ms {

// The card for `report`, with its [Continue] inside the border so everything to
// read is in one box. What the absence earned comes first, and the loot below
// its own divider: the numbers are the same four every time. `show_honor` is
// HonorVisible; farming pays honor either way.
ftxui::Element OfflineCard(const OfflineReport& report, ftxui::Element prompt,
                           bool show_honor);

// How an absence is displayed: "3d 4h", "7h 12m", "45m", "38s". Only the two
// largest units, since nobody back after a day cares about the minutes.
std::string FormatAbsence(double seconds);

}  // namespace ms

#endif  // MS_SRC_FRONTEND_CARDS_OFFLINE_CARD_H_
