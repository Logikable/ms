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
  // On [Cancel]: the item does not come back, and the player reached this
  // dialog from a menu whose other entries are all harmless.
  confirm_.Open(/*cancel_selected=*/true);
}

ftxui::Element SellEquipPanel::Render() const {
  return ThemedWindow(" Sell ",
                      ftxui::vbox({
                          CenteredRow(item_name_),
                          ThemedSeparator(),
                          CenteredRow("Sell for " + FormatMeso(price_)),
                          ThemedSeparator(),
                          confirm_.Render() | ftxui::hcenter,
                      }));
}

ConfirmChoice SellEquipPanel::OnEvent(ftxui::Event event) {
  return confirm_.OnEvent(std::move(event));
}

}  // namespace ms
