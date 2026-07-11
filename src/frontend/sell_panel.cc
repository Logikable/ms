#include "src/frontend/sell_panel.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/frontend/panel_util.h"

namespace ms {
namespace {

// Renders a button label, inverted when focused.
ftxui::Element Button(const std::string& label, bool focused) {
  ftxui::Element e = ftxui::text(" " + label + " ");
  if (focused) {
    e = e | ftxui::inverted;
  }
  return e;
}

}  // namespace

void SellPanel::Reset(const std::string& item_name, int unit_price, int max) {
  item_name_ = item_name;
  unit_price_ = unit_price;
  max_ = max;
  quantity_ = max;  // Default to selling the whole stack.
  focus_row_ = 1;   // Confirm.
  focus_col_ = 0;
  confirmed_ = false;
  cancelled_ = false;
}

ftxui::Element SellPanel::Render() const {
  bool zero_focus = focus_row_ == 0 && focus_col_ == 0;
  bool max_focus = focus_row_ == 0 && focus_col_ == 1;
  bool confirm_focus = focus_row_ == 1 && focus_col_ == 0;
  bool cancel_focus = focus_row_ == 1 && focus_col_ == 1;

  int64_t total = static_cast<int64_t>(quantity_) * unit_price_;
  // The quantity field: underlined to read as an editable input.
  ftxui::Element qty_field =
      ftxui::text(" " + std::to_string(quantity_) + " ") | ftxui::underlined;

  ftxui::Element qty_row = ftxui::hbox({
                               Button("0", zero_focus),
                               ftxui::text("  "),
                               qty_field,
                               ftxui::text("  "),
                               Button("MAX", max_focus),
                           }) |
                           ftxui::hcenter;
  ftxui::Element button_row = ftxui::hbox({
                                  Button("Confirm", confirm_focus),
                                  ftxui::text("   "),
                                  Button("Cancel", cancel_focus),
                              }) |
                              ftxui::hcenter;

  ftxui::Element content = ftxui::vbox({
      ftxui::text(item_name_) | ftxui::hcenter,
      ThemedSeparator(),
      ftxui::text(FormatMeso(unit_price_) + " each") | ftxui::hcenter,
      ftxui::text("Total: " + FormatMeso(total)) | ftxui::hcenter,
      ThemedSeparator(),
      qty_row,
      ftxui::text(""),
      button_row,
  });
  return ThemedWindow(" Sell ", std::move(content));
}

void SellPanel::Activate() {
  if (focus_row_ == 0 && focus_col_ == 0) {
    quantity_ = 0;
  } else if (focus_row_ == 0 && focus_col_ == 1) {
    quantity_ = max_;
  } else if (focus_row_ == 1 && focus_col_ == 0) {
    confirmed_ = true;
  } else {
    cancelled_ = true;
  }
}

bool SellPanel::OnEvent(ftxui::Event event) {
  if (event == ftxui::Event::Escape) {
    cancelled_ = true;
    return true;
  }
  if (event == ftxui::Event::ArrowLeft) {
    focus_col_ = 0;
    return true;
  }
  if (event == ftxui::Event::ArrowRight) {
    focus_col_ = 1;
    return true;
  }
  if (event == ftxui::Event::ArrowUp) {
    focus_row_ = 0;
    return true;
  }
  if (event == ftxui::Event::ArrowDown) {
    focus_row_ = 1;
    return true;
  }
  if (IsForward(event)) {
    Activate();
    return true;
  }
  // Backspace deletes the last digit rather than going back: this panel has an
  // editable field.
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
