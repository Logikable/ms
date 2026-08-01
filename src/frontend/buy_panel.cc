#include "src/frontend/buy_panel.h"

#include <cstdint>
#include <string>
#include <utility>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/frontend/colors.h"
#include "src/frontend/panel_util.h"

namespace ms {

void BuyPanel::Reset(const std::string& item_name, int unit_price,
                     int64_t meso) {
  item_name_ = item_name;
  unit_price_ = unit_price;
  meso_ = meso;
  // Capped at what the balance covers, so the field cannot be typed up to an
  // amount the shop would only refuse. A player who cannot afford one gets a
  // cap of zero and a field that will not leave it.
  int64_t affordable = unit_price > 0 ? meso / unit_price : 0;
  selector_.Reset(static_cast<int>(affordable), /*initial=*/1,
                  QuickPicks::kHidden);
  selector_.set_confirm_enabled(Affordable());
}

int64_t BuyPanel::total() const {
  return static_cast<int64_t>(selector_.value()) * unit_price_;
}

bool BuyPanel::Affordable() const {
  // A quantity of zero is not a purchase, so Confirm has nothing to honour --
  // the same reason it goes down when the total runs past the balance.
  return total() <= meso_ && selector_.value() > 0;
}

ftxui::Element BuyPanel::Render() const {
  ftxui::Element total_row = ftxui::text("Total: " + FormatMeso(total()));
  if (!Affordable()) {
    total_row = std::move(total_row) | ftxui::color(kRed);
  }
  ftxui::Element content = ftxui::vbox({
      CenteredRow(item_name_),
      ThemedSeparator(),
      CenteredRow(FormatMeso(unit_price_) + " each"),
      CenteredRow(std::move(total_row)),
      ThemedSeparator(),
      selector_.Render(),
  });
  return ThemedWindow(" Buy ", std::move(content));
}

bool BuyPanel::OnEvent(ftxui::Event event) {
  bool handled = selector_.OnEvent(event);
  // Re-asked after every keystroke rather than at render time, so Confirm is
  // already inert by the time the player can press it -- Render only draws.
  selector_.set_confirm_enabled(Affordable());
  return handled;
}

bool BuyPanel::TakeConfirmed() {
  return selector_.TakeConfirmed();
}

bool BuyPanel::TakeCancelled() {
  return selector_.TakeCancelled();
}

}  // namespace ms
