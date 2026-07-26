#include "src/frontend/confirm_prompt.h"

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/frontend/panel_util.h"

namespace ms {

void ConfirmPrompt::Open(bool cancel_selected) {
  open_ = true;
  cancel_selected_ = cancel_selected;
}

void ConfirmPrompt::Close() {
  open_ = false;
  cancel_selected_ = false;
}

ConfirmChoice ConfirmPrompt::OnEvent(ftxui::Event event) {
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
    bool cancelled = cancel_selected_;
    Close();
    return cancelled ? ConfirmChoice::kCancelled : ConfirmChoice::kConfirmed;
  }
  return ConfirmChoice::kPending;
}

ftxui::Element ConfirmButtons(ConfirmFocus focus) {
  return ftxui::hbox({
      ftxui::text(" "),
      ActionButton("Confirm", focus == ConfirmFocus::kConfirm),
      ftxui::text("   "),
      ActionButton("Cancel", focus == ConfirmFocus::kCancel),
      ftxui::text(" "),
  });
}

ftxui::Element ConfirmPrompt::Render() const {
  return ConfirmButtons(cancel_selected_ ? ConfirmFocus::kCancel
                                         : ConfirmFocus::kConfirm);
}

ftxui::Element ConfirmPrompt::RenderWindow() const {
  return ThemedWindow("", Render() | ftxui::hcenter);
}

}  // namespace ms
