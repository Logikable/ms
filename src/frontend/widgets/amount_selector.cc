#include "src/frontend/widgets/amount_selector.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/chrome.h"
#include "src/frontend/widgets/confirm_prompt.h"
#include "src/frontend/widgets/keys.h"

namespace ms {
namespace {

// Focusable controls, in layout order.
enum Focus { kQty = 0, kLow, kMax, kConfirm, kCancel };

// Background behind the value when the textbox isn't selected.
const ftxui::Color kFieldBg = ftxui::Color::RGB(45, 55, 75);

// The textbox's minimum width. A larger maximum widens it one digit at a time,
// so a number never outgrows its box.
constexpr int kMinFieldWidth = 8;

// The columns a textbox holding up to `max` needs: its digits, a space on each
// side, and a column for the caret.
int FieldWidth(int64_t max) {
  int digits = static_cast<int>(std::to_string(max).size());
  return std::max(kMinFieldWidth, digits + 3);
}

// Length of one blink phase; the caret shows for one phase and hides for the
// next.
constexpr int kBlinkMs = 500;

// True during the visible half of the caret's blink. Read from the wall clock
// and refreshed by the TUI's periodic redraw.
bool CaretVisible() {
  int64_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
                   .count();
  return (ms / kBlinkMs) % 2 == 0;
}

// Renders the value textbox. When selected it is white with a blinking bar
// caret after the number; otherwise it is a dark filled box.
ftxui::Element ValueField(int64_t value, int width, bool selected) {
  std::string num = std::to_string(value);
  if (!selected) {
    std::string body = " " + num + " ";
    if (static_cast<int>(body.size()) < width) {
      body += std::string(width - static_cast<int>(body.size()), ' ');
    }
    return ftxui::text(body) | ftxui::bgcolor(kFieldBg) |
           ftxui::color(ftxui::Color::White);
  }
  std::string lead = " " + num;
  int pad = width - static_cast<int>(lead.size()) - 1;  // -1 for caret
  if (pad < 0) {
    pad = 0;
  }
  return ftxui::hbox({
             ftxui::text(lead),
             ftxui::text(CaretVisible() ? "|" : " "),
             ftxui::text(std::string(pad, ' ')),
         }) |
         ftxui::bgcolor(ftxui::Color::White) |
         ftxui::color(ftxui::Color::Black);
}

}  // namespace

void AmountSelector::Reset(int64_t max) {
  Reset(max, /*initial=*/max);
}

void AmountSelector::Reset(int64_t max, int64_t initial) {
  max_ = max;
  value_ = std::clamp(initial, static_cast<int64_t>(0), max);
  focus_ = kQty;  // Start in the textbox.
  confirm_enabled_ = true;
  low_ = 1;
}

void AmountSelector::set_low(int64_t low) {
  low_ = low;
}

void AmountSelector::set_confirm_enabled(bool enabled) {
  confirm_enabled_ = enabled;
}

ftxui::Element AmountSelector::Render() const {
  std::vector<ftxui::Element> value_cells;
  value_cells.push_back(ftxui::text(" "));
  value_cells.push_back(ActionButton(std::to_string(low_), focus_ == kLow));
  value_cells.push_back(ftxui::text("  "));
  value_cells.push_back(ValueField(value_, FieldWidth(max_), focus_ == kQty));
  value_cells.push_back(ftxui::text("  "));
  value_cells.push_back(ActionButton("MAX", focus_ == kMax));
  value_cells.push_back(ftxui::text(" "));
  ftxui::Element value_row =
      ftxui::hbox(std::move(value_cells)) | ftxui::hcenter;
  ConfirmFocus button_focus = ConfirmFocus::kNone;
  if (focus_ == kConfirm) {
    button_focus = ConfirmFocus::kConfirm;
  } else if (focus_ == kCancel) {
    button_focus = ConfirmFocus::kCancel;
  }
  ftxui::Element button_row =
      ConfirmButtons(button_focus, confirm_enabled_) | ftxui::hcenter;
  return ftxui::vbox({
      value_row,
      ThemedSeparator(),
      button_row,
  });
}

ConfirmChoice AmountSelector::Activate() {
  if (focus_ == kLow) {
    // Clamped like every other input: a maximum of zero means the caller can't
    // accept one either, and a button that set one anyway would give Confirm an
    // amount that would only be refused.
    value_ = std::min(low_, max_);
  } else if (focus_ == kMax) {
    value_ = max_;
  } else if (focus_ == kConfirm) {
    // A dimmed Confirm does nothing, not just looks disabled: the caller has
    // said it can't accept this amount, so pressing it must not report one.
    return confirm_enabled_ ? ConfirmChoice::kConfirmed
                            : ConfirmChoice::kPending;
  } else if (focus_ == kCancel) {
    return ConfirmChoice::kCancelled;
  }
  // Enter on the textbox does nothing.
  return ConfirmChoice::kPending;
}

ConfirmChoice AmountSelector::OnEvent(ftxui::Event event) {
  if (event == ftxui::Event::Escape) {
    return ConfirmChoice::kCancelled;
  }
  if (event == ftxui::Event::ArrowLeft) {
    if (focus_ == kMax) {
      focus_ = kQty;
    } else if (focus_ == kQty) {
      focus_ = kLow;
    } else if (focus_ == kCancel) {
      focus_ = kConfirm;
    }
    return ConfirmChoice::kPending;
  }
  if (event == ftxui::Event::ArrowRight) {
    if (focus_ == kLow) {
      focus_ = kQty;
    } else if (focus_ == kQty) {
      focus_ = kMax;
    } else if (focus_ == kConfirm) {
      focus_ = kCancel;
    }
    return ConfirmChoice::kPending;
  }
  if (event == ftxui::Event::ArrowUp) {
    if (focus_ == kConfirm || focus_ == kCancel) {
      focus_ = kQty;
    }
    return ConfirmChoice::kPending;
  }
  if (event == ftxui::Event::ArrowDown) {
    if (focus_ == kQty || focus_ == kLow) {
      focus_ = kConfirm;
    } else if (focus_ == kMax) {
      focus_ = kCancel;
    }
    return ConfirmChoice::kPending;
  }
  if (IsForward(event)) {
    return Activate();
  }
  // The value can only be edited while the textbox is selected.
  if (focus_ == kQty) {
    // Either key deletes a digit. If the player binds Backspace to Cancel it no
    // longer reaches here, which is why Delete also works.
    if (event == ftxui::Event::Delete || event == ftxui::Event::Backspace) {
      value_ /= 10;
      return ConfirmChoice::kPending;
    }
    if (event.is_character() && event.character().size() == 1) {
      char c = event.character()[0];
      if (c >= '0' && c <= '9') {
        // Grows one digit at a time and is capped, so typing a long number with
        // a small maximum stops at the maximum instead of wrapping past it.
        int64_t next = value_ > max_ / 10 ? max_ : value_ * 10 + (c - '0');
        value_ = std::min(next, max_);
        return ConfirmChoice::kPending;
      }
    }
  }
  return ConfirmChoice::kPending;
}

}  // namespace ms
