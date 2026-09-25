#include "src/frontend/widgets/text_columns.h"

#include <gtest/gtest.h>

#include <string>

namespace ms {
namespace {

// An accented letter is two bytes and one column, a CJK character three bytes
// and two columns, and an emoji four bytes and two columns.
constexpr char kAccented[] = "Émeraude";
constexpr char kWide[] = "青龍偃";
constexpr char kEmoji[] = "\U0001f4dc";

TEST(TextColumnsTest, CountsColumnsNotBytes) {
  EXPECT_EQ(TextColumns(""), 0);
  EXPECT_EQ(TextColumns("Iron Sword"), 10);
  EXPECT_EQ(TextColumns(kAccented), 8);
  EXPECT_EQ(TextColumns(kWide), 6);
  EXPECT_EQ(TextColumns(kEmoji), 2);
}

TEST(TextColumnsTest, AWindowIsAlwaysTheColumnsAskedFor) {
  EXPECT_EQ(ColumnWindow("Iron", 0, 6), "Iron  ");
  EXPECT_EQ(ColumnWindow("Iron Sword", 0, 4), "Iron");
  EXPECT_EQ(ColumnWindow("Iron Sword", 5, 5), "Sword");
  // Past the end of the text is spaces, not a short string.
  EXPECT_EQ(ColumnWindow("Iron", 8, 3), "   ");
  EXPECT_EQ(ColumnWindow("Iron", 0, 0), "");
  EXPECT_EQ(ColumnWindow("Iron", 0, -2), "");
}

TEST(TextColumnsTest, AMultibyteCharacterIsNotCutInHalf) {
  EXPECT_EQ(ColumnWindow(kAccented, 0, 4), "Émer");
  EXPECT_EQ(ColumnWindow(kAccented, 1, 3), "mer");
  // When an edge falls inside a fullwidth character, its column is left blank
  // instead of drawing half the character.
  EXPECT_EQ(ColumnWindow(kWide, 0, 3), "青 ");
  EXPECT_EQ(ColumnWindow(kWide, 0, 4), "青龍");
  EXPECT_EQ(ColumnWindow(kWide, 1, 4), " 龍 ");
  EXPECT_EQ(ColumnWindow(kWide, 1, 1), " ");
}

// Every window at every offset has the right width, which callers rely on.
TEST(TextColumnsTest, EveryWindowMeasuresItsWidth) {
  const std::string kMixed = std::string(kAccented) + " " + kWide + kEmoji;
  for (int width = 1; width <= 20; ++width) {
    for (int from = 0; from <= 20; ++from) {
      EXPECT_EQ(TextColumns(ColumnWindow(kMixed, from, width)), width)
          << from << " " << width;
    }
  }
}

}  // namespace
}  // namespace ms
