#ifndef MS_SRC_FRONTEND_MAIN_LAYOUT_H_
#define MS_SRC_FRONTEND_MAIN_LAYOUT_H_

#include "ftxui/dom/elements.hpp"
#include "src/frontend/panel_widths.h"

namespace ms {

// The width of each column in the main view. `right` is zero for a character
// with no right-column panel unlocked yet.
struct MainWidths {
  int left = kLeftColumnMin;
  int right = 0;
};

// How a terminal `terminal_width` wide splits into columns. `has_right_column`
// is whether the equipped panel or the bag is shown; the corner panel sizes
// itself and reserves nothing.
//
// The left column grows to its maximum first. On a terminal too narrow for both
// minimums, it keeps its width and the right column runs off the edge, which is
// better than cutting off the stats the player spends AP on. Its width is the
// same either way, since the right column's space is reserved before it
// unlocks.
MainWidths ComputeMainWidths(int terminal_width, bool has_right_column);

// The main view: the character panel over combat on the left, the equipped
// panel over the bag over the corner panel on the right, and the EXP bar across
// the bottom. Split out of Tui::RenderMain so a test can measure it.
//
// Each of the right column's three may be null for a character who hasn't
// unlocked it. The layout closes up around a null, and if all three are null
// there is no right column. `corner` is the hotkeys tip early on and the menu
// panel from level 5, never both.
ftxui::Element MainLayout(MainWidths widths, ftxui::Element character,
                          ftxui::Element combat, ftxui::Element equipped,
                          ftxui::Element inventory, ftxui::Element corner,
                          ftxui::Element exp_bar);

}  // namespace ms

#endif  // MS_SRC_FRONTEND_MAIN_LAYOUT_H_
