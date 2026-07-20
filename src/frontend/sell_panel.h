/* SellPanel is the modal for selling copies of a single stackable item. It
 * shows the item name and the per-item and total meso value above a shared
 * AmountSelector (a quantity textbox flanked by [1]/[MAX], over
 * [Confirm]/[Cancel]). The quantity opens at the whole stack.
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
#include "src/frontend/amount_selector.h"

namespace ms {

class SellPanel {
 public:
  // Seeds the panel for selling from a stack of `max` copies at `unit_price`
  // meso each. Quantity defaults to the whole stack.
  void Reset(const std::string& item_name, int unit_price, int max);
  ftxui::Element Render() const;
  bool OnEvent(ftxui::Event event);
  int quantity() const {
    return selector_.value();
  }
  bool TakeConfirmed();
  bool TakeCancelled();

 private:
  std::string item_name_;
  int unit_price_ = 0;
  AmountSelector selector_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SELL_PANEL_H_
