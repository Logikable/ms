/* Where a screen sits in the terminal.
 *
 * Four layouts cover every screen in the game, and a screen picks one instead
 * of writing its own fillers:
 *
 *  - Centred: one window with nothing behind it, centred both ways.
 *  - SideBySide: windows next to each other, centred together as one block.
 *  - Overlay: a dialog over the screen it relates to.
 *  - BottomRight: a box in the corner of the screen behind it.
 *
 * Placement doesn't keep a screen readable, though: a window taller than the
 * terminal loses rows wherever it is placed. The minimum size below handles
 * that, and every screen fits it; see //src/data_test:screen_fit_test.
 */
#ifndef MS_SRC_FRONTEND_PLACEMENT_H_
#define MS_SRC_FRONTEND_PLACEMENT_H_

#include "ftxui/dom/elements.hpp"

namespace ms {

// The smallest terminal the game is laid out for. Every screen fits in it, so
// nothing the player reads is drawn off the edge.
inline constexpr int kMinTerminalColumns = 120;
inline constexpr int kMinTerminalRows = 30;

// One window, alone on screen.
ftxui::Element Centred(ftxui::Element window);

// Cards side by side, centred as a block, each at its own width. An hbox gives
// every child the full row height, so bordered cards are all drawn as tall as
// the tallest and their borders line up. A card that is itself a column of
// windows keeps its own height, with blank space under it.
ftxui::Element SideBySide(ftxui::Elements cards);

// `dialog` centred over `screen`, which stays visible around it.
ftxui::Element Overlay(ftxui::Element screen, ftxui::Element dialog);

// `box` in the bottom-right corner of `screen`, which stays visible around it.
// Used for notifications, which are news rather than something to answer: the
// corner doesn't cover what the player is reading.
ftxui::Element BottomRight(ftxui::Element screen, ftxui::Element box);

}  // namespace ms

#endif  // MS_SRC_FRONTEND_PLACEMENT_H_
