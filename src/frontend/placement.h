/* Where a screen stands in the terminal.
 *
 * Three shapes cover every screen the game draws, and the point of naming them
 * here is that a screen picks one rather than spelling out its own fillers:
 *
 *  - Centred: one window and nothing behind it, centred on both axes.
 *  - CardRow: windows shoulder to shoulder, centred together as one block.
 *  - Overlay: a dialog floated over the screen it is about.
 *
 * Placement is not what keeps a screen readable, though -- a window taller
 * than the terminal loses rows wherever it stands. That rule is the floor
 * below, which every screen fits; see //src/data_test:screen_fit_test.
 */
#ifndef MS_SRC_FRONTEND_PLACEMENT_H_
#define MS_SRC_FRONTEND_PLACEMENT_H_

#include "ftxui/dom/elements.hpp"

namespace ms {

// The smallest terminal the game is laid out for. Every screen fits inside it,
// so nothing a player is reading is ever drawn off the edge.
inline constexpr int kMinTerminalColumns = 120;
inline constexpr int kMinTerminalRows = 30;

// One window, alone on screen.
ftxui::Element Centred(ftxui::Element window);

// Cards side by side, the row centred as a block and each card at its own
// width. Every card is drawn the height of the TALLEST -- an hbox hands each
// child the full height of the row -- so their borders line up and the row
// reads as one thing rather than several.
ftxui::Element CardRow(ftxui::Elements cards);

// `dialog` centred over `screen`, which stays visible around it.
ftxui::Element Overlay(ftxui::Element screen, ftxui::Element dialog);

}  // namespace ms

#endif  // MS_SRC_FRONTEND_PLACEMENT_H_
