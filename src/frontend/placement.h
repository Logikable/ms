/* Where a screen stands in the terminal.
 *
 * Four shapes cover every screen the game draws, and the point of naming them
 * here is that a screen picks one rather than spelling out its own fillers:
 *
 *  - Centred: one window and nothing behind it, centred on both axes.
 *  - SideBySide: windows shoulder to shoulder, centred together as one block.
 *  - Overlay: a dialog floated over the screen it is about.
 *  - BottomRight: a box tucked into the corner of the screen behind it.
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

// Cards side by side, the row centred as a block and each at its own width.
// An hbox hands every child the full height of the row, so a bordered card is
// drawn the height of the TALLEST and their borders line up. A card that is
// itself a COLUMN of windows keeps its own height, the slack under it blank.
ftxui::Element SideBySide(ftxui::Elements cards);

// `dialog` centred over `screen`, which stays visible around it.
ftxui::Element Overlay(ftxui::Element screen, ftxui::Element dialog);

// `box` in the bottom-right corner of `screen`, which stays visible around
// it. For the notification, which is news rather than something to answer:
// the corner is where it can be read without covering what is being read.
ftxui::Element BottomRight(ftxui::Element screen, ftxui::Element box);

}  // namespace ms

#endif  // MS_SRC_FRONTEND_PLACEMENT_H_
