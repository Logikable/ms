/* Reading a rendered ftxui::Screen back as plain text, for tests.
 *
 * Screen::ToString KEEPS the colour and dim escapes, so a styled cell puts
 * bytes between two things that look adjacent on screen -- which is what
 * breaks a search for "> Name" the moment the name is dimmed. Everything here
 * reads the pixel grid instead, and an unpainted cell reads as a space.
 */
#ifndef MS_SRC_FRONTEND_WIDGETS_SCREEN_TEXT_H_
#define MS_SRC_FRONTEND_WIDGETS_SCREEN_TEXT_H_

#include <string>
#include <vector>

#include "ftxui/screen/screen.hpp"

namespace ms {

// Columns [from, to) of row `y`. Columns off the screen read as nothing.
std::string ScreenRow(const ftxui::Screen& screen, int y, int from, int to);

// The whole of row `y`.
std::string ScreenRow(const ftxui::Screen& screen, int y);

// Every row, top to bottom.
std::vector<std::string> ScreenRows(const ftxui::Screen& screen);

// The whole screen, a newline between rows.
std::string ScreenText(const ftxui::Screen& screen);

// The index of the first row holding `needle`, or -1 if none does.
int RowIndexOf(const ftxui::Screen& screen, const std::string& needle);

// Where the first `needle` starts, as {x, y}, or {-1, -1} if it is not drawn.
// `x` is the COLUMN, not the byte offset into the row: a box-drawing character
// is three bytes in one cell, so the two part company the moment a border is
// on the row.
struct ScreenPos {
  int x = -1;
  int y = -1;
};
ScreenPos FindOnScreen(const ftxui::Screen& screen, const std::string& needle);

// The foreground colour of the first cell of `needle`. Color::Default when it
// is not on screen, which no expected colour equals.
ftxui::Color ColorOf(const ftxui::Screen& screen, const std::string& needle);

// The whole pixel there, for a caller asking about the dim bit as well as the
// colour. A default-constructed Pixel when `needle` is not on screen.
ftxui::Pixel PixelOf(const ftxui::Screen& screen, const std::string& needle);

}  // namespace ms

#endif  // MS_SRC_FRONTEND_WIDGETS_SCREEN_TEXT_H_
