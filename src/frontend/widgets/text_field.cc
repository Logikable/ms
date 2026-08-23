#include "src/frontend/widgets/text_field.h"

#include "ftxui/component/event.hpp"
#include "src/frontend/widgets/panel_util.h"

namespace ms {
namespace {

bool IsAlphanumeric(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9');
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
  if (event.is_character() && event.character().size() == 1 &&
      IsAlphanumeric(event.character()[0]) &&
      static_cast<int>(text_.size()) < max_length_) {
    text_ += event.character();
  }
  return TextEntry::kPending;
}

}  // namespace ms
