#include "src/frontend/screens/buy_panel.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/panel_util.h"

namespace ms {

void BuyPanel::Reset(const std::string& item_name, int unit_price, int64_t meso,
                     int room, int owned) {
  item_name_ = item_name;
  unit_price_ = unit_price;
  meso_ = meso;
  owned_ = owned;
  // Capped so the field cannot be typed up to an amount the shop would only
  // refuse: at what the balance covers, at what the bag has left, and at the
  // four digits the field is meant to take. A player who cannot afford one, or
  // has nowhere to put it, gets a cap of zero and a field that will not leave
  // it.
  // A price of zero puts no ceiling on the balance. The shop never stocks a
  // free item, but the buy-back shelf carries them: a trace, and anything the
  // shop does not sell, went for nothing and comes back for nothing.
  int64_t affordable = kMaxQuantity;
  if (unit_price > 0) {
    affordable = meso / unit_price;
  }
  int64_t max = std::min({affordable, static_cast<int64_t>(std::max(0, room)),
                          static_cast<int64_t>(kMaxQuantity)});
  selector_.Reset(static_cast<int>(max), /*initial=*/1);
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
      // Above the price, because it is the question asked first: a player
      // deciding whether to buy another wants to know how many they have
      // before working out what it costs. Shown at zero as well -- "none yet"
      // is an answer, and a row that came and went would be read as a glitch.
      CenteredRow("Owned: " + std::to_string(owned_)),
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
