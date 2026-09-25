/* ScrollCard is a bordered card of rows that scrolls when it outgrows its
 * space. It has no cursor, since there is nothing to select, only text to read,
 * so the arrows move the page.
 *
 * A card has three groups. The head and foot are always drawn in full. The body
 * between them scrolls, and the bar on the right edge runs beside the body
 * only, so the card's title stays on screen while the reader scrolls. A rule in
 * the body gives way to the bar instead of crossing it, since a bar cut by
 * rules looks like several bars.
 *
 * The bar is drawn only while there is something to scroll. Its column is
 * reserved as soon as the card has a row budget, so the card doesn't change
 * width when it overflows.
 *
 * A card given less width than its rows need squeezes instead. The rows keep
 * their width and scroll sideways under a bar along the card's bottom. The
 * whole card moves together, since otherwise its title would stay clipped while
 * the reader looks at the far end of a row.
 *
 * Every row must be exactly one line tall. A wrapped paragraph can't be sliced
 * and would draw past the card's budget.
 */
#ifndef MS_SRC_FRONTEND_WIDGETS_SCROLL_CARD_H_
#define MS_SRC_FRONTEND_WIDGETS_SCROLL_CARD_H_

#include <string>
#include <utility>
#include <vector>

#include "ftxui/dom/elements.hpp"

namespace ms {

// One row of a card. A separator spans the full width, including the bar's
// column, because a rule that stops at the text column looks like a notch. The
// exception is a rule in the body while the bar is drawn, where the bar's cell
// takes that column.
struct CardRow {
  ftxui::Element element;
  bool separator = false;
};

// The two kinds, so a caller can build rows without naming the struct.
inline CardRow TextRow(ftxui::Element element) {
  return {std::move(element), /*separator=*/false};
}
inline CardRow RuleRow(ftxui::Element element) {
  return {std::move(element), /*separator=*/true};
}

// A card's three groups. The head and foot are drawn in full and the body
// scrolls between them. A card with one section leaves the head and foot empty,
// and then all of it scrolls.
struct CardRows {
  std::vector<CardRow> head;
  std::vector<CardRow> body;
  std::vector<CardRow> foot;
};

// The width `rows` take if nothing squeezes them. A card checks this before
// deciding whether folding a row would help.
int NaturalWidth(const std::vector<CardRow>& rows);
int NaturalWidth(const std::vector<ftxui::Element>& rows);
int NaturalWidth(const CardRows& rows);

class ScrollCard {
 public:
  // The most rows the card may take, borders included. Beyond this it scrolls.
  // Zero means no limit, which suits tests and cards with plenty of room, and
  // it also keeps the bar's column closed.
  void SetMaxRows(int rows) {
    max_rows_ = rows;
  }

  // Moves the view `delta` rows, stopping at both ends. It doesn't wrap:
  // jumping from the bottom to the top is disorienting with no cursor to
  // follow.
  void ScrollBy(int delta);
  // Moves the view `delta` columns, stopping the same way. Does nothing for a
  // card that isn't squeezed, since nothing is off either edge.
  void ScrollXBy(int delta);

  // Back to the top left, for a card the player has just opened.
  void Reset() {
    offset_ = 0;
    x_offset_ = 0;
  }

  // True while the body has more rows than it can draw. A screen checks this
  // before passing the card the arrow keys. It is based on the last render, so
  // it is false until the card has been drawn once.
  bool Overflows() const {
    return total_ > visible_;
  }
  // The same check sideways: true while the card is squeezed and some columns
  // are off its edges.
  bool OverflowsX() const {
    return x_max_ > 0;
  }

  // The card, framed and titled. `content_width` is the width the rows get, not
  // counting the borders or the bar. Zero measures the rows and gives them what
  // they need. `focused` inverts the title, for a screen where two cards take
  // turns with the arrow keys. `view_width` is the width the card is drawn in.
  // If it is narrower than the rows, the card squeezes. Zero uses the rows'
  // width.
  ftxui::Element Render(const std::string& title, CardRows rows,
                        int content_width = 0, bool focused = false,
                        int view_width = 0) const;
  // A card with one section, which scrolls as a whole.
  ftxui::Element Render(const std::string& title, std::vector<CardRow> rows,
                        int content_width = 0, bool focused = false,
                        int view_width = 0) const;

 private:
  // The rows as they will be drawn. A card too short to hold the fixed groups
  // and a line between them folds them into the body, which then scrolls as a
  // whole instead of cutting off the top of the head. `reserved` is the rows
  // needed for something else: the horizontal bar, when the card is squeezed.
  CardRows Fitted(CardRows rows, int reserved) const;
  // How many body rows fit after the borders, the fixed groups and `reserved`.
  // At least one however small the budget, since a card cut to nothing says
  // less than one cut short.
  int VisibleRows(const CardRows& rows, int reserved) const;
  // The card held to the width it is drawn in, scrolling under a bar along its
  // bottom. Returned unchanged when nothing squeezes it.
  ftxui::Element Squeezed(ftxui::Element card, int width, int view_width,
                          bool bar) const;

  // Mutable, like the four below, because Render clamps the offsets to the
  // layout it draws and every panel's Render is const.
  mutable int offset_ = 0;
  mutable int x_offset_ = 0;
  int max_rows_ = 0;
  // The body rows the last render drew, which ScrollBy is limited by.
  mutable int total_ = 0;
  mutable int visible_ = 0;
  // The columns the last render left off the card's edges, which ScrollXBy is
  // limited by. Zero for a card drawn in full.
  mutable int x_max_ = 0;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_WIDGETS_SCROLL_CARD_H_
