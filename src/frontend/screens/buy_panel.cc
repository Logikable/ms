#include "src/frontend/screens/buy_panel.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/chrome.h"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/format.h"

namespace ms {

void BuyPanel::Reset(const std::string& item_name, int unit_price,
                     int64_t balance, int room, int owned,
                     const ItemPrototype* token) {
  item_name_ = item_name;
  unit_price_ = unit_price;
  balance_ = balance;
  owned_ = owned;
  token_ = token;
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
    affordable = balance / unit_price;
  }
  int64_t max = std::min({affordable, static_cast<int64_t>(std::max(0, room)),
                          static_cast<int64_t>(kMaxQuantity)});
  room_ = std::max(0, room);
  cap_ = static_cast<int>(max);
  selector_.Reset(cap_, /*initial=*/1);
  selector_.set_confirm_enabled(Affordable());
}

int64_t BuyPanel::total() const {
  return static_cast<int64_t>(selector_.value()) * unit_price_;
}

bool BuyPanel::Affordable() const {
  // A quantity of zero is not a purchase, so Confirm has nothing to honour --
  // the same reason it goes down when the total runs past the balance.
  return total() <= balance_ && selector_.value() > 0;
}

// The bag before the purse: a player with neither has to clear a slot whatever
// they do about the meso, and naming the currency for an item that has nowhere
// to go would send them off to earn what they could not spend.
std::string BuyPanel::Reason() const {
  if (cap_ > 0) {
    return "";
  }
  if (room_ <= 0) {
    return "Bag full";
  }
  return token_ == nullptr ? "Not enough meso" : "Not enough " + token_->name();
}

// The mark keeps its own colour whatever the number does: red is the reason
// the player cannot pay, and a currency is not a reason (colors.h).
ftxui::Element BuyPanel::Amount(int64_t value, bool red) const {
  if (token_ == nullptr) {
    return RedUnless(ftxui::text(FormatMeso(value)), !red);
  }
  ftxui::Element number =
      RedUnless(ftxui::text(" " + FormatWithCommas(value)), !red);
  return ftxui::hbox({ftxui::text(token_->currency_mark()) |
                          ftxui::color(MarkColor(token_->currency_color())),
                      std::move(number)});
}

ftxui::Element BuyPanel::Render() const {
  bool red = !Affordable();
  ftxui::Element label = RedUnless(ftxui::text("Total: "), !red);
  ftxui::Element total_row =
      ftxui::hbox({std::move(label), Amount(total(), red)});
  std::vector<ftxui::Element> rows = {
      CenteredRow(item_name_),
      ThemedSeparator(),
      // Above the price, because it is the question asked first: a player
      // deciding whether to buy another wants to know how many they have
      // before working out what it costs. Shown at zero as well -- "none yet"
      // is an answer, and a row that came and went would be read as a glitch.
      CenteredRow("Owned: " + std::to_string(owned_)),
      CenteredRow(ftxui::hbox(
          {Amount(unit_price_, /*red=*/false), ftxui::text(" each")})),
      CenteredRow(std::move(total_row)),
  };
  // Drawn only where there is nothing to offer: with a price the player can
  // meet it would be a caption to a number that already reads.
  std::string reason = Reason();
  if (!reason.empty()) {
    rows.push_back(CenteredRow(ftxui::text(reason) | ftxui::color(kRed)));
  }
  rows.push_back(ThemedSeparator());
  rows.push_back(selector_.Render());
  return ThemedWindow(" Buy ", ftxui::vbox(std::move(rows)));
}

ConfirmChoice BuyPanel::OnEvent(ftxui::Event event) {
  ConfirmChoice choice = selector_.OnEvent(std::move(event));
  // Re-asked after every keystroke rather than at render time, so Confirm is
  // already inert by the time the player can press it -- Render only draws.
  selector_.set_confirm_enabled(Affordable());
  return choice;
}

}  // namespace ms
