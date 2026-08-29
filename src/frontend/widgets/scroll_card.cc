#include "src/frontend/widgets/scroll_card.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/panel_util.h"

namespace ms {
namespace {

// The borders a framed card pays for before any row is drawn.
constexpr int kBorderRows = 2;

// One drawn line. A rule is left to stretch: sized to the rows it stops short
// of the border the moment something widens the card, and reads as a notch.
// Everything else is held to `width` with `cell` -- the bar, or a blank
// holding its column -- against the right border.
ftxui::Element Line(CardRow row, int width, bool bar, ftxui::Element cell) {
  if (row.separator) {
    return std::move(row.element);
  }
  ftxui::Element line =
      std::move(row.element) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, width);
  if (!bar) {
    return line;
  }
  // The filler takes nothing when the card is at its own width, and holds the
  // bar against the right border when something has stretched it.
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
    // Separators are left out: a rule asks for one column and stretches to
    // whatever it is given, so it has no say in how wide the card should be.
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

CardRows ScrollCard::Fitted(CardRows rows) const {
  int fixed = static_cast<int>(rows.head.size() + rows.foot.size());
  if (max_rows_ <= 0 || max_rows_ - kBorderRows - fixed >= 1) {
    return rows;
  }
  // No room for the fixed groups and a line between them. Everything scrolls
  // instead: a head with its top cut off says less than a card that moves.
  CardRows all;
  all.body = std::move(rows.head);
  Append(all.body, std::move(rows.body));
  Append(all.body, std::move(rows.foot));
  return all;
}

int ScrollCard::VisibleRows(const CardRows& rows) const {
  int total = static_cast<int>(rows.body.size());
  if (max_rows_ <= 0) {
    return total;
  }
  int fixed = static_cast<int>(rows.head.size() + rows.foot.size());
  return std::max(1, std::min(total, max_rows_ - kBorderRows - fixed));
}

void ScrollCard::ScrollBy(int delta) {
  offset_ = std::max(0, std::min(offset_ + delta, total_ - visible_));
}

ftxui::Element ScrollCard::Render(const std::string& title,
                                  std::vector<CardRow> rows, int content_width,
                                  bool focused) const {
  CardRows one;
  one.body = std::move(rows);
  return Render(title, std::move(one), content_width, focused);
}

ftxui::Element ScrollCard::Render(const std::string& title, CardRows rows,
                                  int content_width, bool focused) const {
  rows = Fitted(std::move(rows));
  total_ = static_cast<int>(rows.body.size());
  visible_ = VisibleRows(rows);
  // Clamped here as well as in ScrollBy: the terminal can be made taller under
  // a card already scrolled to its foot, which leaves the old offset too far
  // down for the window it now has.
  offset_ = std::max(0, std::min(offset_, total_ - visible_));
  int width = content_width > 0 ? content_width : NaturalWidth(rows);
  // The bar's column is held open from the moment the card has a budget to
  // outgrow, so the card does not widen the first time it does.
  bool bar = max_rows_ > 0;

  std::vector<ftxui::Element> cells = ScrollBarCells(total_, offset_, visible_);
  std::vector<ftxui::Element> lines;
  for (CardRow& row : rows.head) {
    lines.push_back(Line(std::move(row), width, bar, ftxui::text(" ")));
  }
  for (int i = 0; i < visible_; ++i) {
    // Blank while the body fits: the column is reserved either way, but a bar
    // is only drawn when there is something off screen to point at.
    ftxui::Element cell =
        cells.empty() ? ftxui::text(" ") : std::move(cells[i]);
    lines.push_back(
        Line(std::move(rows.body[offset_ + i]), width, bar, std::move(cell)));
  }
  for (CardRow& row : rows.foot) {
    lines.push_back(Line(std::move(row), width, bar, ftxui::text(" ")));
  }
  return ThemedWindow(title, ftxui::vbox(std::move(lines)), focused);
}

}  // namespace ms
