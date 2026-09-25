#include "src/frontend/widgets/scroll_card.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/chrome.h"

namespace ms {
namespace {

// The rows the borders take before any row is drawn.
constexpr int kBorderRows = 2;

// One drawn line. A rule is left to stretch: if it were sized to the rows, it
// would stop short of the border when something widened the card and look like
// a notch. When the bar is drawn beside it, the rule gives way to the bar's
// cell so the bar is one unbroken line. Everything else is held to `width`,
// with `cell` (the bar, or a blank keeping its column) against the right
// border.
ftxui::Element Line(CardRow row, int width, bool bar, ftxui::Element cell,
                    bool bar_drawn = false) {
  if (row.separator) {
    if (!bar_drawn) {
      return std::move(row.element);
    }
    return ftxui::hbox({
        std::move(row.element) | ftxui::flex,
        std::move(cell) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 1),
    });
  }
  ftxui::Element line =
      std::move(row.element) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, width);
  if (!bar) {
    return line;
  }
  // The filler takes no room when the card is at its own width, and keeps the
  // bar against the right border when something has stretched the card.
  return ftxui::hbox({
      std::move(line),
      ftxui::filler(),
      std::move(cell) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 1),
  });
}

void Append(std::vector<CardRow>& rows, std::vector<CardRow> more) {
  for (CardRow& row : more) {
    rows.push_back(std::move(row));
  }
}

}  // namespace

int NaturalWidth(const std::vector<ftxui::Element>& rows) {
  ftxui::Element box = ftxui::vbox(rows);
  box->ComputeRequirement();
  return box->requirement().min_x;
}

int NaturalWidth(const std::vector<CardRow>& rows) {
  std::vector<ftxui::Element> elements;
  for (const CardRow& row : rows) {
    // Rules are left out: a rule asks for one column and stretches to whatever
    // it gets, so it has no say in the card's width.
    if (!row.separator) {
      elements.push_back(row.element);
    }
  }
  return NaturalWidth(elements);
}

int NaturalWidth(const CardRows& rows) {
  return std::max({NaturalWidth(rows.head), NaturalWidth(rows.body),
                   NaturalWidth(rows.foot)});
}

CardRows ScrollCard::Fitted(CardRows rows, int reserved) const {
  int fixed = static_cast<int>(rows.head.size() + rows.foot.size()) + reserved;
  if (max_rows_ <= 0 || max_rows_ - kBorderRows - fixed >= 1) {
    return rows;
  }
  // There isn't room for the fixed groups and a line between them, so
  // everything scrolls. A head with its top cut off is worse than a card that
  // scrolls.
  CardRows all;
  all.body = std::move(rows.head);
  Append(all.body, std::move(rows.body));
  Append(all.body, std::move(rows.foot));
  return all;
}

int ScrollCard::VisibleRows(const CardRows& rows, int reserved) const {
  int total = static_cast<int>(rows.body.size());
  if (max_rows_ <= 0) {
    return total;
  }
  int fixed = static_cast<int>(rows.head.size() + rows.foot.size()) + reserved;
  return std::max(1, std::min(total, max_rows_ - kBorderRows - fixed));
}

void ScrollCard::ScrollBy(int delta) {
  offset_ = std::max(0, std::min(offset_ + delta, total_ - visible_));
}

void ScrollCard::ScrollXBy(int delta) {
  x_offset_ = std::max(0, std::min(x_offset_ + delta, x_max_));
}

ftxui::Element ScrollCard::Render(const std::string& title,
                                  std::vector<CardRow> rows, int content_width,
                                  bool focused, int view_width) const {
  CardRows one;
  one.body = std::move(rows);
  return Render(title, std::move(one), content_width, focused, view_width);
}

ftxui::Element ScrollCard::Render(const std::string& title, CardRows rows,
                                  int content_width, bool focused,
                                  int view_width) const {
  int width = content_width > 0 ? content_width : NaturalWidth(rows);
  // Measured before fitting the rows. The horizontal bar takes a row of the
  // budget, so whether the card squeezes has to be known before the rows are
  // cut to fit.
  bool squeeze = view_width > 0 && view_width < width;
  int reserved = squeeze ? 1 : 0;
  rows = Fitted(std::move(rows), reserved);
  total_ = static_cast<int>(rows.body.size());
  visible_ = VisibleRows(rows, reserved);
  // Clamped here as well as in ScrollBy. The terminal can grow taller under a
  // card scrolled to the bottom, which leaves the old offset too far down for
  // the new window.
  offset_ = std::max(0, std::min(offset_, total_ - visible_));
  // The bar's column is reserved as soon as the card has a row budget, so the
  // card doesn't widen the first time it overflows.
  bool bar = max_rows_ > 0;

  std::vector<ftxui::Element> cells = ScrollBarCells(total_, offset_, visible_);
  std::vector<ftxui::Element> lines;
  for (CardRow& row : rows.head) {
    lines.push_back(Line(std::move(row), width, bar, ftxui::text(" ")));
  }
  for (int i = 0; i < visible_; ++i) {
    // Blank while the body fits. The column is reserved either way, but the bar
    // is drawn only when something is off screen.
    bool bar_drawn = !cells.empty();
    ftxui::Element cell = bar_drawn ? std::move(cells[i]) : ftxui::text(" ");
    lines.push_back(Line(std::move(rows.body[offset_ + i]), width, bar,
                         std::move(cell), bar_drawn));
  }
  for (CardRow& row : rows.foot) {
    lines.push_back(Line(std::move(row), width, bar, ftxui::text(" ")));
  }
  ftxui::Element card = ftxui::vbox(std::move(lines));
  if (!squeeze) {
    x_max_ = 0;
    return ThemedWindow(title, std::move(card), focused);
  }
  return ThemedWindow(title, Squeezed(std::move(card), width, view_width, bar),
                      focused);
}

ftxui::Element ScrollCard::Squeezed(ftxui::Element card, int width,
                                    int view_width, bool bar) const {
  // The vertical bar's column moves with the rows, so it counts on both sides
  // of the squeeze: in the width the card asks for and the width it is drawn
  // in.
  int full = width + (bar ? 1 : 0);
  int shown = view_width + (bar ? 1 : 0);
  x_max_ = full - shown;
  x_offset_ = std::max(0, std::min(x_offset_, x_max_));
  // A frame scrolls so the focus point is centred, so to put the reader's
  // column at the left edge the focus point is half a view to its right.
  return card | ftxui::focusPosition(x_offset_ + (shown - 1) / 2, 0) |
         ftxui::hscroll_indicator | ftxui::xframe |
         ftxui::size(ftxui::WIDTH, ftxui::EQUAL, shown);
}

}  // namespace ms
