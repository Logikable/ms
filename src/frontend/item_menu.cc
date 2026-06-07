#include "src/frontend/item_menu.h"

#include <string>
#include <utility>
#include <vector>

#include "ftxui/dom/elements.hpp"

namespace ms {

ItemMenu::ItemMenu(std::vector<std::string> options)
    : options_(std::move(options)), disabled_(options_.size(), false) {
}

ftxui::Element ItemMenu::Render(int row, int col) const {
  std::vector<ftxui::Element> items;
  for (int i = 0; i < static_cast<int>(options_.size()); ++i) {
    ftxui::Element entry = ftxui::text(" " + options_[i] + " ");
    if (i == selected_) {
      entry = entry | ftxui::inverted;
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
          ftxui::vbox(std::move(items)) | ftxui::border | ftxui::clear_under,
          ftxui::filler(),
      }),
      ftxui::filler(),
  });
}

void ItemMenu::Up() {
  int next = selected_ - 1;
  while (next >= 0 && disabled_[next]) {
    next--;
  }
  if (next >= 0) {
    selected_ = next;
  }
}

void ItemMenu::Down() {
  int next = selected_ + 1;
  while (next < static_cast<int>(options_.size()) && disabled_[next]) {
    next++;
  }
  if (next < static_cast<int>(options_.size())) {
    selected_ = next;
  }
}

void ItemMenu::Reset() {
  std::fill(disabled_.begin(), disabled_.end(), false);
  selected_ = 0;
}

void ItemMenu::Disable(int index) {
  disabled_[index] = true;
  while (selected_ < static_cast<int>(options_.size()) &&
         disabled_[selected_]) {
    selected_++;
  }
}

int ItemMenu::selected() const {
  return selected_;
}

}  // namespace ms
