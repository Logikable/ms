#include "src/frontend/screens/sell_equip_panel.h"

#include <string>
#include <utility>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/confirm_prompt.h"
#include "src/frontend/widgets/panel_util.h"

namespace ms {

void SellEquipPanel::Reset(const std::string& item_name, int price) {
  item_name_ = item_name;
  price_ = price;
  // On [Confirm]. A sale used to be final, which is what put the cursor on
  // the way out; the shop's buy-back shelf keeps it now, at the price it paid.
  confirm_.Open(/*cancel_selected=*/false);
}

ftxui::Element SellEquipPanel::Render() const {
  return DialogWindow(" Sell ",
                      {
                          CenteredRow(item_name_),
                          ThemedSeparator(),
                          CenteredRow("Sell for " + FormatMeso(price_)),
                      },
                      confirm_.Render());
}

ConfirmChoice SellEquipPanel::OnEvent(ftxui::Event event) {
  return confirm_.OnEvent(std::move(event));
}

}  // namespace ms
