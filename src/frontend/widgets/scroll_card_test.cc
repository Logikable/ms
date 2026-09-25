#include "src/frontend/widgets/scroll_card.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"

namespace ms {
namespace {

// `count` numbered rows, so a rendered card shows which slice it drew.
std::vector<CardRow> NumberedRows(int count) {
  std::vector<CardRow> rows;
  for (int i = 0; i < count; ++i) {
    rows.push_back(TextRow(ftxui::text("row" + std::to_string(i))));
  }
  return rows;
}

// The width the card asks for, borders and bar included.
int CardWidth(const ScrollCard& card, std::vector<CardRow> rows,
              int content_width, int view_width = 0) {
  ftxui::Element element =
      card.Render(" T ", std::move(rows), content_width, false, view_width);
  element->ComputeRequirement();
  return element->requirement().min_x;
}

// A card with one section, as in most of these tests.
CardRows Body(std::vector<CardRow> rows) {
  CardRows card;
  card.body = std::move(rows);
  return card;
}

// The card drawn onto its own screen, one string per line.
std::vector<std::string> Draw(const ScrollCard& card, CardRows rows,
                              int content_width, int view_width = 0) {
  ftxui::Element element =
      card.Render(" T ", std::move(rows), content_width, false, view_width);
  element->ComputeRequirement();
  ftxui::Screen screen = ftxui::Screen::Create(
      ftxui::Dimension::Fixed(element->requirement().min_x),
      ftxui::Dimension::Fixed(element->requirement().min_y));
  ftxui::Render(screen, element);
  std::vector<std::string> lines;
  for (int y = 0; y < screen.dimy(); ++y) {
    std::string line;
    for (int x = 0; x < screen.dimx(); ++x) {
      line += screen.PixelAt(x, y).character;
    }
    lines.push_back(line);
  }
  return lines;
}

// Whether the card's right-hand column has any bar glyph.
bool HasBar(const std::vector<std::string>& lines) {
  for (const std::string& line : lines) {
    if (line.find("┃") != std::string::npos ||
        line.find("╹") != std::string::npos ||
        line.find("╻") != std::string::npos) {
      return true;
    }
  }
  return false;
}

// Rows of `width` letters, so a squeezed card shows which columns it drew.
std::vector<CardRow> LetterRows(int count, int width) {
  std::vector<CardRow> rows;
  for (int i = 0; i < count; ++i) {
    std::string text;
    for (int c = 0; c < width; ++c) {
      text += static_cast<char>('a' + (c % 26));
    }
    rows.push_back(TextRow(ftxui::text(text)));
  }
  return rows;
}

// Whether the card's last line before the border has a horizontal bar.
bool HasXBar(const std::vector<std::string>& lines) {
  if (lines.size() < 3) {
    return false;
  }
  const std::string& foot = lines[lines.size() - 2];
  return foot.find("─") != std::string::npos ||
         foot.find("╴") != std::string::npos ||
         foot.find("╶") != std::string::npos;
}

TEST(ScrollCardTest, DrawsEveryRowWithNoBudget) {
  ScrollCard card;
  std::vector<std::string> lines = Draw(card, Body(NumberedRows(4)), 8);
  ASSERT_EQ(lines.size(), 6u) << "four rows and two borders";
  EXPECT_NE(lines[1].find("row0"), std::string::npos);
  EXPECT_NE(lines[4].find("row3"), std::string::npos);
  EXPECT_FALSE(card.Overflows());
  EXPECT_FALSE(HasBar(lines));
}

// The budget includes the borders, so a card given six rows draws four.
TEST(ScrollCardTest, CutsToTheBudgetAndDrawsABar) {
  ScrollCard card;
  card.SetMaxRows(6);
  std::vector<std::string> lines = Draw(card, Body(NumberedRows(10)), 8);
  ASSERT_EQ(lines.size(), 6u);
  EXPECT_NE(lines[1].find("row0"), std::string::npos);
  EXPECT_NE(lines[4].find("row3"), std::string::npos);
  EXPECT_TRUE(card.Overflows());
  EXPECT_TRUE(HasBar(lines));
}

// The bar's column is reserved as soon as there is a budget, so the card is the
// same width whether or not it has anything to scroll.
TEST(ScrollCardTest, ReservesTheBarColumnWhateverFits) {
  ScrollCard fits;
  fits.SetMaxRows(20);
  ScrollCard scrolls;
  scrolls.SetMaxRows(6);
  EXPECT_EQ(CardWidth(fits, NumberedRows(4), 8),
            CardWidth(scrolls, NumberedRows(10), 8));
  EXPECT_FALSE(HasBar(Draw(fits, Body(NumberedRows(4)), 8)))
      << "nothing off screen, so no bar drawn";
}

TEST(ScrollCardTest, ScrollsAndHoldsToBothEnds) {
  ScrollCard card;
  card.SetMaxRows(6);
  Draw(card, Body(NumberedRows(10)), 8);  // renders once so it knows its rows

  card.ScrollBy(2);
  std::vector<std::string> lines = Draw(card, Body(NumberedRows(10)), 8);
  EXPECT_NE(lines[1].find("row2"), std::string::npos);

  card.ScrollBy(-9);
  lines = Draw(card, Body(NumberedRows(10)), 8);
  EXPECT_NE(lines[1].find("row0"), std::string::npos) << "held at the head";

  card.ScrollBy(99);
  lines = Draw(card, Body(NumberedRows(10)), 8);
  EXPECT_NE(lines[1].find("row6"), std::string::npos) << "held at the foot";
  EXPECT_NE(lines[4].find("row9"), std::string::npos);

  card.Reset();
  lines = Draw(card, Body(NumberedRows(10)), 8);
  EXPECT_NE(lines[1].find("row0"), std::string::npos);
}

// A card scrolled to the bottom and then given more room scrolls back up
// instead of drawing past the end of its rows.
TEST(ScrollCardTest, ReclampsWhenTheBudgetGrows) {
  ScrollCard card;
  card.SetMaxRows(6);
  Draw(card, Body(NumberedRows(10)), 8);
  card.ScrollBy(99);
  card.SetMaxRows(12);
  std::vector<std::string> lines = Draw(card, Body(NumberedRows(10)), 8);
  ASSERT_EQ(lines.size(), 12u);
  EXPECT_NE(lines[1].find("row0"), std::string::npos);
}

// A rule reaches both borders, including the bar's column, and still does when
// something stretches the card wider.
TEST(ScrollCardTest, DrawsSeparatorsTheWholeWidth) {
  ScrollCard card;
  card.SetMaxRows(20);
  std::vector<CardRow> rows = NumberedRows(1);
  rows.push_back(RuleRow(ftxui::separator()));
  rows.push_back(TextRow(ftxui::text("tail")));
  std::vector<std::string> lines = Draw(card, Body(std::move(rows)), 8);
  ASSERT_EQ(lines.size(), 5u);
  // Border, eight columns of rule, the bar's column, border.
  EXPECT_EQ(lines[2], "├─────────┤");
}

// The scroll screen gives the card a flex box wider than it asked for. The rule
// has to reach the border, and the bar has to stay against the border.
TEST(ScrollCardTest, StretchesTheRuleAndTheBarToAWiderBox) {
  ScrollCard card;
  card.SetMaxRows(6);
  std::vector<CardRow> rows = NumberedRows(9);
  rows[1] = RuleRow(ftxui::separator());
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(20),
                                               ftxui::Dimension::Fixed(6));
  ftxui::Render(screen, card.Render(" T ", std::move(rows), 8) | ftxui::flex);
  // The rule runs out to the bar, which crosses it at the border.
  EXPECT_EQ(screen.PixelAt(0, 2).character, "├");
  EXPECT_EQ(screen.PixelAt(17, 2).character, "─") << "the rule reaches the bar";
  EXPECT_EQ(screen.PixelAt(19, 2).character, "│");
  EXPECT_EQ(screen.PixelAt(19, 1).character, "│");
  EXPECT_EQ(screen.PixelAt(18, 1).character, "┃") << "the bar, on the border";
}

// A rule in the body gives way to the bar while the bar is drawn, so the bar is
// one line rather than several pieces.
TEST(ScrollCardTest, TheBarCrossesARuleInTheBody) {
  ScrollCard card;
  card.SetMaxRows(5);
  std::vector<CardRow> rows = NumberedRows(6);
  rows[1] = RuleRow(ftxui::separator());
  std::vector<std::string> lines = Draw(card, Body(std::move(rows)), 8);
  ASSERT_EQ(lines.size(), 5u);
  EXPECT_EQ(lines[2].rfind("├────────", 0), 0u) << lines[2];
  EXPECT_TRUE(HasBar({lines[2]})) << "the bar stands on the rule: " << lines[2];
}

TEST(ScrollCardTest, MeasuresTheRowsWhenGivenNoWidth) {
  ScrollCard card;
  std::vector<CardRow> rows;
  rows.push_back(TextRow(ftxui::text("a much longer row")));
  rows.push_back(RuleRow(ftxui::separator()));
  EXPECT_EQ(NaturalWidth(rows), 17) << "the rule has no say";
  EXPECT_EQ(CardWidth(card, std::move(rows), 0), 19) << "the row and a border";
}

// The head and foot are drawn in full and the body scrolls between them, so the
// card's title stays on screen.
TEST(ScrollCardTest, HoldsTheHeadAndTheFootAndScrollsBetweenThem) {
  ScrollCard card;
  card.SetMaxRows(8);
  CardRows rows;
  rows.head = {TextRow(ftxui::text("head")), RuleRow(ftxui::separator())};
  rows.body = NumberedRows(10);
  rows.foot = {RuleRow(ftxui::separator()), TextRow(ftxui::text("foot"))};

  std::vector<std::string> lines = Draw(card, rows, 8);
  ASSERT_EQ(lines.size(), 8u) << "head, rule, two body rows, rule, foot";
  EXPECT_NE(lines[1].find("head"), std::string::npos);
  EXPECT_NE(lines[3].find("row0"), std::string::npos);
  EXPECT_NE(lines[4].find("row1"), std::string::npos);
  EXPECT_NE(lines[6].find("foot"), std::string::npos);
  EXPECT_TRUE(card.Overflows()) << "the body has more than it can draw";
  // The bar runs beside the body only.
  EXPECT_FALSE(HasBar({lines[1]}));
  EXPECT_TRUE(HasBar({lines[3], lines[4]}));
  EXPECT_FALSE(HasBar({lines[6]}));

  card.ScrollBy(3);
  lines = Draw(card, rows, 8);
  EXPECT_NE(lines[1].find("head"), std::string::npos) << "the head is held";
  EXPECT_NE(lines[3].find("row3"), std::string::npos) << "the body moved";
  EXPECT_NE(lines[6].find("foot"), std::string::npos) << "the foot is held";
}

// A card too short for its fixed rows and a line between them scrolls as a
// whole. A head with its top cut off is worse than a card that scrolls.
TEST(ScrollCardTest, ScrollsEntireWhenTheFixedRowsDoNotFit) {
  ScrollCard card;
  card.SetMaxRows(4);
  CardRows rows;
  rows.head = {TextRow(ftxui::text("head"))};
  rows.body = NumberedRows(3);
  rows.foot = {TextRow(ftxui::text("foot"))};

  std::vector<std::string> lines = Draw(card, rows, 8);
  ASSERT_EQ(lines.size(), 4u);
  EXPECT_NE(lines[1].find("head"), std::string::npos);
  EXPECT_NE(lines[2].find("row0"), std::string::npos);

  card.ScrollBy(2);
  lines = Draw(card, rows, 8);
  EXPECT_NE(lines[1].find("row1"), std::string::npos) << "the head moved too";
}

TEST(ScrollCardTest, AtLeastOneRowHoweverSmallTheBudget) {
  ScrollCard card;
  card.SetMaxRows(2);
  std::vector<std::string> lines = Draw(card, Body(NumberedRows(10)), 8);
  ASSERT_EQ(lines.size(), 3u);
  EXPECT_NE(lines[1].find("row0"), std::string::npos);
}

// The rows keep their width, and the card shows a window onto them with a bar
// along the bottom.
TEST(ScrollCardTest, SqueezesToTheViewWidthAndDrawsABar) {
  ScrollCard card;
  std::vector<std::string> lines = Draw(card, Body(LetterRows(3, 20)), 20, 8);
  EXPECT_EQ(CardWidth(card, LetterRows(3, 20), 20, 8), 10)
      << "eight and borders";
  EXPECT_NE(lines[1].find("abcdefgh"), std::string::npos);
  EXPECT_EQ(lines[1].find("ijk"), std::string::npos) << "past the window";
  EXPECT_TRUE(card.OverflowsX());
  EXPECT_TRUE(HasXBar(lines));
}

TEST(ScrollCardTest, SlidesSidewaysAndHoldsToBothEnds) {
  ScrollCard card;
  Draw(card, Body(LetterRows(3, 20)), 20, 8);  // so it knows its rows
  card.ScrollXBy(4);
  EXPECT_NE(Draw(card, Body(LetterRows(3, 20)), 20, 8)[1].find("efghijkl"),
            std::string::npos);
  card.ScrollXBy(-99);
  EXPECT_NE(Draw(card, Body(LetterRows(3, 20)), 20, 8)[1].find("abcdefgh"),
            std::string::npos);
  card.ScrollXBy(99);
  EXPECT_NE(Draw(card, Body(LetterRows(3, 20)), 20, 8)[1].find("mnopqrst"),
            std::string::npos)
      << "the last of the row, and no further";
}

// The whole card scrolls sideways, head included. A clipped title is useless
// while the reader looks at the far end of a row.
TEST(ScrollCardTest, SlidesTheHeadWithTheBody) {
  ScrollCard card;
  CardRows rows;
  rows.head = {TextRow(ftxui::text("HEADHEADHEAD"))};
  rows.body = LetterRows(2, 20);
  Draw(card, rows, 20, 8);
  card.ScrollXBy(4);
  CardRows again;
  again.head = {TextRow(ftxui::text("HEADHEADHEAD"))};
  again.body = LetterRows(2, 20);
  EXPECT_NE(Draw(card, again, 20, 8)[1].find("HEADHEAD"), std::string::npos);
}

TEST(ScrollCardTest, DrawsWholeWhenTheViewHoldsIt) {
  ScrollCard card;
  std::vector<std::string> lines = Draw(card, Body(LetterRows(3, 20)), 20, 20);
  EXPECT_NE(lines[1].find("abcdefghijklmnopqrst"), std::string::npos);
  EXPECT_FALSE(card.OverflowsX());
  EXPECT_FALSE(HasXBar(lines));
  card.ScrollXBy(5);
  EXPECT_NE(Draw(card, Body(LetterRows(3, 20)), 20, 20)[1].find("abcdefghij"),
            std::string::npos)
      << "nothing off either edge to reach";
}

// The bar along the bottom takes a row of the budget like any other.
TEST(ScrollCardTest, TheHorizontalBarCostsARow) {
  ScrollCard card;
  card.SetMaxRows(6);
  std::vector<std::string> lines = Draw(card, Body(LetterRows(10, 20)), 20, 8);
  ASSERT_EQ(lines.size(), 6u);
  EXPECT_TRUE(HasXBar(lines));
  EXPECT_TRUE(card.Overflows());
}

}  // namespace
}  // namespace ms
