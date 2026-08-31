#include "src/frontend/widgets/text_field.h"

#include <string>

#include "ftxui/component/event.hpp"
#include "src/frontend/widgets/keys.h"

namespace ms {
namespace {

bool IsAlphanumeric(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9');
}

// `text` without the spaces on its end. A name is padded into fixed-width
// columns, where a trailing space is a space nobody typed.
std::string Trimmed(const std::string& text) {
  std::size_t last = text.find_last_not_of(' ');
  return last == std::string::npos ? "" : text.substr(0, last + 1);
}

}  // namespace

void TextField::BeginEdit() {
  editing_ = true;
  text_.clear();
}

void TextField::EndEdit() {
  editing_ = false;
  text_.clear();
}

TextEntry TextField::OnEvent(const ftxui::Event& event) {
  if (!editing_) {
    return TextEntry::kPending;
  }
  if (IsBack(event) || event == ftxui::Event::ArrowUp ||
      event == ftxui::Event::ArrowDown) {
    EndEdit();
    return TextEntry::kCancelled;
  }
  if (IsForward(event)) {
    // Committing hands the caller the buffer, so the edit ends after it is
    // read rather than here.
    text_ = Trimmed(text_);
    if (text_.empty()) {
      EndEdit();
      return TextEntry::kCancelled;
    }
    editing_ = false;
    return TextEntry::kCommitted;
  }
  if (event == ftxui::Event::Backspace || event == ftxui::Event::Delete) {
    if (!text_.empty()) {
      text_.pop_back();
    }
    return TextEntry::kPending;
  }
  if (!event.is_character() || event.character().size() != 1 ||
      static_cast<int>(text_.size()) >= max_length_) {
    return TextEntry::kPending;
  }
  char typed = event.character()[0];
  // A space is only a space between two other characters: one typed first
  // would let a name hide behind the column it is padded into.
  bool space = typed == ' ' && !text_.empty();
  if (IsAlphanumeric(typed) || space) {
    text_ += typed;
  }
  return TextEntry::kPending;
}

}  // namespace ms
