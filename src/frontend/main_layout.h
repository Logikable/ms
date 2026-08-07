#ifndef MS_SRC_FRONTEND_MAIN_LAYOUT_H_
#define MS_SRC_FRONTEND_MAIN_LAYOUT_H_

#include "ftxui/dom/elements.hpp"

namespace ms {

// Arranges the main view: the character panel over combat on the left, the
// equipped panel over the bag over the hotkeys tip on the right, and the exp
// bar across the foot. Split out of Tui::RenderMain so a test can measure it.
//
// `equipped`, `inventory` and `hotkeys` may each be null, for a character who
// has not unlocked them -- or, for the tip, has outgrown it. The layout closes
// up around a null rather than leaving a gap, and with all three null there is
// no right column at all.
ftxui::Element MainLayout(ftxui::Element character, ftxui::Element combat,
                          ftxui::Element equipped, ftxui::Element inventory,
                          ftxui::Element hotkeys, ftxui::Element exp_bar);

}  // namespace ms

#endif  // MS_SRC_FRONTEND_MAIN_LAYOUT_H_
