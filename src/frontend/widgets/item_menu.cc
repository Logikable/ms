#include "src/frontend/widgets/item_menu.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/chrome.h"
#include "src/frontend/widgets/keys.h"

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
      // The vbox below paints white first, and this colour is applied after it,
      // so the entry stays yellow.
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
          ClearUnder(ftxui::vbox(std::move(items)) |
                     ftxui::color(ftxui::Color::White) | ftxui::border |
                     ftxui::color(kTheme)),
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
  // Goes round the ring at most once, so a menu with nothing enabled stops
  // instead of looping forever. A full round lands back where it started, which
  // is also right for a menu of one.
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
  // A row that isn't drawn can't be selected either, so hiding also disables.
  Disable(index);
}

void ItemMenu::Highlight(int index) {
  // Gold means "press this", so a row that can't be pressed doesn't get it.
  // Star force is the example: it stays grey until the scroll slots are used,
  // and the trail waits there rather than pointing at a row that does nothing.
  if (disabled_[index]) {
    return;
  }
  highlighted_[index] = true;
}

void ItemMenu::SetLabel(int index, std::string label) {
  options_[index] = std::move(label);
}

void ItemMenu::Disable(int index) {
  disabled_[index] = true;
  // Move past the newly disabled entry. The caller must leave at least one
  // enabled.
  while (selected_ < static_cast<int>(options_.size()) &&
         disabled_[selected_]) {
    selected_++;
  }
}

int ItemMenu::Width() const {
  std::size_t widest = 0;
  for (int i = 0; i < static_cast<int>(options_.size()); ++i) {
    if (!hidden_[i]) {
      widest = std::max(widest, options_[i].size());
    }
  }
  // The cursor prefix, the space after the entry, and the border on each side.
  return static_cast<int>(widest) + 5;
}

int ItemMenu::selected() const {
  return selected_;
}

}  // namespace ms
