#include "src/frontend/widgets/text_columns.h"

#include <gtest/gtest.h>

#include <string>

namespace ms {
namespace {

// An accented letter is two bytes of one column; a CJK character is three
// bytes of two; an emoji is four bytes of two.
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
  // Past the end of the text is space, not a short string.
  EXPECT_EQ(ColumnWindow("Iron", 8, 3), "   ");
  EXPECT_EQ(ColumnWindow("Iron", 0, 0), "");
  EXPECT_EQ(ColumnWindow("Iron", 0, -2), "");
}

TEST(TextColumnsTest, AMultibyteCharacterIsNotCutInHalf) {
  EXPECT_EQ(ColumnWindow(kAccented, 0, 4), "Émer");
  EXPECT_EQ(ColumnWindow(kAccented, 1, 3), "mer");
  // A fullwidth character an edge falls inside gives up its column rather
  // than being drawn as half of itself.
  EXPECT_EQ(ColumnWindow(kWide, 0, 3), "青 ");
  EXPECT_EQ(ColumnWindow(kWide, 0, 4), "青龍");
  EXPECT_EQ(ColumnWindow(kWide, 1, 4), " 龍 ");
  EXPECT_EQ(ColumnWindow(kWide, 1, 1), " ");
}

// Every window of every offset, measured: the promise the callers lean on.
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
