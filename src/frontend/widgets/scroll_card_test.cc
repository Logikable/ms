#include "src/frontend/widgets/scroll_card.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"

namespace ms {
namespace {

// `count` numbered rows, so a rendered card says which slice of them it drew.
std::vector<CardRow> NumberedRows(int count) {
  std::vector<CardRow> rows;
  for (int i = 0; i < count; ++i) {
    rows.push_back(TextRow(ftxui::text("row" + std::to_string(i))));
  }
  return rows;
}

// The columns the card asks for, borders and bar included.
int CardWidth(const ScrollCard& card, std::vector<CardRow> rows,
              int content_width) {
  ftxui::Element element = card.Render(" T ", std::move(rows), content_width);
  element->ComputeRequirement();
  return element->requirement().min_x;
}

// A card of one section, which is what most of these tests are.
CardRows Body(std::vector<CardRow> rows) {
  CardRows card;
  card.body = std::move(rows);
  return card;
}

// The card drawn onto a screen of its own, one string per line.
std::vector<std::string> Draw(const ScrollCard& card, CardRows rows,
                              int content_width) {
  ftxui::Element element = card.Render(" T ", std::move(rows), content_width);
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

// Whether the card's right-hand column carries any bar glyph.
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

TEST(ScrollCardTest, DrawsEveryRowWithNoBudget) {
  ScrollCard card;
  std::vector<std::string> lines = Draw(card, Body(NumberedRows(4)), 8);
  ASSERT_EQ(lines.size(), 6u) << "four rows and two borders";
  EXPECT_NE(lines[1].find("row0"), std::string::npos);
  EXPECT_NE(lines[4].find("row3"), std::string::npos);
  EXPECT_FALSE(card.Overflows());
  EXPECT_FALSE(HasBar(lines));
}

// The budget counts the borders, so a card given six rows draws four.
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

// The column is held open from the moment there is a budget, so the card is
// the same width whether or not it has anything to scroll.
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
  Draw(card, Body(NumberedRows(10)), 8);  // Teaches it what it is showing.

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

// A card scrolled to its foot and then given more room comes back up rather
// than drawing past the end of its rows.
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

// A rule reaches both borders, the bar's column included, and keeps reaching
// them when something stretches the card past its own width.
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

// The scroll screen hands the card a flex box wider than it asked for. The
// rule has to follow the border out, and the bar has to stay against it.
TEST(ScrollCardTest, StretchesTheRuleAndTheBarToAWiderBox) {
  ScrollCard card;
  card.SetMaxRows(6);
  std::vector<CardRow> rows = NumberedRows(9);
  rows[1] = RuleRow(ftxui::separator());
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(20),
                                               ftxui::Dimension::Fixed(6));
  ftxui::Render(screen, card.Render(" T ", std::move(rows), 8) | ftxui::flex);
  std::vector<std::string> lines;
  for (int y = 0; y < screen.dimy(); ++y) {
    std::string line;
    for (int x = 0; x < screen.dimx(); ++x) {
      line += screen.PixelAt(x, y).character;
    }
    lines.push_back(line);
  }
  EXPECT_EQ(lines[2], "├──────────────────┤") << "the rule reaches both";
  EXPECT_EQ(screen.PixelAt(19, 1).character, "│");
  EXPECT_EQ(screen.PixelAt(18, 1).character, "┃") << "the bar, on the border";
}

TEST(ScrollCardTest, MeasuresTheRowsWhenGivenNoWidth) {
  ScrollCard card;
  std::vector<CardRow> rows;
  rows.push_back(TextRow(ftxui::text("a much longer row")));
  rows.push_back(RuleRow(ftxui::separator()));
  EXPECT_EQ(NaturalWidth(rows), 17) << "the rule has no say";
  EXPECT_EQ(CardWidth(card, std::move(rows), 0), 19) << "the row and a border";
}

// Head and foot are drawn whole and the body scrolls between them, so a rule
// never crosses the bar and what names the card stays on screen.
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
  // The bar runs beside the body alone.
  EXPECT_FALSE(HasBar({lines[1]}));
  EXPECT_TRUE(HasBar({lines[3], lines[4]}));
  EXPECT_FALSE(HasBar({lines[6]}));

  card.ScrollBy(3);
  lines = Draw(card, rows, 8);
  EXPECT_NE(lines[1].find("head"), std::string::npos) << "the head is held";
  EXPECT_NE(lines[3].find("row3"), std::string::npos) << "the body moved";
  EXPECT_NE(lines[6].find("foot"), std::string::npos) << "the foot is held";
}

// A card too short to hold its fixed rows and a line between them scrolls
// entire: a head with its top cut off says less than a card that moves.
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

}  // namespace
}  // namespace ms
