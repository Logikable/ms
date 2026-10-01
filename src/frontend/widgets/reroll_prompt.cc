#include "src/frontend/widgets/reroll_prompt.h"

#include <utility>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/chrome.h"
#include "src/frontend/widgets/keys.h"

namespace ms {

void RerollPrompt::Close() {
  keep_focus_ = KeepFocus::kNone;
  confirm_.Close();
}

void RerollPrompt::FocusConfirmRow(bool cancel, bool affordable) {
  keep_focus_ = KeepFocus::kNone;
  confirm_.Open(/*cancel_selected=*/cancel || !affordable);
}

ConfirmFocus RerollPrompt::confirm_focus() const {
  return keep_focus_ == KeepFocus::kNone ? confirm_.focus()
                                         : ConfirmFocus::kNone;
}

ftxui::Element RerollPrompt::KeepButtons(bool has_after) const {
  // Spaced so each button starts in the column of the one below it: Confirm is
  // a column wider than Keep.
  ftxui::Element row = ftxui::hbox({
      ftxui::text(" "),
      ActionButton("Keep ↑", keep_focus_ == KeepFocus::kBefore),
      ftxui::text("    "),
      ActionButton("Keep ↓", keep_focus_ == KeepFocus::kAfter),
      ftxui::text(" "),
  });
  return has_after ? row : std::move(row) | ftxui::dim;
}

RerollAction RerollPrompt::OnKeepEvent(const ftxui::Event& event,
                                       bool affordable) {
  if (event == ftxui::Event::ArrowLeft) {
    keep_focus_ = KeepFocus::kBefore;
  } else if (event == ftxui::Event::ArrowRight) {
    keep_focus_ = KeepFocus::kAfter;
  } else if (event == ftxui::Event::ArrowDown) {
    FocusConfirmRow(/*cancel=*/keep_focus_ == KeepFocus::kAfter, affordable);
  } else if (IsForward(event)) {
    const bool keep_after = keep_focus_ == KeepFocus::kAfter;
    // Back to Confirm, whichever was kept: the next thing a player does after
    // keeping is roll again.
    FocusConfirmRow(/*cancel=*/false, affordable);
    return keep_after ? RerollAction::kKeepAfter : RerollAction::kKeepBefore;
  }
  return RerollAction::kNone;
}

RerollAction RerollPrompt::OnEvent(ftxui::Event event, bool affordable,
                                   bool has_after) {
  if (!confirm_.open()) {
    if (IsForward(event)) {
      // A roll the purse can't cover still opens the question, with Confirm
      // greyed. It is the same window a player rerolls their way into, and not
      // showing it would look like the shelf had gone away.
      FocusConfirmRow(/*cancel=*/false, affordable);
    }
    return RerollAction::kNone;
  }
  if (IsBack(event)) {
    Close();
    return RerollAction::kClosed;
  }
  if (keep_focus_ != KeepFocus::kNone) {
    return OnKeepEvent(event, affordable);
  }
  if (event == ftxui::Event::ArrowUp) {
    if (has_after) {
      keep_focus_ = confirm_.focus() == ConfirmFocus::kCancel
                        ? KeepFocus::kAfter
                        : KeepFocus::kBefore;
    }
    return RerollAction::kNone;
  }
  switch (confirm_.OnEvent(std::move(event), affordable)) {
    case ConfirmChoice::kPending:
      return RerollAction::kNone;
    case ConfirmChoice::kConfirmed:
      // The window stays open: Confirm buys another roll of the lines it shows.
      confirm_.Open(/*cancel_selected=*/false);
      return RerollAction::kReroll;
    case ConfirmChoice::kCancelled:
      return RerollAction::kClosed;
  }
  return RerollAction::kNone;
}

}  // namespace ms
