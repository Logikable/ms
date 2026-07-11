/* SellPanel is the modal for selling copies of a single stackable item. It
 * shows the item name, the per-item and total meso value, an editable quantity
 * field flanked by `0` and `MAX` buttons, and Confirm/Cancel buttons. The four
 * buttons form a 2x2 box: top row [0] [MAX], bottom row [Confirm] [Cancel];
 * Confirm is focused by default. Up/Down switch rows, Left/Right switch
 * columns. The quantity is always editable via digit keys (clamped to [0, max])
 * and Backspace; it is not a focus stop.
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
  int focus_row_ = 1;  // 0 = [0]/[MAX] row, 1 = [Confirm]/[Cancel] row
  int focus_col_ = 0;  // 0 = left button, 1 = right button
  bool confirmed_ = false;
  bool cancelled_ = false;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SELL_PANEL_H_
