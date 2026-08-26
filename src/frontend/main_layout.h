#ifndef MS_SRC_FRONTEND_MAIN_LAYOUT_H_
#define MS_SRC_FRONTEND_MAIN_LAYOUT_H_

#include "ftxui/dom/elements.hpp"
#include "src/frontend/panel_widths.h"

namespace ms {

// The width each column of the main view lays out at. `right` is zero for a
// character with no right-column panel unlocked yet.
struct MainWidths {
  int left = kLeftColumnMin;
  int right = 0;
};

// The columns a terminal `terminal_width` wide splits into.
// `has_right_column` is whether the equipped panel or the bag is on screen --
// the corner panel alone does not reserve a column, since it sizes itself.
//
// The left column
// grows to its maximum before the right column gets anything past its own
// minimum, and on a terminal too narrow for both minimums the left column
// keeps its own and the right column takes what is left -- its rightmost
// columns run off the edge, which beats cutting the stats the player is
// spending AP on.
MainWidths ComputeMainWidths(int terminal_width, bool has_right_column);

// Arranges the main view: the character panel over combat on the left, the
// equipped panel over the bag over the corner panel on the right, and the exp
// bar across the foot. Split out of Tui::RenderMain so a test can measure it.
//
// `equipped`, `inventory` and `corner` may each be null, for a character who
// has not unlocked them. The layout closes up around a null rather than
// leaving a gap, and with all three null there is no right column at all.
//
// `corner` is the hotkeys tip early on and the menu panel from level 5 --
// never both. Either way it sizes itself to its own contents and sits against
// the bottom-right corner.
ftxui::Element MainLayout(MainWidths widths, ftxui::Element character,
                          ftxui::Element combat, ftxui::Element equipped,
                          ftxui::Element inventory, ftxui::Element corner,
                          ftxui::Element exp_bar);

}  // namespace ms

#endif  // MS_SRC_FRONTEND_MAIN_LAYOUT_H_
