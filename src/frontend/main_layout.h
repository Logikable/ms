#ifndef MS_SRC_FRONTEND_MAIN_LAYOUT_H_
#define MS_SRC_FRONTEND_MAIN_LAYOUT_H_

#include "ftxui/dom/elements.hpp"

namespace ms {

// Arranges the main view: the character panel over combat on the left, the
// equipped panel over the bag over the corner panel on the right, and the exp
// bar across the foot. Split out of Tui::RenderMain so a test can measure it.
//
// `equipped`, `inventory` and `corner` may each be null, for a character who
// has not unlocked them. The layout closes up around a null rather than
// leaving a gap, and with all three null there is no right column at all.
//
// `corner` is the hotkeys tip early on and the menu panel from level 5 --
// never both, and `corner_fills` says which: the tip sizes itself to its own
// longest line, and the menu spans the column.
ftxui::Element MainLayout(ftxui::Element character, ftxui::Element combat,
                          ftxui::Element equipped, ftxui::Element inventory,
                          ftxui::Element corner, bool corner_fills,
                          ftxui::Element exp_bar);

}  // namespace ms

#endif  // MS_SRC_FRONTEND_MAIN_LAYOUT_H_
