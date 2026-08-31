#include "src/frontend/widgets/screen_text.h"

#include <algorithm>
#include <string>
#include <vector>

#include "ftxui/screen/screen.hpp"

namespace ms {
namespace {

const std::string& Space() {
  static const std::string* space = new std::string(" ");
  return *space;
}

}  // namespace

std::string ScreenRow(const ftxui::Screen& screen, int y, int from, int to) {
  std::string row;
  for (int x = std::max(0, from); x < std::min(to, screen.dimx()); ++x) {
    const std::string& cell = screen.PixelAt(x, y).character;
    row += cell.empty() ? " " : cell;
  }
  return row;
}

std::string ScreenRow(const ftxui::Screen& screen, int y) {
  return ScreenRow(screen, y, 0, screen.dimx());
}

std::vector<std::string> ScreenRows(const ftxui::Screen& screen) {
  std::vector<std::string> rows;
  for (int y = 0; y < screen.dimy(); ++y) {
    rows.push_back(ScreenRow(screen, y));
  }
  return rows;
}

std::string ScreenText(const ftxui::Screen& screen) {
  std::string out;
  for (int y = 0; y < screen.dimy(); ++y) {
    out += ScreenRow(screen, y);
    out += '\n';
  }
  return out;
}

int RowIndexOf(const ftxui::Screen& screen, const std::string& needle) {
  return FindOnScreen(screen, needle).y;
}

ScreenPos FindOnScreen(const ftxui::Screen& screen, const std::string& needle) {
  for (int y = 0; y < screen.dimy(); ++y) {
    // The column each byte of the row starts at. A box-drawing character is
    // three bytes in one cell, so an offset into the row is not a column --
    // which is what a caller measuring where a box was drawn is asking for.
    std::string row;
    std::vector<int> column_at;
    for (int x = 0; x < screen.dimx(); ++x) {
      const std::string& cell = screen.PixelAt(x, y).character;
      const std::string& text = cell.empty() ? Space() : cell;
      row += text;
      column_at.insert(column_at.end(), text.size(), x);
    }
    size_t at = row.find(needle);
    if (at != std::string::npos) {
      return {column_at[at], y};
    }
  }
  return {};
}

ftxui::Color ColorOf(const ftxui::Screen& screen, const std::string& needle) {
  ScreenPos at = FindOnScreen(screen, needle);
  return at.y < 0 ? ftxui::Color::Default
                  : screen.PixelAt(at.x, at.y).foreground_color;
}

ftxui::Pixel PixelOf(const ftxui::Screen& screen, const std::string& needle) {
  ScreenPos at = FindOnScreen(screen, needle);
  return at.y < 0 ? ftxui::Pixel() : screen.PixelAt(at.x, at.y);
}

}  // namespace ms
