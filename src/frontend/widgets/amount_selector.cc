#include "src/frontend/widgets/amount_selector.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/confirm_prompt.h"
#include "src/frontend/widgets/panel_util.h"

namespace ms {
namespace {

// Focusable controls, in the order the layout presents them.
enum Focus { kQty = 0, kOne, kMax, kConfirm, kCancel };

// Fills behind the value when the textbox is not selected.
const ftxui::Color kFieldBg = ftxui::Color::RGB(45, 55, 75);

// Total field width of the value textbox.
constexpr int kFieldWidth = 8;

// Length of one blink phase; the caret shows for one and hides the next.
constexpr int kBlinkMs = 500;

// True during the visible half of the caret blink cycle. Sampled from the wall
// clock and refreshed by the TUI's periodic redraw.
bool CaretVisible() {
  int64_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
                   .count();
  return (ms / kBlinkMs) % 2 == 0;
}

// Renders the value textbox. When selected it turns white and carries a
// blinking bar caret after the number; otherwise it is a dark filled box.
ftxui::Element ValueField(int value, bool selected) {
  std::string num = std::to_string(value);
  if (!selected) {
    std::string body = " " + num + " ";
    if (static_cast<int>(body.size()) < kFieldWidth) {
      body += std::string(kFieldWidth - static_cast<int>(body.size()), ' ');
    }
    return ftxui::text(body) | ftxui::bgcolor(kFieldBg) |
           ftxui::color(ftxui::Color::White);
  }
  std::string lead = " " + num;
  int pad = kFieldWidth - static_cast<int>(lead.size()) - 1;  // -1 for caret
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

void AmountSelector::Reset(int max) {
  Reset(max, /*initial=*/max);
}

void AmountSelector::Reset(int max, int initial) {
  max_ = max;
  value_ = std::clamp(initial, 0, max);
  focus_ = kQty;  // Start in the textbox.
  confirmed_ = false;
  cancelled_ = false;
  confirm_enabled_ = true;
}

void AmountSelector::set_confirm_enabled(bool enabled) {
  confirm_enabled_ = enabled;
}

ftxui::Element AmountSelector::Render() const {
  std::vector<ftxui::Element> value_cells;
  value_cells.push_back(ftxui::text(" "));
  value_cells.push_back(ActionButton("1", focus_ == kOne));
  value_cells.push_back(ftxui::text("  "));
  value_cells.push_back(ValueField(value_, focus_ == kQty));
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

void AmountSelector::Activate() {
  if (focus_ == kOne) {
    value_ = 1;
  } else if (focus_ == kMax) {
    value_ = max_;
  } else if (focus_ == kConfirm) {
    // A dimmed Confirm is inert rather than merely unhelpful: the caller has
    // said it cannot honour this amount, so pressing it must not report one.
    confirmed_ = confirm_enabled_;
  } else if (focus_ == kCancel) {
    cancelled_ = true;
  }
  // Enter on the textbox does nothing.
}

bool AmountSelector::OnEvent(ftxui::Event event) {
  if (event == ftxui::Event::Escape) {
    cancelled_ = true;
    return true;
  }
  if (event == ftxui::Event::ArrowLeft) {
    if (focus_ == kMax) {
      focus_ = kQty;
    } else if (focus_ == kQty) {
      focus_ = kOne;
    } else if (focus_ == kCancel) {
      focus_ = kConfirm;
    }
    return true;
  }
  if (event == ftxui::Event::ArrowRight) {
    if (focus_ == kOne) {
      focus_ = kQty;
    } else if (focus_ == kQty) {
      focus_ = kMax;
    } else if (focus_ == kConfirm) {
      focus_ = kCancel;
    }
    return true;
  }
  if (event == ftxui::Event::ArrowUp) {
    if (focus_ == kConfirm || focus_ == kCancel) {
      focus_ = kQty;
    }
    return true;
  }
  if (event == ftxui::Event::ArrowDown) {
    if (focus_ == kQty || focus_ == kOne) {
      focus_ = kConfirm;
    } else if (focus_ == kMax) {
      focus_ = kCancel;
    }
    return true;
  }
  if (IsForward(event)) {
    Activate();
    return true;
  }
  // The value is editable only while the textbox is selected.
  if (focus_ == kQty) {
    if (event == ftxui::Event::Backspace) {
      value_ /= 10;
      return true;
    }
    if (event.is_character() && event.character().size() == 1) {
      char c = event.character()[0];
      if (c >= '0' && c <= '9') {
        int64_t next = static_cast<int64_t>(value_) * 10 + (c - '0');
        value_ = static_cast<int>(std::min<int64_t>(next, max_));
        return true;
      }
    }
  }
  return false;
}

bool AmountSelector::TakeConfirmed() {
  bool v = confirmed_;
  confirmed_ = false;
  return v;
}

bool AmountSelector::TakeCancelled() {
  bool v = cancelled_;
  cancelled_ = false;
  return v;
}

}  // namespace ms
