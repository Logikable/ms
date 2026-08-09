#include "src/frontend/widgets/item_menu.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/panel_util.h"

namespace ms {

ItemMenu::ItemMenu(std::vector<std::string> options)
    : options_(std::move(options)),
      disabled_(options_.size(), false),
      hidden_(options_.size(), false),
      highlighted_(options_.size(), false) {
}

ftxui::Element ItemMenu::Render(int row, int col) const {
  std::vector<ftxui::Element> items;
  for (int i = 0; i < static_cast<int>(options_.size()); ++i) {
    if (hidden_[i]) {
      continue;
    }
    std::string prefix = (i == selected_) ? "> " : "  ";
    ftxui::Element entry = ftxui::text(prefix + options_[i] + " ");
    if (highlighted_[i]) {
      // After the white the vbox paints below, so this is what the cell keeps.
      entry = entry | ftxui::color(kYellow);
    }
    if (disabled_[i]) {
      entry = entry | ftxui::dim;
    }
    items.push_back(entry);
  }
  return ftxui::vbox({
      ftxui::filler() | ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, row),
      ftxui::hbox({
          ftxui::filler() | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, col),
          ftxui::vbox(std::move(items)) | ftxui::color(ftxui::Color::White) |
              ftxui::border | ftxui::color(kTheme) | ftxui::clear_under,
          ftxui::filler(),
      }),
      ftxui::filler(),
  });
}

void ItemMenu::Up() {
  Step(-1);
}

void ItemMenu::Down() {
  Step(1);
}

void ItemMenu::Step(int delta) {
  int stops = static_cast<int>(options_.size());
  // Rounds the ring at most once, so a menu with nothing enabled stops rather
  // than walking forever -- a loop that terminates only because of a rule kept
  // elsewhere is not one to leave lying around. A full round lands back where
  // it started, which is also the right answer for a menu of one.
  int next = selected_;
  for (int i = 0; i < stops; ++i) {
    next = StepCursor(next, delta, stops);
    if (!disabled_[next]) {
      selected_ = next;
      return;
    }
  }
}

void ItemMenu::Reset() {
  std::fill(disabled_.begin(), disabled_.end(), false);
  std::fill(hidden_.begin(), hidden_.end(), false);
  std::fill(highlighted_.begin(), highlighted_.end(), false);
  selected_ = 0;
}

void ItemMenu::Hide(int index) {
  hidden_[index] = true;
  // A row that is not drawn must not be walked onto either, so hiding
  // subsumes disabling.
  Disable(index);
}

void ItemMenu::Highlight(int index) {
  highlighted_[index] = true;
}

void ItemMenu::Disable(int index) {
  disabled_[index] = true;
  // Advance past newly-disabled entry; caller must leave at least one enabled.
  while (selected_ < static_cast<int>(options_.size()) &&
         disabled_[selected_]) {
    selected_++;
  }
}

int ItemMenu::selected() const {
  return selected_;
}

}  // namespace ms
