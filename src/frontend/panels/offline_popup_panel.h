/* The card a returning player is met with: how long they were away, and what
 * their character earned while the game was closed.
 *
 * A view like the boss clear card beside it -- it holds nothing and decides
 * nothing. See //src/combat:offline for what fills the report.
 */
#ifndef MS_SRC_FRONTEND_PANELS_OFFLINE_POPUP_PANEL_H_
#define MS_SRC_FRONTEND_PANELS_OFFLINE_POPUP_PANEL_H_

#include <string>

#include "ftxui/dom/elements.hpp"
#include "src/combat/offline.h"

namespace ms {

// The card for `report`, with `prompt` -- the [Continue] that dismisses it --
// inside the border, so the whole of what the player is reading is one box.
// What the absence earned reads first and the loot under a rule of its own:
// the numbers are the same four every time, and a long list of drops should
// not be scanned for them.
//
// `show_honor` is HonorVisible: the farming pays honor either way, but a
// player with nothing to spend it on is not told about a currency yet.
ftxui::Element OfflinePopupPanel(const OfflineReport& report,
                                 ftxui::Element prompt, bool show_honor);

// How long an absence reads as: "3d 4h", "7h 12m", "45m", "38s". The two
// largest units it has, since nobody coming back after a day cares about the
// minutes.
std::string FormatAbsence(double seconds);

}  // namespace ms

#endif  // MS_SRC_FRONTEND_PANELS_OFFLINE_POPUP_PANEL_H_
