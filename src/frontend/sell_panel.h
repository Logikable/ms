/* SellPanel is the modal for selling copies of a single stackable item. It
 * shows the item name, the per-item and total meso value, a quantity textbox
 * flanked by `0` and `MAX` buttons, and Confirm/Cancel buttons. Focus moves
 * between five controls: the top row is [0] · textbox · [MAX], the bottom row
 * is [Confirm] [Cancel]. The textbox is selected on open; it is the only place
 * digits and Backspace edit the quantity, and it shows a blinking cursor while
 * selected. Left/Right move within a row, Down from the textbox (or the 0/MAX
 * buttons) drops to the buttons, and Up from a button returns to the textbox.
 *
 * The panel owns no game state: Reset() seeds it with the item's price and
 * stack size, quantity() reports the chosen amount, and TakeConfirmed() /
 * TakeCancelled() each return true once when the player confirms or cancels.
 */
#ifndef MS_SRC_FRONTEND_SELL_PANEL_H_
#define MS_SRC_FRONTEND_SELL_PANEL_H_

#include <string>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"

namespace ms {

class SellPanel {
 public:
  // Seeds the panel for selling from a stack of `max` copies at `unit_price`
  // meso each. Quantity defaults to the whole stack and focus to Confirm.
  void Reset(const std::string& item_name, int unit_price, int max);
  ftxui::Element Render() const;
  // Handles arrow navigation, Enter/Space activation of the focused button,
  // digit entry and Backspace on the quantity, and Escape (cancel). Returns
  // true when the event was consumed.
  bool OnEvent(ftxui::Event event);
  int quantity() const {
    return quantity_;
  }
  // Each returns true exactly once, after the player activates Confirm / Cancel
  // (or presses Escape), then resets.
  bool TakeConfirmed();
  bool TakeCancelled();

 private:
  void Activate();

  std::string item_name_;
  int unit_price_ = 0;
  int max_ = 0;
  int quantity_ = 0;
  int focus_ = 0;  // a SellFocus value (see sell_panel.cc); textbox on Reset
  bool confirmed_ = false;
  bool cancelled_ = false;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SELL_PANEL_H_
