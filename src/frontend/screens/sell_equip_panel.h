/* SellEquipPanel is the confirm dialog for selling one item off the equip tab.
 * Equipment does not stack, so there is no amount to pick and no counterpart to
 * SellPanel's selector: the panel names the item, says what the shop pays, and
 * asks.
 *
 * The panel owns no game state. Reset() seeds it with the name, the price and
 * whether the copy carries upgrades; OnEvent() reports which way the answer
 * went. The cursor opens on [Cancel], because a sale cannot be undone.
 */
#ifndef MS_SRC_FRONTEND_SCREENS_SELL_EQUIP_PANEL_H_
#define MS_SRC_FRONTEND_SCREENS_SELL_EQUIP_PANEL_H_

#include <string>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/confirm_prompt.h"

namespace ms {

class SellEquipPanel {
 public:
  // Seeds the dialog for selling `item_name` for `price` meso. `upgraded` says
  // the copy carries scrolls or stars, which the price does not cover and the
  // dialog therefore has to mention.
  void Reset(const std::string& item_name, int price, bool upgraded);
  ftxui::Element Render() const;
  ConfirmChoice OnEvent(ftxui::Event event);

 private:
  std::string item_name_;
  int price_ = 0;
  bool upgraded_ = false;
  ConfirmPrompt confirm_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_SELL_EQUIP_PANEL_H_
