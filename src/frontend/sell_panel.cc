#include "src/frontend/sell_panel.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>
#include <utility>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/frontend/panel_util.h"

namespace ms {
namespace {

// Focusable controls, in the order the layout presents them.
enum SellFocus { kQty = 0, kZero, kMax, kConfirm, kCancel };

// Fills behind the quantity when the textbox is not selected.
const ftxui::Color kFieldBg = ftxui::Color::RGB(45, 55, 75);

// Total field width of the quantity textbox.
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

// Renders a bracketed button, inverted when focused.
ftxui::Element Button(const std::string& label, bool focused) {
  ftxui::Element e = ftxui::text("[ " + label + " ]");
  if (focused) {
    e = e | ftxui::inverted;
  }
  return e;
}

// Renders the quantity textbox. When selected it turns white and carries a
// blinking bar caret after the number; otherwise it is a dark filled box.
ftxui::Element QuantityField(int quantity, bool selected) {
  std::string num = std::to_string(quantity);
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

void SellPanel::Reset(const std::string& item_name, int unit_price, int max) {
  item_name_ = item_name;
  unit_price_ = unit_price;
  max_ = max;
  quantity_ = max;  // Default to selling the whole stack.
  focus_ = kQty;    // Start in the textbox.
  confirmed_ = false;
  cancelled_ = false;
}

ftxui::Element SellPanel::Render() const {
  int64_t total = static_cast<int64_t>(quantity_) * unit_price_;

  ftxui::Element qty_row = ftxui::hbox({
                               ftxui::text(" "),
                               Button("0", focus_ == kZero),
                               ftxui::text("  "),
                               QuantityField(quantity_, focus_ == kQty),
                               ftxui::text("  "),
                               Button("MAX", focus_ == kMax),
                               ftxui::text(" "),
                           }) |
                           ftxui::hcenter;
  ftxui::Element button_row = ftxui::hbox({
                                  ftxui::text(" "),
                                  Button("Confirm", focus_ == kConfirm),
                                  ftxui::text("   "),
                                  Button("Cancel", focus_ == kCancel),
                                  ftxui::text(" "),
                              }) |
                              ftxui::hcenter;

  ftxui::Element content = ftxui::vbox({
      ftxui::text(item_name_) | ftxui::hcenter,
      ThemedSeparator(),
      ftxui::text(FormatMeso(unit_price_) + " each") | ftxui::hcenter,
      ftxui::text("Total: " + FormatMeso(total)) | ftxui::hcenter,
      ThemedSeparator(),
      qty_row,
      ThemedSeparator(),
      button_row,
  });
  return ThemedWindow(" Sell ", std::move(content));
}

void SellPanel::Activate() {
  if (focus_ == kZero) {
    quantity_ = 0;
  } else if (focus_ == kMax) {
    quantity_ = max_;
  } else if (focus_ == kConfirm) {
    confirmed_ = true;
  } else if (focus_ == kCancel) {
    cancelled_ = true;
  }
  // Enter on the textbox does nothing.
}

bool SellPanel::OnEvent(ftxui::Event event) {
  if (event == ftxui::Event::Escape) {
    cancelled_ = true;
    return true;
  }
  if (event == ftxui::Event::ArrowLeft) {
    if (focus_ == kMax) {
      focus_ = kQty;
    } else if (focus_ == kQty) {
      focus_ = kZero;
    } else if (focus_ == kCancel) {
      focus_ = kConfirm;
    }
    return true;
  }
  if (event == ftxui::Event::ArrowRight) {
    if (focus_ == kZero) {
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
    if (focus_ == kQty || focus_ == kZero) {
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
  // The quantity is editable only while the textbox is selected.
  if (focus_ == kQty) {
    if (event == ftxui::Event::Backspace) {
      quantity_ /= 10;
      return true;
    }
    if (event.is_character() && event.character().size() == 1) {
      char c = event.character()[0];
      if (c >= '0' && c <= '9') {
        int64_t next = static_cast<int64_t>(quantity_) * 10 + (c - '0');
        quantity_ = static_cast<int>(std::min<int64_t>(next, max_));
        return true;
      }
    }
  }
  return false;
}

bool SellPanel::TakeConfirmed() {
  bool v = confirmed_;
  confirmed_ = false;
  return v;
}

bool SellPanel::TakeCancelled() {
  bool v = cancelled_;
  cancelled_ = false;
  return v;
}

}  // namespace ms
