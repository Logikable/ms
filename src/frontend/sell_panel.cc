#include "src/frontend/sell_panel.h"

#include <cstdint>
#include <string>
#include <utility>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/frontend/panel_util.h"

namespace ms {

void SellPanel::Reset(const std::string& item_name, int unit_price, int max) {
  item_name_ = item_name;
  unit_price_ = unit_price;
  selector_.Reset(max);
}

ftxui::Element SellPanel::Render() const {
  int64_t total = static_cast<int64_t>(selector_.value()) * unit_price_;
  ftxui::Element content = ftxui::vbox({
      ftxui::text(item_name_) | ftxui::hcenter,
      ThemedSeparator(),
      ftxui::text(FormatMeso(unit_price_) + " each") | ftxui::hcenter,
      ftxui::text("Total: " + FormatMeso(total)) | ftxui::hcenter,
      ThemedSeparator(),
      selector_.Render(),
  });
  return ThemedWindow(" Sell ", std::move(content));
}

bool SellPanel::OnEvent(ftxui::Event event) {
  return selector_.OnEvent(event);
}

bool SellPanel::TakeConfirmed() {
  return selector_.TakeConfirmed();
}

bool SellPanel::TakeCancelled() {
  return selector_.TakeCancelled();
}

}  // namespace ms
