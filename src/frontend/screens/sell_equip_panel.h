/* SellEquipPanel is the confirm dialog for selling one item from the Equip tab.
 * Equipment doesn't stack, so there is no amount to choose, unlike SellPanel:
 * the panel names the item, says what the shop pays, and asks.
 *
 * The panel holds no game state. Reset() sets the name and price, and OnEvent()
 * reports the answer. The cursor starts on [Confirm], since the shop's buyback
 * shelf keeps the sale afterwards and there is nothing to guard against.
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
  // Sets up the dialog for selling `item_name` for `price` meso.
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
