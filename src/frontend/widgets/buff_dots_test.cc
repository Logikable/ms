#include "src/frontend/widgets/buff_dots.h"

#include <string>
#include <vector>

#include "gtest/gtest.h"

namespace ms {
namespace {

// Each row of dots with the labels drawn over them, as the bar draws them.
std::vector<std::string> Drawn(int width,
                               const std::vector<std::string>& labels,
                               int count) {
  std::vector<std::vector<std::string>> cells = BuffDots(width, labels, count);
  std::vector<std::string> rows;
  for (int r = 0; r < static_cast<int>(labels.size()); ++r) {
    std::vector<std::string> row(width, " ");
    for (int c = 0; c < width; ++c) {
      if (!cells[r][c].empty()) {
        row[c] = cells[r][c];
      }
    }
    int x = (width - static_cast<int>(labels[r].size())) / 2;
    for (char ch : labels[r]) {
      EXPECT_EQ(row[x], " ") << "a dot under the name at column " << x;
      row[x++] = std::string(1, ch);
    }
    std::string joined;
    for (const std::string& cell : row) {
      joined += cell;
    }
    rows.push_back(joined);
  }
  return rows;
}

TEST(BuffDotsTest, GrowInFromBothEdges) {
  EXPECT_EQ(Drawn(21, {"Brandish"}, 0)[0], "      Brandish       ");
  EXPECT_EQ(Drawn(21, {"Brandish"}, 1)[0], " ·    Brandish       ");
  EXPECT_EQ(Drawn(21, {"Brandish"}, 3)[0], " · ·  Brandish     · ");
  EXPECT_EQ(Drawn(21, {"Brandish"}, 4)[0], " · ·  Brandish   · · ");
}

TEST(BuffDotsTest, DoubleUpRatherThanReachTheName) {
  EXPECT_EQ(Drawn(21, {"Brandish"}, 7)[0], " : :  Brandish · · · ");
  EXPECT_EQ(Drawn(21, {"Brandish"}, 12)[0], " ⁞ :  Brandish : : : ");
  EXPECT_EQ(Drawn(21, {"Brandish"}, 15)[0], " ⁞ ⁞  Brandish   ⁝ ⁞ ");
  // Beyond four per glyph the room is full, and no more are shown.
  EXPECT_EQ(Drawn(21, {"Brandish"}, 40)[0], " ⁞ ⁞  Brandish ⁞ ⁞ ⁞ ");
  // A name that leaves no room keeps the whole bar.
  EXPECT_EQ(Drawn(10, {"Brandish"}, 4)[0], " Brandish ");
}

TEST(BuffDotsTest, TakeTheRowTheNameLeavesEmpty) {
  std::vector<std::string> rows = Drawn(14, {"Brandish", ""}, 3);
  EXPECT_EQ(rows[0], "   Brandish   ");
  EXPECT_EQ(rows[1], " · ·        · ");
  // When full, the empty row spills over onto the name's row.
  rows = Drawn(14, {"Brandish", ""}, 8);
  EXPECT_EQ(rows[0], " · Brandish · ");
  EXPECT_EQ(rows[1], " · · ·  · · · ");
}

TEST(BuffDotsTest, WrappedNameKeepsBothRows) {
  std::vector<std::string> rows = Drawn(14, {"Advanced", "Final Attack"}, 2);
  EXPECT_EQ(rows[0], " · Advanced · ");
  EXPECT_EQ(rows[1], " Final Attack ");
}

}  // namespace
}  // namespace ms
