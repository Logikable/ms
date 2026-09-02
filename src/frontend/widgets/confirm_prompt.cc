#include "src/frontend/widgets/confirm_prompt.h"

#include <utility>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/chrome.h"
#include "src/frontend/widgets/keys.h"

namespace ms {

void ConfirmPrompt::Open(bool cancel_selected) {
  open_ = true;
  cancel_selected_ = cancel_selected;
}

void ConfirmPrompt::Close() {
  open_ = false;
  cancel_selected_ = false;
}

void ConfirmPrompt::FocusCancel() {
  cancel_selected_ = true;
}

ConfirmChoice ConfirmPrompt::OnEvent(ftxui::Event event, bool confirm_enabled) {
  if (!open_) {
    return ConfirmChoice::kPending;
  }
  if (IsBack(event)) {
    Close();
    return ConfirmChoice::kCancelled;
  }
  if (event == ftxui::Event::ArrowLeft) {
    cancel_selected_ = false;
    return ConfirmChoice::kPending;
  }
  if (event == ftxui::Event::ArrowRight) {
    cancel_selected_ = true;
    return ConfirmChoice::kPending;
  }
  if (IsForward(event)) {
    if (cancel_selected_) {
      Close();
      return ConfirmChoice::kCancelled;
    }
    if (!confirm_enabled) {
      return ConfirmChoice::kPending;
    }
    Close();
    return ConfirmChoice::kConfirmed;
  }
  return ConfirmChoice::kPending;
}

ftxui::Element ConfirmButtons(ConfirmFocus focus) {
  return ConfirmButtons(focus, /*confirm_enabled=*/true);
}

ftxui::Element ConfirmButtons(ConfirmFocus focus, bool confirm_enabled) {
  return ButtonRow("Confirm", "Cancel", focus == ConfirmFocus::kConfirm,
                   focus == ConfirmFocus::kCancel, confirm_enabled);
}

ConfirmFocus ConfirmPrompt::focus() const {
  return cancel_selected_ ? ConfirmFocus::kCancel : ConfirmFocus::kConfirm;
}

ftxui::Element ConfirmPrompt::Render() const {
  return ConfirmButtons(cancel_selected_ ? ConfirmFocus::kCancel
                                         : ConfirmFocus::kConfirm);
}

ftxui::Element ConfirmPrompt::RenderWindow() const {
  return ThemedWindow("", Render() | ftxui::hcenter);
}

}  // namespace ms
