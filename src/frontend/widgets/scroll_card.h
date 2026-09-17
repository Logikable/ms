/* ScrollCard is a bordered card of rows that scrolls when it outgrows the
 * room it is given. No cursor rides it: there is nothing to point at, only
 * text to read, so the arrows move the page itself.
 *
 * A card is built of three groups. The head and the foot are always drawn
 * whole; the body between them is what scrolls, and the bar down the right
 * edge runs beside the body alone. That way no rule crosses the bar -- a
 * card cut into sections by rules would otherwise read as several bars -- and
 * what names the card stays on screen while the reader moves through it.
 *
 * The bar is drawn only while there is something to scroll. Its column is held
 * open from the moment the card has a row budget at all, so a card does not
 * change width the moment it outgrows one.
 *
 * A card given less width than its rows ask for squeezes instead: the rows
 * keep the width they were built at and slide sideways under a bar along the
 * card's foot. The whole card moves together -- what names it would otherwise
 * stay clipped while the reader looks at the far end of a row.
 *
 * Rows must each be exactly one line tall. A row that wraps -- a paragraph --
 * cannot be sliced, and would draw past the budget the card was given.
 */
#ifndef MS_SRC_FRONTEND_WIDGETS_SCROLL_CARD_H_
#define MS_SRC_FRONTEND_WIDGETS_SCROLL_CARD_H_

#include <string>
#include <utility>
#include <vector>

#include "ftxui/dom/elements.hpp"

namespace ms {

// One row of a card. A separator is drawn the full width, the bar's column
// included: a rule held to the text column stops short of the border and
// reads as a notch.
struct CardRow {
  ftxui::Element element;
  bool separator = false;
};

// The two kinds, so a caller builds rows without naming the struct.
inline CardRow TextRow(ftxui::Element element) {
  return {std::move(element), /*separator=*/false};
}
inline CardRow RuleRow(ftxui::Element element) {
  return {std::move(element), /*separator=*/true};
}

// A card's three groups. Head and foot are drawn whole and body scrolls
// between them. A card of one section leaves head and foot empty, and then
// the whole of it scrolls.
struct CardRows {
  std::vector<CardRow> head;
  std::vector<CardRow> body;
  std::vector<CardRow> foot;
};

// The columns `rows` take if nothing squeezes them. What a card asks before
// deciding whether folding a row would buy it anything.
int NaturalWidth(const std::vector<CardRow>& rows);
int NaturalWidth(const std::vector<ftxui::Element>& rows);
int NaturalWidth(const CardRows& rows);

class ScrollCard {
 public:
  // The rows the card may take, borders included. Past this it scrolls. Zero
  // means no limit, which is what a test and a card with room to spare want,
  // and it also holds the bar's column shut.
  void SetMaxRows(int rows) {
    max_rows_ = rows;
  }

  // The columns the card is DRAWN in, borders and bar excluded. Narrower than
  // the rows ask for and the card squeezes; zero, the default, and it takes
  // the width they ask for and never squeezes.
  void SetViewWidth(int columns) {
    view_width_ = columns;
  }

  // Moves the view `delta` rows, held to the card at both ends. It does not
  // wrap: coming out of the foot at the head is disorienting with no cursor
  // to follow.
  void ScrollBy(int delta);
  // Moves it `delta` columns, held the same way. Nothing at all for a card
  // that is not squeezed: there is nothing off either edge to reach.
  void ScrollXBy(int delta);

  // Back to the top and the left, for a card the player has just opened.
  void Reset() {
    offset_ = 0;
    x_offset_ = 0;
  }

  // True while the body has more rows than it can draw, which is what a
  // screen asks before handing it the arrows. Answered from the last render,
  // so it is false until the card has been drawn once.
  bool Overflows() const {
    return total_ > visible_;
  }
  // The same question sideways: true while the card is squeezed and there are
  // columns off its edges.
  bool OverflowsX() const {
    return x_max_ > 0;
  }

  // The card, framed and titled. `content_width` is the columns the rows get,
  // counting neither the borders nor the bar; zero measures the rows and
  // gives them what they ask for. `focused` inverts the title, for a screen
  // where two cards take turns holding the arrows.
  ftxui::Element Render(const std::string& title, CardRows rows,
                        int content_width = 0, bool focused = false) const;
  // A card of one section, which scrolls entire.
  ftxui::Element Render(const std::string& title, std::vector<CardRow> rows,
                        int content_width = 0, bool focused = false) const;

 private:
  // The rows as they will be drawn: the fixed groups folded into the body of
  // a card too short to hold them and a line between them, which scrolls
  // entire rather than losing the top of its head. `reserved` is the rows the
  // card owes something else -- the horizontal bar, when it is squeezed.
  CardRows Fitted(CardRows rows, int reserved) const;
  // How many body rows fit, with the borders, the fixed groups and `reserved`
  // paid for. At least one however small the budget: a card cut to nothing
  // says less than a card cut short.
  int VisibleRows(const CardRows& rows, int reserved) const;
  // The card held to the width it is drawn in, sliding under a bar along its
  // foot. Returned whole when nothing squeezes it.
  ftxui::Element Squeezed(ftxui::Element card, int width, bool bar) const;

  // Held with the four below: Render clamps the offsets to the layout it is
  // drawing, and every panel's Render is const.
  mutable int offset_ = 0;
  mutable int x_offset_ = 0;
  int max_rows_ = 0;
  int view_width_ = 0;
  // What the last render drew of the body, which is what ScrollBy is held to.
  mutable int total_ = 0;
  mutable int visible_ = 0;
  // The columns the last render left off the card's edges, and what ScrollXBy
  // is held to. Zero for a card drawn whole.
  mutable int x_max_ = 0;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_WIDGETS_SCROLL_CARD_H_
