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

ftxui::Element ConfirmPrompt::Render() const {
  ftxui::Element confirm = ftxui::text("[Confirm]");
  ftxui::Element cancel = ftxui::text("[Cancel]");
  if (cancel_selected_) {
    cancel = cancel | ftxui::inverted;
  } else {
    confirm = confirm | ftxui::inverted;
  }
  return ftxui::hbox(
      {ftxui::text(" "), confirm, ftxui::text("  "), cancel, ftxui::text(" ")});
}

ftxui::Element ConfirmPrompt::RenderWindow() const {
  return ThemedWindow("", Render() | ftxui::hcenter);
}

}  // namespace ms
