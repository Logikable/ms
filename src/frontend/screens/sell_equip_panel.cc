#include "src/frontend/screens/sell_equip_panel.h"

#include <string>
#include <utility>
#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/confirm_prompt.h"
#include "src/frontend/widgets/panel_util.h"

namespace ms {

void SellEquipPanel::Reset(const std::string& item_name, int price,
                           bool upgraded) {
  item_name_ = item_name;
  price_ = price;
  upgraded_ = upgraded;
  // On [Cancel]: the item does not come back, and the player reached this
  // dialog from a menu whose other entries are all harmless.
  confirm_.Open(/*cancel_selected=*/true);
}

ftxui::Element SellEquipPanel::Render() const {
  std::vector<ftxui::Element> rows{CenteredRow(item_name_), ThemedSeparator()};
  // "Pays nothing" rather than a zero: the starter sword and every trace are
  // worth nothing, and the sentence says what the number cannot -- that this
  // is throwing the item away rather than being paid badly for it.
  if (price_ > 0) {
    rows.push_back(CenteredRow("Pays " + FormatMeso(price_)));
  } else {
    rows.push_back(CenteredRow("Pays nothing"));
  }
  // The one warning worth a row. A player who poured traces and stars into a
  // weapon is the one this dialog can cost the most, and the price alone does
  // not tell them the investment is not in it.
  if (upgraded_) {
    rows.push_back(CenteredRow("Scrolls and stars are not paid for") |
                   ftxui::color(kRed));
  }
  rows.push_back(ThemedSeparator());
  rows.push_back(confirm_.Render() | ftxui::hcenter);
  return ThemedWindow(" Sell ", ftxui::vbox(std::move(rows)));
}

ConfirmChoice SellEquipPanel::OnEvent(ftxui::Event event) {
  return confirm_.OnEvent(std::move(event));
}

}  // namespace ms
