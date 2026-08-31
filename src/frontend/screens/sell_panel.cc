#include "src/frontend/screens/sell_panel.h"

#include <cstdint>
#include <string>
#include <utility>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/format.h"
#include "src/frontend/widgets/panel_util.h"

namespace ms {

void SellPanel::Reset(const std::string& item_name, int unit_price, int max) {
  item_name_ = item_name;
  unit_price_ = unit_price;
  selector_.Reset(max);
}

ftxui::Element SellPanel::Render() const {
  int64_t total = static_cast<int64_t>(selector_.value()) * unit_price_;
  ftxui::Element content = ftxui::vbox({
      CenteredRow(item_name_),
      ThemedSeparator(),
      CenteredRow(FormatMeso(unit_price_) + " each"),
      CenteredRow("Total: " + FormatMeso(total)),
      ThemedSeparator(),
      selector_.Render(),
  });
  return ThemedWindow(" Sell ", std::move(content));
}

ConfirmChoice SellPanel::OnEvent(ftxui::Event event) {
  return selector_.OnEvent(std::move(event));
}

}  // namespace ms
