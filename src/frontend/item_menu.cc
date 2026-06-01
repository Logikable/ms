#include "src/frontend/item_menu.h"

#include <string>
#include <utility>
#include <vector>

#include "ftxui/dom/elements.hpp"

namespace ms {

ItemMenu::ItemMenu(std::vector<std::string> options)
    : options_(std::move(options)) {
}

ftxui::Element ItemMenu::Render(int row, int col) const {
  std::vector<ftxui::Element> items;
  for (int i = 0; i < static_cast<int>(options_.size()); ++i) {
    ftxui::Element entry = ftxui::text(" " + options_[i] + " ");
    if (i == selected_) {
      entry = entry | ftxui::inverted;
    }
    items.push_back(entry);
  }
  return ftxui::vbox({
      ftxui::filler() | ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, row),
      ftxui::hbox({
          ftxui::filler() | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, col),
          ftxui::vbox(std::move(items)) | ftxui::border,
          ftxui::filler(),
      }),
      ftxui::filler(),
  });
}

void ItemMenu::Up() {
  if (selected_ > 0) {
    selected_--;
  }
}

void ItemMenu::Down() {
  if (selected_ < static_cast<int>(options_.size()) - 1) {
    selected_++;
  }
}

void ItemMenu::Reset() {
  selected_ = 0;
}

int ItemMenu::selected() const {
  return selected_;
}

}  // namespace ms
