#include "src/frontend/item_menu.h"

#include <string>
#include <utility>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/frontend/panel_util.h"

namespace ms {

ItemMenu::ItemMenu(std::vector<std::string> options)
    : options_(std::move(options)), disabled_(options_.size(), false) {
}

ftxui::Element ItemMenu::Render(int row, int col) const {
  std::vector<ftxui::Element> items;
  for (int i = 0; i < static_cast<int>(options_.size()); ++i) {
    std::string prefix = (i == selected_) ? "> " : "  ";
    ftxui::Element entry = ftxui::text(prefix + options_[i] + " ");
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
