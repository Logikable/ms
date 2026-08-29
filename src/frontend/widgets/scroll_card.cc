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

int ScrollCard::VisibleRows(int total) const {
  if (max_rows_ <= 0) {
    return total;
  }
  return std::max(1, std::min(total, max_rows_ - kBorderRows));
}

void ScrollCard::ScrollBy(int delta) {
  offset_ = std::max(0, std::min(offset_ + delta, total_ - visible_));
}

ftxui::Element ScrollCard::Render(const std::string& title,
                                  std::vector<CardRow> rows, int content_width,
                                  bool focused) const {
  total_ = static_cast<int>(rows.size());
  visible_ = VisibleRows(total_);
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
  for (int i = 0; i < visible_; ++i) {
    CardRow& row = rows[offset_ + i];
    if (row.separator) {
      lines.push_back(
          std::move(row.element) |
          ftxui::size(ftxui::WIDTH, ftxui::EQUAL, bar ? width + 1 : width));
      continue;
    }
    ftxui::Element line =
        std::move(row.element) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, width);
    if (!bar) {
      lines.push_back(std::move(line));
      continue;
    }
    // Blank while the whole card fits: the column is reserved either way, but
    // a bar is only drawn when there is something off screen to point at.
    ftxui::Element cell =
        cells.empty() ? ftxui::text(" ") : std::move(cells[i]);
    lines.push_back(ftxui::hbox({
        std::move(line),
        std::move(cell) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 1),
    }));
  }
  return ThemedWindow(title, ftxui::vbox(std::move(lines)), focused);
}

}  // namespace ms
