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
// `has_right_column` is whether the equipped panel or the bag is on screen;
// the corner panel sizes itself and reserves nothing.
//
// The LEFT column grows to its maximum first, and on a terminal too narrow for
// both minimums it keeps its own while the right column runs off the edge --
// which beats cutting the stats the player is spending AP on. Its width is the
// same either way, the right column's room being held before it is
// unlocked.
MainWidths ComputeMainWidths(int terminal_width, bool has_right_column);

// The main view: the character panel over combat on the left, the equipped
// panel over the bag over the corner panel on the right, the exp bar across
// the foot. Split out of Tui::RenderMain so a test can measure it.
//
// The right column's three may each be null, for a character who has not
// unlocked them; the layout CLOSES UP around a null, and with all three there
// is no right column. `corner` is the hotkeys tip early and the menu panel
// from level 5, never both.
ftxui::Element MainLayout(MainWidths widths, ftxui::Element character,
                          ftxui::Element combat, ftxui::Element equipped,
                          ftxui::Element inventory, ftxui::Element corner,
                          ftxui::Element exp_bar);

}  // namespace ms

#endif  // MS_SRC_FRONTEND_MAIN_LAYOUT_H_
