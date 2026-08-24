#include "src/frontend/widgets/text_columns.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/screen/string.hpp"

namespace ms {
namespace {

// One character and the columns it takes.
struct Glyph {
  std::string text;
  int columns;
};

std::vector<Glyph> Glyphs(const std::string& text) {
  std::vector<Glyph> glyphs;
  // ftxui follows a fullwidth glyph with an empty one, which is its second
  // column rather than another character.
  for (std::string& piece : ftxui::Utf8ToGlyphs(text)) {
    if (piece.empty()) {
      if (!glyphs.empty()) {
        ++glyphs.back().columns;
      }
      continue;
    }
    glyphs.push_back({std::move(piece), 1});
  }
  return glyphs;
}

}  // namespace

int TextColumns(const std::string& text) {
  int columns = 0;
  for (const Glyph& glyph : Glyphs(text)) {
    columns += glyph.columns;
  }
  return columns;
}

std::string ColumnWindow(const std::string& text, int from, int count) {
  if (count <= 0) {
    return "";
  }
  from = std::max(0, from);
  std::string window;
  int filled = 0;
  int at = 0;
  for (const Glyph& glyph : Glyphs(text)) {
    int start = at;
    at += glyph.columns;
    if (at <= from || start >= from + count) {
      continue;
    }
    if (start >= from && at <= from + count) {
      window += glyph.text;
      filled += glyph.columns;
      continue;
    }
    // Half a fullwidth character draws as nothing, so the column it cannot
    // fill is given up instead.
    int columns = std::min(at, from + count) - std::max(start, from);
    window.append(columns, ' ');
    filled += columns;
  }
  window.append(count - filled, ' ');
  return window;
}

}  // namespace ms
