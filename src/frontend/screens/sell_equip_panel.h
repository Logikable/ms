/* SellEquipPanel is the confirm dialog for selling one item off the equip tab.
 * Equipment does not stack, so there is no amount to pick and no counterpart to
 * SellPanel's selector: the panel names the item, says what the shop pays, and
 * asks.
 *
 * The panel owns no game state. Reset() seeds it with the name and the price;
 * OnEvent() reports which way the answer went. The cursor opens on [Confirm]:
 * the shop's buy-back shelf holds the sale afterwards, so there is nothing to
 * guard the player against.
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
  // Seeds the dialog for selling `item_name` for `price` meso.
  void Reset(const std::string& item_name, int price);
  ftxui::Element Render() const;
  ConfirmChoice OnEvent(ftxui::Event event);

 private:
  std::string item_name_;
  int price_ = 0;
  ConfirmPrompt confirm_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_SELL_EQUIP_PANEL_H_
