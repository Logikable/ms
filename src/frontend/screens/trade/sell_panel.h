/* SellPanel is the dialog for selling copies of one stackable item. It shows
 * the item name and the per-item and total meso value above a shared
 * AmountSelector (a quantity box with [1] and [MAX] on either side, over
 * [Confirm]/[Cancel]). The quantity starts at the whole stack.
 *
 * The panel holds no game state: Reset() sets the item's price and stack size,
 * quantity() reports the chosen amount, and OnEvent returns the ConfirmChoice
 * every dialog returns.
 */
#ifndef MS_SRC_FRONTEND_SCREENS_SELL_PANEL_H_
#define MS_SRC_FRONTEND_SCREENS_SELL_PANEL_H_

#include <string>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/amount_selector.h"

namespace ms {

class SellPanel {
 public:
  // Sets up the panel for selling from a stack of `max` copies at `unit_price`
  // meso each. The quantity starts at the whole stack.
  void Reset(const std::string& item_name, int unit_price, int max);
  ftxui::Element Render() const;
  ConfirmChoice OnEvent(ftxui::Event event);
  int quantity() const {
    return selector_.value();
  }

 private:
  std::string item_name_;
  int unit_price_ = 0;
  AmountSelector selector_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_SELL_PANEL_H_
