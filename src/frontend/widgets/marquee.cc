#include "src/frontend/widgets/marquee.h"

#include <algorithm>
#include <chrono>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/screen/string.hpp"

namespace ms {

namespace {

// One character of a name and the columns it takes. A name is walked a glyph
// at a time rather than a byte at a time: a byte is not a column, and half a
// multibyte character is not a character.
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

int Columns(const std::vector<Glyph>& glyphs) {
  int columns = 0;
  for (const Glyph& glyph : glyphs) {
    columns += glyph.columns;
  }
  return columns;
}

// `count` columns of `glyphs`, starting at column `from`. A fullwidth glyph
// the edge of the window falls inside is drawn as a space for the column that
// fits, so the window is always exactly as wide as it was asked to be.
std::string Window(const std::vector<Glyph>& glyphs, int from, int count) {
  std::string out;
  int at = 0;
  for (const Glyph& glyph : glyphs) {
    int start = at;
    at += glyph.columns;
    if (at <= from || start >= from + count) {
      continue;
    }
    if (start >= from && at <= from + count) {
      out += glyph.text;
      continue;
    }
    out.append(std::min(at, from + count) - std::max(start, from), ' ');
  }
  return out;
}

// How far into the name the window has slid, given how long the row has been
// selected.
//
// Every offset is held for one step except the two ends, which are held for
// the pause instead: the head so it can be read before it goes, the tail so
// the answer the player was waiting for does not flick past. So the slide
// itself is the offsets between them -- one fewer than the number of steps.
int OffsetAt(int steps, std::chrono::steady_clock::duration elapsed) {
  std::chrono::milliseconds ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(elapsed);
  if (ms < std::chrono::milliseconds::zero()) {
    return 0;  // a clock that ran backwards shows the head, not a crash
  }
  std::chrono::milliseconds slide = kMarqueeStep * (steps - 1);
  std::chrono::milliseconds cycle = kMarqueePause * 2 + slide;
  std::chrono::milliseconds into(ms.count() % cycle.count());
  if (into < kMarqueePause) {
    return 0;
  }
  into -= kMarqueePause;
  if (into >= slide) {
    return steps;  // the tail pause, spent at the end
  }
  return 1 + static_cast<int>(into / kMarqueeStep);
}

}  // namespace

void SelectionClock::Follow(int key) {
  if (key == key_) {
    return;
  }
  key_ = key;
  since_ = std::chrono::steady_clock::now();
}

std::chrono::steady_clock::duration SelectionClock::Elapsed() const {
  return std::chrono::steady_clock::now() - since_;
}

std::string ScrollingWindow(const std::string& text, int width,
                            std::chrono::steady_clock::duration elapsed) {
  if (width <= 0) {
    return "";
  }
  std::vector<Glyph> glyphs = Glyphs(text);
  int columns = Columns(glyphs);
  if (columns <= width) {
    return text + std::string(width - columns, ' ');
  }
  return Window(glyphs, OffsetAt(columns - width, elapsed), width);
}

}  // namespace ms
