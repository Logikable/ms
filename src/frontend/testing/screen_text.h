/* Reads a rendered ftxui::Screen back as plain text, for tests.
 *
 * Screen::ToString keeps the colour and dim escapes, so a styled cell puts
 * bytes between two things that look adjacent on screen. That breaks a search
 * for "> Name" as soon as the name is dimmed. Everything here reads the pixel
 * grid instead, and an unpainted cell reads as a space.
 */
#ifndef MS_SRC_FRONTEND_TESTING_SCREEN_TEXT_H_
#define MS_SRC_FRONTEND_TESTING_SCREEN_TEXT_H_

#include <string>
#include <vector>

#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"

namespace ms {

// Columns [from, to) of row `y`. Columns off the screen read as nothing.
std::string ScreenRow(const ftxui::Screen& screen, int y, int from, int to);

// The whole of row `y`.
std::string ScreenRow(const ftxui::Screen& screen, int y);

// Every row, top to bottom.
std::vector<std::string> ScreenRows(const ftxui::Screen& screen);

// The whole screen, with a newline between rows.
std::string ScreenText(const ftxui::Screen& screen);

// The index of the first row containing `needle`, or -1 if none does.
int RowIndexOf(const ftxui::Screen& screen, const std::string& needle);

// Where the first `needle` starts, as {x, y}, or {-1, -1} if it isn't drawn.
// `x` is the column, not the byte offset in the row: a box-drawing character is
// three bytes in one cell, so the two differ once a border is on the row.
struct ScreenPos {
  int x = -1;
  int y = -1;
};
ScreenPos FindOnScreen(const ftxui::Screen& screen, const std::string& needle);

// The foreground colour of the first cell of `needle`. Color::Default if it
// isn't on screen, which no expected colour equals.
ftxui::Color ColorOf(const ftxui::Screen& screen, const std::string& needle);

// The whole pixel there, for checking the dim bit as well as the colour. A
// default-constructed Pixel if `needle` isn't on screen.
ftxui::Pixel PixelOf(const ftxui::Screen& screen, const std::string& needle);

// The rows of `element` whose text touches its right border with no gap, drawn
// at the element's requested width. An empty result passes.
//
// This catches a card that sizes itself to its widest row and forgets the
// margin. The left side isn't checked, since that column belongs to the cursor.
// Divider lines are skipped, as is a card whose right column is a scroll bar.
std::vector<std::string> RowsTouchingTheRightBorder(ftxui::Element element);

}  // namespace ms

#endif  // MS_SRC_FRONTEND_TESTING_SCREEN_TEXT_H_
