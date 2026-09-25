#include "src/frontend/widgets/buff_dots.h"

#include <string>
#include <utility>
#include <vector>

namespace ms {
namespace {

// The dot glyphs, indexed by how many dots each holds.
const char* const kGlyphs[] = {"", "·", ":", "⁝", "⁞"};

struct Cell {
  int row = 0;
  int col = 0;
};

// Where one side's glyphs can go, starting at the edge: one column in from the
// border, one column apart, first on rows the name leaves empty and then on the
// name's rows. `limit(row)` is how far in from the edge that row allows a
// glyph.
template <typename Limit>
std::vector<Cell> Slots(int rows, Limit limit) {
  std::vector<Cell> slots;
  for (int pass = 0; pass < 2; ++pass) {
    for (int row = rows - 1; row >= 0; --row) {
      auto [empty, reach] = limit(row);
      if (empty != (pass == 0)) {
        continue;
      }
      for (int in = 1; in <= reach; in += 2) {
        slots.push_back({row, in});
      }
    }
  }
  return slots;
}

// Places `count` dots over `slots` using the lightest glyph that fits them all,
// with the partial glyph innermost. `mirror` converts a column counted from the
// left edge into one counted from the right.
void Lay(const std::vector<Cell>& slots, int count, int width, bool mirror,
         std::vector<std::vector<std::string>>& cells) {
  int room = static_cast<int>(slots.size());
  int per = 1;
  while (per < 4 && (count + per - 1) / per > room) {
    per *= 2;
  }
  for (int i = 0; i < room && count > 0; ++i) {
    int here = count < per ? count : per;
    count -= here;
    const Cell& slot = slots[i];
    cells[slot.row][mirror ? width - 1 - slot.col : slot.col] = kGlyphs[here];
  }
}

}  // namespace

std::vector<std::vector<std::string>> BuffDots(
    int width, const std::vector<std::string>& labels, int count) {
  int rows = static_cast<int>(labels.size());
  std::vector<std::vector<std::string>> cells(
      rows, std::vector<std::string>(width > 0 ? width : 0));
  if (count <= 0 || width <= 0) {
    return cells;
  }
  // A glyph one column clear of the name, or of the other side's glyph across
  // the middle of an empty row.
  auto left = [&](int row) {
    int len = static_cast<int>(labels[row].size());
    if (len == 0) {
      return std::pair<bool, int>{true, (width - 3) / 2};
    }
    return std::pair<bool, int>{false, (width - len) / 2 - 2};
  };
  auto right = [&](int row) {
    int len = static_cast<int>(labels[row].size());
    if (len == 0) {
      return std::pair<bool, int>{true, (width - 3) / 2};
    }
    return std::pair<bool, int>{false, width - (width - len) / 2 - len - 2};
  };
  Lay(Slots(rows, left), (count + 1) / 2, width, false, cells);
  Lay(Slots(rows, right), count / 2, width, true, cells);
  return cells;
}

}  // namespace ms
